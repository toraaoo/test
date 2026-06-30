#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// The IpcTransport seam: moves length-prefixed message frames over a per-user
// channel (Unix domain socket on POSIX, named pipe on Windows). Payload bytes are
// opaque; the channel/JSON envelope lives one layer up.
namespace hestia::ipc {
    // A full-duplex frame pipe. Both ends — the daemon (one per accepted
    // connection) and every client — hold one. The connection is multiplexed:
    // many frames may travel each way and correlation (request id, events) is the
    // job of the layer above. send() is safe to call concurrently with recv() and
    // with other send()s; recv() is meant to be driven by a single reader.
    class Connection {
    public:
        virtual ~Connection() = default;

        // Send one frame. Returns false if the peer is gone.
        virtual bool send(std::string_view frame) = 0;

        // Block for the next inbound frame; nullopt once the peer closes or the
        // connection is close()d from another thread.
        virtual std::optional<std::string> recv() = 0;

        // Shut the connection down, unblocking a recv() in progress. Idempotent.
        virtual void close() = 0;
    };

    // The verified identity of an accepted connection's peer. The seam where a
    // future remote transport carries a token/cert instead of a local uid.
    struct Peer {
        bool local = true;
        std::uint32_t uid = 0;
    };

    // Invoked once per accepted connection, on its own thread, with the verified
    // peer identity. Returns when the connection should be torn down.
    using ConnectionHandler =
        std::function<void(std::shared_ptr<Connection>, const Peer &)>;

    // Server side, owned by the daemon. One instance per endpoint.
    class Listener {
    public:
        virtual ~Listener() = default;

        // Block, accepting connections and handing each to `on_connection` on a
        // dedicated thread, until stop() is called. Joins all connection threads
        // before returning.
        virtual void serve(const ConnectionHandler &on_connection) = 0;

        // Unblock serve() and release the endpoint. Async-signal-safe, so it can
        // be called directly from a SIGINT/SIGTERM handler.
        virtual void stop() = 0;
    };

    // Bind a listener to `endpoint`, failing fast if another daemon already owns
    // it (single-instance guard — a stale socket from a crashed daemon is
    // reclaimed; a live one is refused). Throws std::system_error on failure.
    std::unique_ptr<Listener> bind_listener(const std::filesystem::path &endpoint);

    // Open a persistent connection to a daemon listening on `endpoint`. Throws
    // std::system_error if no daemon is reachable.
    std::shared_ptr<Connection> connect(const std::filesystem::path &endpoint);
}
