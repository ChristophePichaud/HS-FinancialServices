// session.cpp
// Async read/write loop for one client connection.

#include "session.hpp"

#include <boost/asio.hpp>

#include <iostream>
#include <utility>

namespace hs {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

// ---------------------------------------------------------------------------
Session::Session(tcp::socket socket,
                 const ServiceRouter& router,
                 CloseCallback on_close,
                 std::size_t session_id)
    : socket_(std::move(socket))
    , router_(router)
    , on_close_(std::move(on_close))
    , session_id_(session_id)
{}

Session::~Session() {
    close();
}

// ---------------------------------------------------------------------------
void Session::start() {
    do_read_header();
}

void Session::stop() {
    // Post the close onto the socket's executor so it runs in an I/O thread.
    asio::post(socket_.get_executor(), [self = shared_from_this()]() {
        self->close();
    });
}

// ---------------------------------------------------------------------------
void Session::do_read_header() {
    auto self = shared_from_this();
    asio::async_read(
        socket_,
        asio::buffer(header_buf_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                // EOF or connection reset – normal teardown.
                close();
                return;
            }
            try {
                current_header_ = decode_header(header_buf_.data());
            } catch (const std::exception& ex) {
                // Malformed header – close connection.
                close();
                return;
            }
            if (current_header_.payload_length == 0) {
                // Zero-length payload – dispatch directly (e.g. Ping).
                body_buf_.clear();
                do_read_body(); // will skip directly to dispatch in next step
                return;
            }
            body_buf_.resize(current_header_.payload_length);
            do_read_body();
        });
}

// ---------------------------------------------------------------------------
void Session::do_read_body() {
    auto self = shared_from_this();

    auto dispatch_and_write = [this, self]() {
        Message req;
        req.header  = current_header_;
        req.payload = std::string(body_buf_.begin(), body_buf_.end());

        Message resp = router_.dispatch(req);
        do_write(std::move(resp));
    };

    if (current_header_.payload_length == 0) {
        dispatch_and_write();
        return;
    }

    asio::async_read(
        socket_,
        asio::buffer(body_buf_),
        [this, self, dispatch_and_write = std::move(dispatch_and_write)](
                boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                close();
                return;
            }
            dispatch_and_write();
        });
}

// ---------------------------------------------------------------------------
void Session::do_write(Message response) {
    // Serialise into write_buf_ (header + payload) to issue a single write.
    write_buf_.resize(kHeaderSize + response.payload.size());

    encode_header(response.header, write_buf_.data());
    std::memcpy(write_buf_.data() + kHeaderSize,
                response.payload.data(),
                response.payload.size());

    auto self = shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(write_buf_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                close();
                return;
            }
            // Loop back for the next request.
            do_read_header();
        });
}

// ---------------------------------------------------------------------------
void Session::close() {
    if (closed_) return;
    closed_ = true;
    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    if (on_close_) on_close_();
}

} // namespace hs
