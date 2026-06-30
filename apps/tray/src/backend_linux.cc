#include "tray_backend.h"
#include "tray_icon_data.h"

#include <mutex>
#include <utility>
#include <vector>

#include <gio/gio.h>
#include <unistd.h>

#include <hestia/app_info.h>

// Linux tray backend with no GUI toolkit: speaks the StatusNotifierItem and
// com.canonical.dbusmenu D-Bus protocols directly over GDBus (gio-2.0), keeping
// the dependency surface to glib/gio. The GMainLoop is the UI thread; set_model()/
// quit() from other threads hop onto it with g_main_context_invoke().
namespace hestia::tray {
    namespace {
        constexpr char kItemPath[] = "/StatusNotifierItem";
        constexpr char kMenuPath[] = "/MenuBar";
        constexpr char kWatcherName[] = "org.kde.StatusNotifierWatcher";
        constexpr char kWatcherPath[] = "/StatusNotifierWatcher";
        constexpr char kItemIface[] = "org.kde.StatusNotifierItem";
        constexpr char kMenuIface[] = "com.canonical.dbusmenu";

        // Only the members we actually serve. Hosts read item properties and call
        // the dbusmenu layout/event methods; everything else has sane defaults.
        constexpr char kIntrospectionXml[] = R"XML(
<node>
  <interface name="org.kde.StatusNotifierItem">
    <property name="Category" type="s" access="read"/>
    <property name="Id" type="s" access="read"/>
    <property name="Title" type="s" access="read"/>
    <property name="Status" type="s" access="read"/>
    <property name="IconName" type="s" access="read"/>
    <property name="IconPixmap" type="a(iiay)" access="read"/>
    <property name="Menu" type="o" access="read"/>
    <property name="ItemIsMenu" type="b" access="read"/>
    <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
    <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
    <method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
    <signal name="NewTitle"/>
    <signal name="NewIcon"/>
    <signal name="NewToolTip"/>
    <signal name="NewStatus"><arg type="s"/></signal>
  </interface>
  <interface name="com.canonical.dbusmenu">
    <property name="Version" type="u" access="read"/>
    <property name="Status" type="s" access="read"/>
    <method name="GetLayout">
      <arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/>
      <arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/>
    </method>
    <method name="GetGroupProperties">
      <arg type="ai" direction="in"/><arg type="as" direction="in"/>
      <arg type="a(ia{sv})" direction="out"/>
    </method>
    <method name="GetProperty">
      <arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/>
    </method>
    <method name="Event">
      <arg type="i" direction="in"/><arg type="s" direction="in"/>
      <arg type="v" direction="in"/><arg type="u" direction="in"/>
    </method>
    <method name="EventGroup">
      <arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/>
    </method>
    <method name="AboutToShow">
      <arg type="i" direction="in"/><arg type="b" direction="out"/>
    </method>
    <method name="AboutToShowGroup">
      <arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/>
    </method>
    <signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
    <signal name="ItemsPropertiesUpdated"><arg type="a(ia{sv})"/><arg type="a(ias)"/></signal>
  </interface>
</node>
)XML";

        class GDBusTrayBackend final : public TrayBackend {
        public:
            explicit GDBusTrayBackend(std::string app_name)
                : app_name_(std::move(app_name)) {}

            void set_model(TrayModel model) override {
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    model_ = std::move(model);
                    ++revision_;
                }
                // Tell the host its cached layout/tooltip is stale. Marshalled onto
                // the loop thread; a no-op until the connection is up.
                g_main_context_invoke(nullptr, &GDBusTrayBackend::emit_updated, this);
            }

