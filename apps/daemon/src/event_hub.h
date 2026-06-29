#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <hestia/ipc/protocol.h>
#include <hestia/ipc/transport.h>

namespace hestia::daemon {
    // Fans daemon events out to subscribed client connections. A frontend opts in
    // with the `events.subscribe` channel; thereafter the hub pushes matching
    // event frames down its connection. Subscribers are held weakly, so a client
    // that simply disconnects is pruned the next time it would be sent an event.
    class EventHub {
    public:
        // Subscribe `conn` to events, optionally filtered to a single process id
        // (matched against the event payload's "id"). Empty filter = all events.
        void subscribe(const std::shared_ptr<ipc::Connection> &conn,
                       std::optional<std::string> id_filter) {
            std::lock_guard<std::mutex> lk(mu_);
            subs_.push_back({conn, std::move(id_filter)});
        }

        void unsubscribe(const ipc::Connection *conn) {
            std::lock_guard<std::mutex> lk(mu_);
            std::erase_if(subs_, [&](const Sub &s) {
                const auto c = s.conn.lock();
                return !c || c.get() == conn;
            });
        }

        // Push `event` to every matching subscriber. The send happens outside the
        // lock so a slow client can't stall publishing to the others.
        void publish(const ipc::Event &event) {
            const std::string id = event.payload.value("id", std::string{});
            std::vector<std::shared_ptr<ipc::Connection> > targets;
            {
                std::lock_guard<std::mutex> lk(mu_);
                for (const auto &sub: subs_) {
                    auto c = sub.conn.lock();
                    if (!c) continue;
                    if (sub.id_filter && *sub.id_filter != id) continue;
                    targets.push_back(std::move(c));
                }
            }
            const std::string frame = ipc::encode(event);
            for (const auto &target: targets) target->send(frame);
        }

    private:
        struct Sub {
            std::weak_ptr<ipc::Connection> conn;
            std::optional<std::string> id_filter;
        };

        std::mutex mu_;
        std::vector<Sub> subs_;
    };
}
