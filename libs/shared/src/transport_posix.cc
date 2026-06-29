#include "hestia/ipc/transport.h"

#if !defined(_WIN32)

#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

namespace hestia::ipc {
    namespace fs = std::filesystem;

    namespace {
        // Cap frame size so a desynced peer fails fast instead of making us
        // allocate gigabytes from a bogus length prefix.
        constexpr std::uint32_t kMaxFrame = 16u * 1024 * 1024;

        [[noreturn]] void throw_errno(const char *what) {
            throw std::system_error(errno, std::generic_category(), what);
        }

        bool read_all(int fd, void *buf, std::size_t n) {
            auto *p = static_cast<char *>(buf);
            for (std::size_t got = 0; got < n;) {
                const ssize_t r = ::read(fd, p + got, n - got);
                if (r > 0) { got += static_cast<std::size_t>(r); continue; }
                if (r < 0 && errno == EINTR) continue;
                return false; // EOF or error
            }
            return true;
        }

        bool write_all(int fd, const void *buf, std::size_t n) {
            const auto *p = static_cast<const char *>(buf);
            for (std::size_t sent = 0; sent < n;) {
                const ssize_t r = ::write(fd, p + sent, n - sent);
                if (r >= 0) { sent += static_cast<std::size_t>(r); continue; }
                if (errno == EINTR) continue;
                return false;
            }
            return true;
        }

        bool read_frame(int fd, std::string &out) {
            std::uint32_t len_be = 0;
            if (!read_all(fd, &len_be, sizeof(len_be))) return false;
            const std::uint32_t len = ntohl(len_be);
            if (len > kMaxFrame) return false;
            out.resize(len);
            return len == 0 || read_all(fd, out.data(), len);
        }

        bool write_frame(int fd, std::string_view msg) {
            const std::uint32_t len_be = htonl(static_cast<std::uint32_t>(msg.size()));
            if (!write_all(fd, &len_be, sizeof(len_be))) return false;
            return write_all(fd, msg.data(), msg.size());
        }

        // Fill a sockaddr_un from a filesystem path, guarding the fixed-size
        // sun_path buffer (~108 bytes — the classic Unix-socket trap).
        void fill_addr(sockaddr_un &addr, const std::string &path) {
            if (path.size() >= sizeof(addr.sun_path)) {
                throw std::system_error(ENAMETOOLONG, std::generic_category(),
                                        "socket path too long: " + path);
            }
            addr.sun_family = AF_UNIX;
            std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        }

        // Is a daemon actually answering on `path`? Used to tell a live daemon
        // (refuse to start) from a stale socket left by a crash (reclaim it).
        bool endpoint_alive(const std::string &path) {
            const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) return false;
            sockaddr_un addr{};
            try { fill_addr(addr, path); } catch (...) { ::close(fd); return false; }
            const bool ok = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
            ::close(fd);
            return ok;
        }

        // A connected Unix-socket fd as a full-duplex frame pipe. send() is
        // serialized so concurrent writers can't interleave frames; recv() is
        // unblocked by close() shutting the fd down from another thread.
        class PosixConnection final : public Connection {
        public:
            explicit PosixConnection(int fd) : fd_(fd) {}
            ~PosixConnection() override { close(); }

            bool send(std::string_view frame) override {
                std::lock_guard<std::mutex> lk(write_mu_);
                const int fd = fd_.load();
                return fd >= 0 && write_frame(fd, frame);
            }

            std::optional<std::string> recv() override {
                const int fd = fd_.load();
                if (fd < 0) return std::nullopt;
                std::string frame;
                if (!read_frame(fd, frame)) return std::nullopt;
                return frame;
            }

            void close() override {
                const int fd = fd_.exchange(-1);
                if (fd >= 0) {
                    ::shutdown(fd, SHUT_RDWR); // wake a blocked recv()
                    ::close(fd);
                }
            }

        private:
            std::atomic<int> fd_;
            std::mutex write_mu_;
        };

        class PosixListener final : public Listener {
        public:
            PosixListener(int fd, fs::path path) : fd_(fd), path_(std::move(path)) {
                if (::pipe(stop_pipe_) != 0) {
                    ::close(fd_);
                    throw_errno("pipe");
                }
            }

            ~PosixListener() override {
                if (fd_ >= 0) ::close(fd_);
                if (stop_pipe_[0] >= 0) ::close(stop_pipe_[0]);
                if (stop_pipe_[1] >= 0) ::close(stop_pipe_[1]);
                std::error_code ec;
                fs::remove(path_, ec); // best-effort cleanup of our own socket
            }