            void run() override {
                GError *error = nullptr;
                connection_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
                if (!connection_) {
                    g_printerr("hestia-tray: no session bus: %s\n", error ? error->message : "?");
                    g_clear_error(&error);
                    return;
                }

                nodes_ = g_dbus_node_info_new_for_xml(kIntrospectionXml, &error);
                if (!nodes_) {
                    g_printerr("hestia-tray: bad introspection: %s\n", error ? error->message : "?");
                    g_clear_error(&error);
                    return;
                }

                static const GDBusInterfaceVTable item_vtable{
                    &GDBusTrayBackend::item_method, &GDBusTrayBackend::item_get_property, nullptr, {}};
                static const GDBusInterfaceVTable menu_vtable{
                    &GDBusTrayBackend::menu_method, &GDBusTrayBackend::menu_get_property, nullptr, {}};

                g_dbus_connection_register_object(
                    connection_, kItemPath,
                    g_dbus_node_info_lookup_interface(nodes_, kItemIface),
                    &item_vtable, this, nullptr, nullptr);
                g_dbus_connection_register_object(
                    connection_, kMenuPath,
                    g_dbus_node_info_lookup_interface(nodes_, kMenuIface),
                    &menu_vtable, this, nullptr, nullptr);

                bus_name_ = "org.kde.StatusNotifierItem-" + std::to_string(::getpid()) + "-1";
                g_bus_own_name_on_connection(
                    connection_, bus_name_.c_str(), G_BUS_NAME_OWNER_FLAGS_NONE,
                    nullptr, nullptr, this, nullptr);

                // Register with the watcher whenever it appears — not just once.
                // This survives a watcher that starts after us (login race) or
                // restarts (panel reload), so the icon comes back on its own.
                watcher_id_ = g_bus_watch_name_on_connection(
                    connection_, kWatcherName, G_BUS_NAME_WATCHER_FLAGS_NONE,
                    &GDBusTrayBackend::on_watcher_appeared, nullptr, this, nullptr);

                loop_ = g_main_loop_new(nullptr, FALSE);
                g_main_loop_run(loop_);

                if (watcher_id_) g_bus_unwatch_name(watcher_id_);
                g_main_loop_unref(loop_);
                g_dbus_node_info_unref(nodes_);
                g_object_unref(connection_);
            }

            void quit() override {
                g_main_context_invoke(
                    nullptr,
                    [](gpointer self) -> gboolean {
                        auto *b = static_cast<GDBusTrayBackend *>(self);
                        if (b->loop_) g_main_loop_quit(b->loop_);
                        return G_SOURCE_REMOVE;
                    },
                    this);
            }

        private:
            // Register our item with the StatusNotifierWatcher so the host (panel)
            // shows the icon. Called every time the watcher appears.
            static void on_watcher_appeared(GDBusConnection *conn, const gchar *, const gchar *,
                                            gpointer self) {
                auto *b = static_cast<GDBusTrayBackend *>(self);
                g_dbus_connection_call(
                    conn, kWatcherName, kWatcherPath, kWatcherName, "RegisterStatusNotifierItem",
                    g_variant_new("(s)", b->bus_name_.c_str()), nullptr, G_DBUS_CALL_FLAGS_NONE, -1,
                    nullptr, nullptr, nullptr);
            }

            static gboolean emit_updated(gpointer self) {
                auto *b = static_cast<GDBusTrayBackend *>(self);
                if (!b->connection_) return G_SOURCE_REMOVE;
                guint revision;
                {
                    std::lock_guard<std::mutex> lk(b->mu_);
                    revision = b->revision_;
                }
                g_dbus_connection_emit_signal(
                    b->connection_, nullptr, kMenuPath, kMenuIface, "LayoutUpdated",
                    g_variant_new("(ui)", revision, 0), nullptr);
                g_dbus_connection_emit_signal(
                    b->connection_, nullptr, kItemPath, kItemIface, "NewToolTip",
                    nullptr, nullptr);
                return G_SOURCE_REMOVE;
            }

            static GVariant *item_get_property(GDBusConnection *, const gchar *, const gchar *,
                                               const gchar *, const gchar *name, GError **,
                                               gpointer self) {
                auto *b = static_cast<GDBusTrayBackend *>(self);
                const std::string prop = name;
                if (prop == "Category") return g_variant_new_string("ApplicationStatus");
                if (prop == "Id") return g_variant_new_string(APP_ID);
                if (prop == "Title") return g_variant_new_string(b->app_name_.c_str());
                if (prop == "Status") return g_variant_new_string("Active");
                if (prop == "IconName") return g_variant_new_string("hestia");
                if (prop == "IconPixmap") {
                    // Embedded ARGB pixmaps so the icon renders even when the
                    // app runs uninstalled (no "hestia" icon in the theme path).
                    GVariantBuilder builder;
                    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(iiay)"));
                    for (const auto &px : kTrayIcons) {
                        GVariant *bytes = g_variant_new_fixed_array(
                                G_VARIANT_TYPE_BYTE, px.argb, px.len, sizeof(unsigned char));
                        g_variant_builder_add(&builder, "(ii@ay)", px.size, px.size, bytes);
                    }
                    return g_variant_builder_end(&builder);
                }
                if (prop == "Menu") return g_variant_new_object_path(kMenuPath);
                if (prop == "ItemIsMenu") return g_variant_new_boolean(TRUE);
                if (prop == "ToolTip") {
                    std::string tip;
                    {
                        std::lock_guard<std::mutex> lk(b->mu_);
                        tip = b->model_.tooltip;
                    }
                    GVariant *pixmaps = g_variant_new_array(G_VARIANT_TYPE("(iiay)"), nullptr, 0);
                    return g_variant_new("(s@a(iiay)ss)", "applications-system", pixmaps,
                                         tip.c_str(), "");
                }
                return nullptr;
            }

