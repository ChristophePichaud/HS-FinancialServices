// server.hpp
// Top-level server: owns the io_context, the thread pool, the acceptor, and
// the ServiceRouter.
//
// Threading model:
//   - One io_context shared by N worker threads (N = hardware_concurrency).
//   - All async handlers execute in those threads without a mutex, except
//     where shared mutable state is explicitly noted.
//   - Each Session lives entirely within the io_context strand implied by
//     the fact that Boost.Asio's composed operations on a single socket are
//     always serialised (no explicit strand needed per-session for single
//     async_read / async_write at a time).

#pragma once

#include "service_router.hpp"
#include "session.hpp"

#include <boost/asio.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>

namespace hs {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

struct ServerConfig {
    std::string   address{"0.0.0.0"};
    uint16_t      port{9000};
    unsigned      thread_count{0}; ///< 0 → use hardware_concurrency()
    std::size_t   max_connections{50000};
};

class Server {
public:
    explicit Server(const ServerConfig& config);
    ~Server();

    /// Register all banking service handlers.
    ServiceRouter& router() noexcept { return router_; }

    /// Start accepting connections and block until stop() is called.
    void run();

    /// Initiate graceful shutdown (safe to call from a signal handler via
    /// asio::signal_set).
    void stop();

    // Metrics (read by any thread with relaxed ordering for display only).
    uint64_t total_connections()  const noexcept { return total_connections_.load(std::memory_order_relaxed); }
    uint64_t active_connections() const noexcept { return active_connections_.load(std::memory_order_relaxed); }
    uint64_t total_requests()     const noexcept { return total_requests_.load(std::memory_order_relaxed); }

    /// Called by Session when it dispatches a request.
    void on_request() noexcept { total_requests_.fetch_add(1, std::memory_order_relaxed); }

private:
    void do_accept();

    ServerConfig              config_;
    asio::io_context          ioc_;
    tcp::acceptor             acceptor_;
    asio::signal_set          signals_;
    ServiceRouter             router_;

    std::vector<std::thread>  threads_;

    std::atomic<uint64_t>     total_connections_{0};
    std::atomic<uint64_t>     active_connections_{0};
    std::atomic<uint64_t>     total_requests_{0};
    std::atomic<std::size_t>  next_session_id_{1};
};

} // namespace hs
