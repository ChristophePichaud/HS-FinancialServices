// session.hpp
// Represents one client TCP connection.
//
// Lifecycle:
//   1. Server accepts a socket and constructs a Session (shared_ptr).
//   2. Session::start() kicks off the first async_read for the header.
//   3. On header receipt → async_read body → dispatch → async_write response
//      → loop back to step 2.
//   4. On any error or graceful close, the Session destructs and releases
//      its socket.  The shared_ptr ensures the object stays alive for the
//      duration of every async operation.

#pragma once

#include "protocol.hpp"
#include "service_router.hpp"

#include <boost/asio.hpp>

#include <array>
#include <memory>
#include <atomic>
#include <functional>

namespace hs {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session> {
public:
    /// Callback invoked when this session is closed (used by Server to
    /// decrement the active-connection counter).
    using CloseCallback = std::function<void()>;

    Session(tcp::socket socket,
            const ServiceRouter& router,
            CloseCallback on_close,
            std::size_t session_id);

    ~Session();

    /// Begin the async read/dispatch loop.
    void start();

    /// Request a graceful shutdown of this session.
    void stop();

    std::size_t id() const noexcept { return session_id_; }

private:
    void do_read_header();
    void do_read_body();
    void do_write(Message response);
    void close();

    tcp::socket           socket_;
    const ServiceRouter&  router_;
    CloseCallback         on_close_;
    std::size_t           session_id_;

    // Flat header buffer – reused each request (no allocation on hot path).
    std::array<uint8_t, kHeaderSize> header_buf_{};

    // Payload buffer – resized to the declared payload_length.
    std::vector<uint8_t>  body_buf_;

    // Write buffer – holds the serialised response; kept alive until
    // async_write completes.
    std::vector<uint8_t>  write_buf_;

    MessageHeader current_header_{};
    bool          closed_{false};
};

} // namespace hs
