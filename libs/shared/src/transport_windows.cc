#include "hestia/ipc/transport.h"

#if defined(_WIN32)

// Windows named-pipe transport, mirroring transport_posix.cc's contract.
// Connections use overlapped I/O so a close() from another thread can unblock an
// in-flight recv()/send() via a shared close event (the analogue of shutdown()).

#include <windows.h>

#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace hestia::ipc {
    namespace fs = std::filesystem;

    namespace {
        // Cap frame size so a desynced peer fails fast instead of making us
        // allocate gigabytes from a bogus length prefix.
        constexpr std::uint32_t kMaxFrame = 16u * 1024 * 1024;
        constexpr DWORD kPipeBuffer = 64u * 1024;

        // 4-byte big-endian length prefix, matching the POSIX side's htonl wire
        // format so the two transports are protocol-compatible.
        void put_be32(unsigned char *p, std::uint32_t v) {
            p[0] = static_cast<unsigned char>((v >> 24) & 0xff);
            p[1] = static_cast<unsigned char>((v >> 16) & 0xff);
            p[2] = static_cast<unsigned char>((v >> 8) & 0xff);
            p[3] = static_cast<unsigned char>(v & 0xff);
        }

        std::uint32_t get_be32(const unsigned char *p) {
            return (static_cast<std::uint32_t>(p[0]) << 24) |
                   (static_cast<std::uint32_t>(p[1]) << 16) |
                   (static_cast<std::uint32_t>(p[2]) << 8) |
                   static_cast<std::uint32_t>(p[3]);
        }

        std::wstring current_user_sid() {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
                return {};
            }
            DWORD len = 0;
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &len);
            std::vector<unsigned char> buf(len);
            std::wstring sid;
            if (len && ::GetTokenInformation(token, TokenUser, buf.data(), len, &len)) {
                auto *user = reinterpret_cast<TOKEN_USER *>(buf.data());
                LPWSTR str = nullptr;
                if (::ConvertSidToStringSidW(user->User.Sid, &str)) {
                    sid = str;
                    ::LocalFree(str);
                }
            }
            ::CloseHandle(token);
            return sid;
        }

        // Restricts the pipe to the current user and LocalSystem; the default DACL
        // would let any local authenticated user connect. get() is null on failure.
        class PipeSecurity {
        public:
            PipeSecurity() {
                std::wstring sddl = L"D:(A;;FA;;;SY)";
                if (const std::wstring sid = current_user_sid(); !sid.empty()) {
                    sddl += L"(A;;FA;;;" + sid + L")";
                }
                if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        sddl.c_str(), SDDL_REVISION_1, &sd_, nullptr)) {
                    sa_.nLength = sizeof(sa_);
                    sa_.lpSecurityDescriptor = sd_;
                    sa_.bInheritHandle = FALSE;
                }
            }
            ~PipeSecurity() { if (sd_) ::LocalFree(sd_); }
            PipeSecurity(const PipeSecurity &) = delete;
            PipeSecurity &operator=(const PipeSecurity &) = delete;

            SECURITY_ATTRIBUTES *get() { return sd_ ? &sa_ : nullptr; }

        private:
            PSECURITY_DESCRIPTOR sd_ = nullptr;
            SECURITY_ATTRIBUTES sa_{};
        };

        HANDLE create_pipe_instance(const std::wstring &name, bool first) {
            DWORD open_mode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
            if (first) open_mode |= FILE_FLAG_FIRST_PIPE_INSTANCE;
            PipeSecurity sec;
            return ::CreateNamedPipeW(
                name.c_str(), open_mode,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                    PIPE_REJECT_REMOTE_CLIENTS,
                PIPE_UNLIMITED_INSTANCES, kPipeBuffer, kPipeBuffer, 0, sec.get());
        }

        // A connected pipe handle as a full-duplex frame pipe. recv() and send()
        // use independent overlapped operations (separate events) so they can run
        // concurrently; send() is serialized so writers can't interleave frames.
        // close() signals close_event_ and cancels in-flight I/O to unblock both.
        class WinConnection final : public Connection {
        public:
            explicit WinConnection(HANDLE pipe) : pipe_(pipe) {
                read_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                write_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                close_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            }

            ~WinConnection() override {
                close();
                if (pipe_ != INVALID_HANDLE_VALUE) ::CloseHandle(pipe_);
                if (read_event_) ::CloseHandle(read_event_);
                if (write_event_) ::CloseHandle(write_event_);
                if (close_event_) ::CloseHandle(close_event_);
            }

            bool send(std::string_view frame) override {
                std::lock_guard<std::mutex> lk(write_mu_);
                if (closed_.load()) return false;
                unsigned char hdr[4];
                put_be32(hdr, static_cast<std::uint32_t>(frame.size()));
                if (!io(false, hdr, sizeof(hdr))) return false;
                return frame.empty() || io(false, frame.data(), frame.size());
            }

            std::optional<std::string> recv() override {
                unsigned char hdr[4];
                if (!io(true, hdr, sizeof(hdr))) return std::nullopt;
                const std::uint32_t len = get_be32(hdr);
                if (len > kMaxFrame) return std::nullopt;
                std::string out;
                out.resize(len);
                if (len != 0 && !io(true, out.data(), len)) return std::nullopt;
                return out;
            }

            void close() override {
                if (closed_.exchange(true)) return;
                ::SetEvent(close_event_);     // wake any blocked recv()/send()
                ::CancelIoEx(pipe_, nullptr); // cancel in-flight overlapped I/O
            }

        private:
            // Transfer exactly n bytes in or out, restarting on short transfers
            // and bailing out the moment close() fires. Returns false on EOF,
            // error, or close.
            bool io(bool reading, const void *buf, std::size_t n) {
                auto *p = static_cast<const unsigned char *>(buf);
                for (std::size_t done = 0; done < n;) {
                    const DWORD chunk = static_cast<DWORD>(
                        std::min<std::size_t>(n - done, 1u << 20));
                    DWORD moved = 0;
                    if (!io_once(reading, const_cast<unsigned char *>(p) + done,
                                 chunk, moved) ||
                        moved == 0) {
                        return false;
                    }
                    done += moved;
                }
                return true;
            }

            bool io_once(bool reading, void *buf, DWORD n, DWORD &moved) {
                if (closed_.load()) return false;
                const HANDLE event = reading ? read_event_ : write_event_;
                OVERLAPPED ov{};
                ov.hEvent = event;
                ::ResetEvent(event);

                const BOOL ok = reading
                    ? ::ReadFile(pipe_, buf, n, nullptr, &ov)
                    : ::WriteFile(pipe_, buf, n, nullptr, &ov);
                if (!ok) {
                    if (::GetLastError() != ERROR_IO_PENDING) return false;
                    const HANDLE waits[2] = {close_event_, event};
                    const DWORD w =
                        ::WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                    if (w == WAIT_OBJECT_0) { // close() fired
                        ::CancelIoEx(pipe_, &ov);
                        ::GetOverlappedResult(pipe_, &ov, &moved, TRUE);
                        return false;
                    }
                }
                DWORD transferred = 0;
                if (!::GetOverlappedResult(pipe_, &ov, &transferred, TRUE)) {
                    return false;
                }
                moved = transferred;
                return true;
            }

            HANDLE pipe_ = INVALID_HANDLE_VALUE;
            HANDLE read_event_ = nullptr;
            HANDLE write_event_ = nullptr;
            HANDLE close_event_ = nullptr;
            std::atomic<bool> closed_{false};
            std::mutex write_mu_;
        };

        class WinListener final : public Listener {
        public:
            WinListener(std::wstring name, HANDLE first)
                : name_(std::move(name)), first_pipe_(first) {
                stop_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                connect_event_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            }

            ~WinListener() override {
                if (first_pipe_ != INVALID_HANDLE_VALUE) ::CloseHandle(first_pipe_);
                if (stop_event_) ::CloseHandle(stop_event_);
                if (connect_event_) ::CloseHandle(connect_event_);
            }

            void serve(const ConnectionHandler &on_connection) override {
                running_ = true;
                HANDLE pipe = first_pipe_; // the instance bind_listener reserved
                first_pipe_ = INVALID_HANDLE_VALUE;

                while (running_) {
                    if (pipe == INVALID_HANDLE_VALUE) {
                        pipe = create_pipe_instance(name_, /*first=*/false);
                        if (pipe == INVALID_HANDLE_VALUE) break;
                    }

                    const int status = await_client(pipe);
                    if (status < 0) { // stop() fired
                        ::CloseHandle(pipe);
                        break;
                    }
                    if (status == 0) { // transient accept failure
                        ::CloseHandle(pipe);
                        pipe = INVALID_HANDLE_VALUE;
                        continue;
                    }

                    reap_finished();
                    auto connection = std::make_shared<WinConnection>(pipe);
                    pipe = INVALID_HANDLE_VALUE; // handed off to the connection
                    auto done = std::make_shared<std::atomic<bool>>(false);
                    {
                        std::lock_guard<std::mutex> lk(workers_mu_);
                        live_.push_back(connection);
                    }
                    workers_.push_back(Worker{
                        std::thread([this, connection, done, &on_connection] {
                            on_connection(connection, Peer{});
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
                ::SetEvent(stop_event_);
            }

        private:
            struct Worker {
                std::thread thread;
                std::shared_ptr<std::atomic<bool>> done;
            };

            // Block until a client connects on `pipe`. Returns 1 on connect, 0 on
            // a transient failure, -1 if stop() was called.
            int await_client(HANDLE pipe) {
                OVERLAPPED ov{};
                ov.hEvent = connect_event_;
                ::ResetEvent(connect_event_);

                if (::ConnectNamedPipe(pipe, &ov)) return 1;
                switch (::GetLastError()) {
                    case ERROR_PIPE_CONNECTED:
                        return 1; // client beat us to it
                    case ERROR_IO_PENDING:
                        break;
                    default:
                        return 0;
                }

                const HANDLE waits[2] = {stop_event_, connect_event_};
                if (::WaitForMultipleObjects(2, waits, FALSE, INFINITE) ==
                    WAIT_OBJECT_0) {
                    ::CancelIoEx(pipe, &ov);
                    return -1;
                }
                DWORD ignored = 0;
                return ::GetOverlappedResult(pipe, &ov, &ignored, TRUE) ? 1 : 0;
            }

            void forget(const std::shared_ptr<WinConnection> &connection) {
                std::lock_guard<std::mutex> lk(workers_mu_);
                std::erase(live_, connection);
            }

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

            std::wstring name_;
            HANDLE first_pipe_ = INVALID_HANDLE_VALUE;
            HANDLE stop_event_ = nullptr;
            HANDLE connect_event_ = nullptr;
            std::atomic<bool> running_{false};
            std::mutex workers_mu_;
            std::vector<std::shared_ptr<WinConnection>> live_;
            std::vector<Worker> workers_;
        };
    } // namespace

    std::unique_ptr<Listener> bind_listener(const fs::path &endpoint) {
        const std::wstring name = endpoint.wstring();
        // FILE_FLAG_FIRST_PIPE_INSTANCE is the single-instance guard: it fails if
        // any instance of this pipe already exists, i.e. a live daemon owns it.
        // Unlike a Unix socket there is no stale endpoint to reclaim — a crashed
        // daemon's pipe instances are torn down with the process.
        HANDLE first = create_pipe_instance(name, /*first=*/true);
        if (first == INVALID_HANDLE_VALUE) {
            const DWORD err = ::GetLastError();
            if (err == ERROR_ACCESS_DENIED || err == ERROR_PIPE_BUSY) {
                throw std::system_error(static_cast<int>(err),
                                        std::system_category(),
                                        "hestiad is already running");
            }
            throw std::system_error(static_cast<int>(err), std::system_category(),
                                    "CreateNamedPipe");
        }
        return std::make_unique<WinListener>(name, first);
    }

    std::shared_ptr<Connection> connect(const fs::path &endpoint) {
        const std::wstring name = endpoint.wstring();
        for (;;) {
            const HANDLE pipe = ::CreateFileW(
                name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                DWORD mode = PIPE_READMODE_BYTE;
                ::SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
                return std::make_shared<WinConnection>(pipe);
            }
            const DWORD err = ::GetLastError();
            // All instances busy: wait briefly for one to free up, then retry.
            if (err == ERROR_PIPE_BUSY && ::WaitNamedPipeW(name.c_str(), 2000)) {
                continue;
            }
            throw std::system_error(static_cast<int>(err), std::system_category(),
                                    "no daemon at " + endpoint.string());
        }
    }
}

#endif // _WIN32
