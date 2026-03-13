// server.cpp
// Implements the Server class: io_context, thread pool, acceptor.

#include "server.hpp"

#include <iostream>
#include <stdexcept>

namespace hs {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

// ---------------------------------------------------------------------------
Server::Server(const ServerConfig& cfg)
    : config_(cfg)
    , ioc_()
    , acceptor_(ioc_)
    , signals_(ioc_, SIGINT, SIGTERM)
{
    tcp::endpoint endpoint(asio::ip::make_address(config_.address), config_.port);

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));

    // SO_REUSEPORT lets multiple acceptors (one per thread) reduce lock contention
    // on Linux kernels >= 3.9.  We only use a single acceptor here; bind is safe.
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);

    // Graceful shutdown on SIGINT / SIGTERM.
    signals_.async_wait([this](boost::system::error_code /*ec*/, int /*sig*/) {
        stop();
    });

    unsigned n = config_.thread_count;
    if (n == 0) n = std::thread::hardware_concurrency();
    if (n == 0) n = 1;
    config_.thread_count = n;
}

Server::~Server() {
    stop();
}

// ---------------------------------------------------------------------------
void Server::run() {
    do_accept();

    unsigned n = config_.thread_count;
    threads_.reserve(n);

    std::cout << "[Server] Listening on " << config_.address
              << ':' << config_.port
              << " with " << n << " I/O thread(s)\n";

    // N-1 additional threads; the calling thread also runs ioc_.run() below,
    // giving a total of N threads running the io_context concurrently.
    for (unsigned i = 1; i < n; ++i) {
        threads_.emplace_back([this]() { ioc_.run(); });
    }
    ioc_.run(); // Blocks until ioc_ is stopped.

    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    std::cout << "[Server] Stopped. "
              << "Served " << total_connections_.load() << " connections, "
              << total_requests_.load() << " requests.\n";
}

// ---------------------------------------------------------------------------
void Server::stop() {
    // ioc_.stop() is thread-safe.
    ioc_.stop();
}

// ---------------------------------------------------------------------------
void Server::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket) {
            if (!acceptor_.is_open()) return;

            if (!ec) {
                if (active_connections_.load(std::memory_order_relaxed)
                        < config_.max_connections) {
                    total_connections_.fetch_add(1, std::memory_order_relaxed);
                    active_connections_.fetch_add(1, std::memory_order_relaxed);

                    // TCP_NODELAY: critical for low-latency banking traffic.
                    socket.set_option(tcp::no_delay(true));

                    std::size_t sid = next_session_id_.fetch_add(1, std::memory_order_relaxed);
                    auto session = std::make_shared<Session>(
                        std::move(socket),
                        router_,
                        [this]() {
                            active_connections_.fetch_sub(1, std::memory_order_relaxed);
                        },
                        sid);
                    session->start();
                } else {
                    // Gracefully reject: close the socket immediately.
                    boost::system::error_code ignored;
                    socket.close(ignored);
                }
            }
            // Continue accepting even after errors (e.g. EMFILE).
            do_accept();
        });
}

} // namespace hs