            static void item_method(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                                    const gchar *, GVariant *, GDBusMethodInvocation *inv, gpointer) {
                // Activate/ContextMenu/Scroll: nothing to do — ItemIsMenu means the
                // host renders our menu itself. Acknowledge with an empty reply.
                g_dbus_method_invocation_return_value(inv, nullptr);
            }

            static GVariant *menu_get_property(GDBusConnection *, const gchar *, const gchar *,
                                               const gchar *, const gchar *name, GError **,
                                               gpointer) {
                const std::string prop = name;
                if (prop == "Version") return g_variant_new_uint32(3);
                if (prop == "Status") return g_variant_new_string("normal");
                return nullptr;
            }

            // Build the a{sv} property dict for one menu item (id is carried
            // separately in the layout tuple).
            static GVariant *item_props(const MenuItem &item) {
                GVariantBuilder b;
                g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
                if (item.separator) {
                    g_variant_builder_add(&b, "{sv}", "type", g_variant_new_string("separator"));
                    return g_variant_builder_end(&b);
                }
                g_variant_builder_add(&b, "{sv}", "label", g_variant_new_string(item.label.c_str()));
                g_variant_builder_add(&b, "{sv}", "enabled", g_variant_new_boolean(item.enabled));
                g_variant_builder_add(&b, "{sv}", "visible", g_variant_new_boolean(TRUE));
                if (item.checked) {
                    g_variant_builder_add(&b, "{sv}", "toggle-type",
                                          g_variant_new_string("checkmark"));
                    g_variant_builder_add(&b, "{sv}", "toggle-state", g_variant_new_int32(1));
                }
                return g_variant_builder_end(&b);
            }

            static GVariant *leaf_node(int id, const MenuItem &item) {
                GVariantBuilder children;
                g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
                return g_variant_new("(i@a{sv}@av)", id, item_props(item),
                                     g_variant_builder_end(&children));
            }

            GVariant *root_node() {
                std::lock_guard<std::mutex> lk(mu_);
                GVariantBuilder props;
                g_variant_builder_init(&props, G_VARIANT_TYPE("a{sv}"));
                g_variant_builder_add(&props, "{sv}", "children-display",
                                      g_variant_new_string("submenu"));

                GVariantBuilder children;
                g_variant_builder_init(&children, G_VARIANT_TYPE("av"));
                for (std::size_t i = 0; i < model_.items.size(); ++i) {
                    g_variant_builder_add(&children, "v",
                                          leaf_node(static_cast<int>(i + 1), model_.items[i]));
                }
                return g_variant_new("(i@a{sv}@av)", 0, g_variant_builder_end(&props),
                                     g_variant_builder_end(&children));
            }

            void dispatch_menu(const gchar *method, GVariant *params, GDBusMethodInvocation *inv) {
                const std::string name = method;
                if (name == "GetLayout") {
                    guint revision;
                    {
                        std::lock_guard<std::mutex> lk(mu_);
                        revision = revision_;
                    }
                    g_dbus_method_invocation_return_value(
                        inv, g_variant_new("(u@(ia{sv}av))", revision, root_node()));
                } else if (name == "GetGroupProperties") {
                    g_dbus_method_invocation_return_value(inv, group_properties(params));
                } else if (name == "GetProperty") {
                    // Hosts that fetch via GetLayout/GetGroupProperties rarely reach
                    // here; an empty string keeps them happy.
                    g_dbus_method_invocation_return_value(inv,
                                                          g_variant_new("(v)", g_variant_new_string("")));
                } else if (name == "Event") {
                    handle_event(params);
                    g_dbus_method_invocation_return_value(inv, nullptr);
                } else if (name == "EventGroup") {
                    // Some hosts batch clicks here instead of calling Event per
                    // item. Dispatch each, then return an empty idErrors array.
                    handle_event_group(params);
                    GVariantBuilder errs;
                    g_variant_builder_init(&errs, G_VARIANT_TYPE("ai"));
                    g_dbus_method_invocation_return_value(
                        inv, g_variant_new("(@ai)", g_variant_builder_end(&errs)));
                } else if (name == "AboutToShow") {
                    g_dbus_method_invocation_return_value(inv, g_variant_new("(b)", FALSE));
                } else if (name == "AboutToShowGroup") {
                    // (updatesNeeded, idErrors) — nothing to update, nothing failed.
                    GVariantBuilder a, b;
                    g_variant_builder_init(&a, G_VARIANT_TYPE("ai"));
                    g_variant_builder_init(&b, G_VARIANT_TYPE("ai"));
                    g_dbus_method_invocation_return_value(
                        inv, g_variant_new("(@ai@ai)", g_variant_builder_end(&a),
                                           g_variant_builder_end(&b)));
                } else {
                    g_dbus_method_invocation_return_error(
                        inv, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD, "unsupported: %s", method);
                }
            }