            void serve(const ConnectionHandler &on_connection) override {
                running_ = true;
                while (running_) {
                    pollfd fds[2] = {
                        {fd_, POLLIN, 0},
                        {stop_pipe_[0], POLLIN, 0},
                    };
                    const int n = ::poll(fds, 2, -1);
                    if (n < 0) {
                        if (errno == EINTR) continue;
                        break;
                    }
                    if (fds[1].revents & POLLIN) break; // stop() was called
                    if (!(fds[0].revents & POLLIN)) continue;

                    const int conn = ::accept(fd_, nullptr, nullptr);
                    if (conn < 0) continue;

                    reap_finished();
                    auto connection = std::make_shared<PosixConnection>(conn);
                    auto done = std::make_shared<std::atomic<bool>>(false);
                    {
                        std::lock_guard<std::mutex> lk(workers_mu_);
                        live_.push_back(connection);
                    }
                    workers_.push_back(Worker{
                        std::thread([this, connection, done, &on_connection] {
                            on_connection(connection);
                            forget(connection);
                            done->store(true);
                        }),
                        done,
                    });
                }
                shutdown_workers();
                running_ = false;
            }

            void stop() override {
                running_ = false;
                const char byte = 1;
                // write() is async-signal-safe; this is the only thing the signal
                // handler touches.
                const ssize_t ignored = ::write(stop_pipe_[1], &byte, 1);
                (void) ignored;
            }

        private:
            struct Worker {
                std::thread thread;
                std::shared_ptr<std::atomic<bool>> done;
            };

            // Drop a connection from the live set once its handler returns.
            void forget(const std::shared_ptr<PosixConnection> &connection) {
                std::lock_guard<std::mutex> lk(workers_mu_);
                std::erase(live_, connection);
            }

            // Join the threads of connections that have already finished, so the
            // worker list doesn't grow without bound over the daemon's lifetime.
            void reap_finished() {
                for (auto it = workers_.begin(); it != workers_.end();) {
                    if (it->done->load()) {
                        if (it->thread.joinable()) it->thread.join();
                        it = workers_.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            // On shutdown, close every live connection to unblock its recv(),
            // then join all handler threads before serve() returns.
            void shutdown_workers() {
                {
                    std::lock_guard<std::mutex> lk(workers_mu_);
                    for (const auto &connection : live_) connection->close();
                }
                for (auto &worker : workers_) {
                    if (worker.thread.joinable()) worker.thread.join();
                }
                workers_.clear();
                std::lock_guard<std::mutex> lk(workers_mu_);
                live_.clear();
            }

            int fd_ = -1;
            int stop_pipe_[2] = {-1, -1};
            fs::path path_;
            std::atomic<bool> running_{false};
            std::mutex workers_mu_;
            std::vector<std::shared_ptr<PosixConnection>> live_;
            std::vector<Worker> workers_;
        };
    } // namespace

    std::unique_ptr<Listener> bind_listener(const fs::path &endpoint) {
        std::error_code ec;
        fs::create_directories(endpoint.parent_path(), ec);

        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) throw_errno("socket");

        sockaddr_un addr{};
        const std::string path = endpoint.string();
        try {
            fill_addr(addr, path);
        } catch (...) {
            ::close(fd);
            throw;
        }

        auto try_bind = [&] {
            return ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0;
        };

        if (!try_bind()) {
            if (errno != EADDRINUSE) {
                ::close(fd);
                throw_errno("bind");
            }
            // Address in use: a live daemon, or a stale socket from a crash?
            if (endpoint_alive(path)) {
                ::close(fd);
                throw std::system_error(EADDRINUSE, std::generic_category(),
                                        "hestiad is already running");
            }
            ::unlink(path.c_str()); // reclaim the stale socket
            if (!try_bind()) {
                ::close(fd);
                throw_errno("bind");
            }
        }

        if (::listen(fd, 16) != 0) {
            ::close(fd);
            throw_errno("listen");
        }
        return std::make_unique<PosixListener>(fd, endpoint);
    }

    std::shared_ptr<Connection> connect(const fs::path &endpoint) {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) throw_errno("socket");
        sockaddr_un addr{};
        try {
            fill_addr(addr, endpoint.string());
        } catch (...) {
            ::close(fd);
            throw;
        }
        if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            const int err = errno;
            ::close(fd);
            throw std::system_error(err, std::generic_category(),
                                    "no daemon at " + endpoint.string());
        }
        return std::make_shared<PosixConnection>(fd);
    }
}

#endif // !_WIN32
