#include "tray_backend.h"

#include <functional>
#include <utility>
#include <vector>

#import <Cocoa/Cocoa.h>

// macOS tray backend: an NSStatusItem in the menu bar with an NSMenu. AppKit is
// main-thread-only, so set_model()/quit() called from other threads hop onto the
// main queue via dispatch_async. Compiled with ARC (-fobjc-arc); no manual
// retain/release.
namespace hestia::tray {
    class CocoaBackend;
}

// Receives menu clicks and forwards them to the C++ backend by item tag.
@interface HestiaTrayTarget : NSObject {
    hestia::tray::CocoaBackend *_backend;
}
- (instancetype)initWithBackend:(hestia::tray::CocoaBackend *)backend;
- (void)onMenuItem:(id)sender;
@end

namespace hestia::tray {
    class CocoaBackend final : public TrayBackend {
    public:
        explicit CocoaBackend(std::string app_name) : app_name_(std::move(app_name)) {}

        void set_model(TrayModel model) override {
            auto *held = new TrayModel(std::move(model));
            dispatch_async(dispatch_get_main_queue(), ^{
              apply_model(*held);
              delete held;
            });
        }

        void run() override {
            @autoreleasepool {
                [NSApplication sharedApplication];
                // Accessory: a menu-bar agent with no Dock icon.
                [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
                target_ = [[HestiaTrayTarget alloc] initWithBackend:this];
                status_item_ = [[NSStatusBar systemStatusBar]
                    statusItemWithLength:NSVariableStatusItemLength];
                status_item_.button.title =
                    [NSString stringWithUTF8String:app_name_.c_str()];
                [NSApp run];
            }
        }

        void quit() override {
            dispatch_async(dispatch_get_main_queue(), ^{
              [NSApp terminate:nil];
            });
        }

        // Invoked by the Objective-C target on the main thread.
        void invoke(long tag) {
            if (tag >= 0 && tag < static_cast<long>(callbacks_.size()) && callbacks_[tag]) {
                callbacks_[tag]();
            }
        }

    private:
        // Rebuild the NSMenu from the model. Main thread only.
        void apply_model(const TrayModel &model) {
            if (!status_item_) return;
            callbacks_.clear();

            NSMenu *menu = [[NSMenu alloc] init];
            menu.autoenablesItems = NO; // honour each item's enabled flag verbatim
            for (const auto &item: model.items) {
                if (item.separator) {
                    [menu addItem:[NSMenuItem separatorItem]];
                    continue;
                }
                NSMenuItem *menu_item = [[NSMenuItem alloc]
                    initWithTitle:[NSString stringWithUTF8String:item.label.c_str()]
                           action:nil
                    keyEquivalent:@""];
                menu_item.enabled = item.enabled ? YES : NO;
                menu_item.state = item.checked ? NSControlStateValueOn : NSControlStateValueOff;
                if (item.on_click) {
                    menu_item.tag = static_cast<NSInteger>(callbacks_.size());
                    menu_item.target = target_;
                    menu_item.action = @selector(onMenuItem:);
                    callbacks_.push_back(item.on_click);
                }
                [menu addItem:menu_item];
            }

            status_item_.menu = menu;
            status_item_.button.toolTip =
                [NSString stringWithUTF8String:model.tooltip.c_str()];
        }

        std::string app_name_;
        NSStatusItem *status_item_ = nil;
        HestiaTrayTarget *target_ = nil;
        std::vector<std::function<void()>> callbacks_;
    };

    std::unique_ptr<TrayBackend> make_tray_backend(std::string app_name) {
        return std::make_unique<CocoaBackend>(std::move(app_name));
    }
}

@implementation HestiaTrayTarget
- (instancetype)initWithBackend:(hestia::tray::CocoaBackend *)backend {
    if ((self = [super init])) {
        _backend = backend;
    }
    return self;
}
- (void)onMenuItem:(id)sender {
    _backend->invoke([(NSMenuItem *)sender tag]);
}
@end
