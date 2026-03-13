// client_session.hpp
// A single simulated client connection in the load generator.
//
// Each ClientSession:
//   1. Connects to the server.
//   2. Enters a request loop:
//      a. Choose a random request type from the configured scenario mix.
//      b. Record timestamp.
//      c. Send request (async_write header + payload).
//      d. Read response (async_read header + payload).
//      e. Record latency.
//      f. Loop until the test duration expires.
//   3. Closes the connection and signals completion.

#pragma once

#include "metrics_collector.hpp"

// Re-use the server's protocol header (same binary protocol).
// Note: this shared include deliberately couples both applications to the same
// protocol definition, which is the intended design—any protocol change is
// automatically reflected in both the server and the load generator.
#include "../../server/include/protocol.hpp"

#include <boost/asio.hpp>

#include <array>
#include <vector>
#include <memory>
#include <atomic>
#include <functional>
#include <string>

namespace hs::loadgen {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

/// Request type mix for the load generator.
struct ScenarioConfig {
    int weight_sql          {20};
    int weight_buy          {25};
    int weight_sell         {25};
    int weight_position     {20};
    int weight_price_history{10};
};

class ClientSession : public std::enable_shared_from_this<ClientSession> {
public:
    using DoneCallback = std::function<void()>;

    ClientSession(asio::io_context& ioc,
                  const std::string& host,
                  uint16_t port,
                  MetricsCollector& metrics,
                  std::atomic<bool>& stop_flag,
                  const ScenarioConfig& scenario,
                  DoneCallback on_done,
                  unsigned client_id);

    void start();

private:
    void do_connect();
    void do_send_request();
    void do_read_response_header();
    void do_read_response_body();
    void finish();

    // Build a random request based on scenario weights.
    hs::Message next_request();

    asio::io_context& ioc_;
    tcp::socket       socket_;
    tcp::resolver     resolver_;

    std::string           host_;
    uint16_t              port_;
    MetricsCollector&     metrics_;
    std::atomic<bool>&    stop_flag_;
    ScenarioConfig        scenario_;
    DoneCallback          on_done_;
    unsigned              client_id_;

    // I/O buffers.
    std::array<uint8_t, kHeaderSize> header_buf_{};
    std::vector<uint8_t>             body_buf_;
    std::vector<uint8_t>             write_buf_;

    hs::MessageHeader  resp_header_{};
    TimePoint          request_start_{};

    // Simple LCG for request selection (avoid std::mt19937 overhead).
    uint64_t rng_state_;

    uint64_t next_rand() noexcept {
        // LCG from Numerical Recipes
        rng_state_ = rng_state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        return rng_state_;
    }
};

} // namespace hs::loadgen
