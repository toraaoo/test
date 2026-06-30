#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <hestia/ipc/protocol.h>
#include <hestia/ipc/transport.h>

namespace hestia::daemon {
    // Fans daemon events out to subscribed client connections (via the
    // `events.subscribe` channel). Subscribers are held weakly, so a disconnected
    // client is pruned the next time it would be sent an event.
    //
    // publish() only enqueues; a delivery thread does the blocking sends, so a
    // slow client can't stall the supervisor loop between ticks. The queue is
    // bounded and drops its oldest events under backpressure — they are advisory.
    class EventHub {
    public:
        EventHub() : worker_([this] { deliver_loop(); }) {}

        ~EventHub() {
            {
                std::lock_guard<std::mutex> lk(mu_);
                stop_ = true;
            }
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();
        }

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

        void publish(const ipc::Event &event) {
            std::lock_guard<std::mutex> lk(mu_);
            if (queue_.size() >= kMaxQueued) queue_.pop_front();
            queue_.push_back(event);
            cv_.notify_one();
        }

    private:
        struct Sub {
            std::weak_ptr<ipc::Connection> conn;
            std::optional<std::string> id_filter;
        };

        static constexpr std::size_t kMaxQueued = 1024;

        void deliver_loop() {
            for (;;) {
                ipc::Event event;
                {
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait(lk, [this] { return stop_ || !queue_.empty(); });
                    if (stop_ && queue_.empty()) return;
                    event = std::move(queue_.front());
                    queue_.pop_front();
                }
                send_to_subscribers(event);
            }
        }

        // Snapshot targets under the lock, send outside it so one slow client
        // can't stall the others.
        void send_to_subscribers(const ipc::Event &event) {
            const std::string id = event.payload.value("id", std::string{});
            std::vector<std::shared_ptr<ipc::Connection>> targets;
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

        std::mutex mu_;
        std::condition_variable cv_;
        std::vector<Sub> subs_;
        std::deque<ipc::Event> queue_;
        bool stop_ = false;
        std::thread worker_;
    };
}