            GVariant *group_properties(GVariant *params) {
                GVariant *ids_v = g_variant_get_child_value(params, 0);
                std::vector<int> ids;
                GVariantIter it;
                g_variant_iter_init(&it, ids_v);
                gint32 id;
                while (g_variant_iter_next(&it, "i", &id)) ids.push_back(id);
                g_variant_unref(ids_v);

                std::lock_guard<std::mutex> lk(mu_);
                if (ids.empty()) { // empty request means "all"
                    ids.push_back(0);
                    for (std::size_t i = 0; i < model_.items.size(); ++i) ids.push_back(static_cast<int>(i + 1));
                }

                GVariantBuilder out;
                g_variant_builder_init(&out, G_VARIANT_TYPE("a(ia{sv})"));
                for (int wanted: ids) {
                    if (wanted == 0) {
                        GVariantBuilder p;
                        g_variant_builder_init(&p, G_VARIANT_TYPE("a{sv}"));
                        g_variant_builder_add(&p, "{sv}", "children-display",
                                              g_variant_new_string("submenu"));
                        g_variant_builder_add(&out, "(i@a{sv})", 0, g_variant_builder_end(&p));
                    } else if (wanted >= 1 && static_cast<std::size_t>(wanted) <= model_.items.size()) {
                        g_variant_builder_add(&out, "(i@a{sv})", wanted,
                                              item_props(model_.items[wanted - 1]));
                    }
                }
                return g_variant_new("(@a(ia{sv}))", g_variant_builder_end(&out));
            }

            void handle_event(GVariant *params) {
                gint32 id = 0;
                const gchar *event_id = nullptr;
                g_variant_get_child(params, 0, "i", &id);
                g_variant_get_child(params, 1, "&s", &event_id);
                dispatch_click(id, event_id);
            }

            // EventGroup carries an array of (id, eventId, data, timestamp).
            void handle_event_group(GVariant *params) {
                GVariant *events = g_variant_get_child_value(params, 0);
                GVariantIter it;
                g_variant_iter_init(&it, events);
                gint32 id = 0;
                const gchar *event_id = nullptr;
                GVariant *data = nullptr;
                guint32 timestamp = 0;
                while (g_variant_iter_next(&it, "(i&s@vu)", &id, &event_id, &data, &timestamp)) {
                    dispatch_click(id, event_id);
                    if (data) g_variant_unref(data);
                }
                g_variant_unref(events);
            }

            // Fire the menu item's callback for a "clicked" event. Shared by Event
            // and EventGroup so every host's click style reaches the same path.
            void dispatch_click(gint32 id, const gchar *event_id) {
                if (!event_id || std::string(event_id) != "clicked") return;
                std::function<void()> callback;
                {
                    std::lock_guard<std::mutex> lk(mu_);
                    if (id >= 1 && static_cast<std::size_t>(id) <= model_.items.size()) {
                        callback = model_.items[id - 1].on_click;
                    }
                }
                if (callback) callback(); // outside the lock: it may call set_model()
            }

            // Trampoline: GDBus hands method calls to a static; route to the member.
            static void menu_method(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                                    const gchar *method, GVariant *params,
                                    GDBusMethodInvocation *inv, gpointer self) {
                static_cast<GDBusTrayBackend *>(self)->dispatch_menu(method, params, inv);
            }

            std::string app_name_;
            std::string bus_name_;
            GDBusConnection *connection_ = nullptr;
            GDBusNodeInfo *nodes_ = nullptr;
            GMainLoop *loop_ = nullptr;
            guint watcher_id_ = 0;

            std::mutex mu_;
            TrayModel model_;
            guint revision_ = 1;
        };
    } // namespace

    std::unique_ptr<TrayBackend> make_tray_backend(std::string app_name) {
        return std::make_unique<GDBusTrayBackend>(std::move(app_name));
    }
}
