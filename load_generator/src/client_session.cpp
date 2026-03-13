// client_session.cpp
// Implements one simulated banking client.

#include "client_session.hpp"

#include <sstream>
#include <iostream>
#include <cstring>

namespace hs::loadgen {

namespace asio = boost::asio;
using tcp      = asio::ip::tcp;

// ---------------------------------------------------------------------------
ClientSession::ClientSession(asio::io_context& ioc,
                             const std::string& host,
                             uint16_t port,
                             MetricsCollector& metrics,
                             std::atomic<bool>& stop_flag,
                             const ScenarioConfig& scenario,
                             DoneCallback on_done,
                             unsigned client_id)
    : ioc_(ioc)
    , socket_(ioc)
    , resolver_(ioc)
    , host_(host)
    , port_(port)
    , metrics_(metrics)
    , stop_flag_(stop_flag)
    , scenario_(scenario)
    , on_done_(std::move(on_done))
    , client_id_(client_id)
    , rng_state_(static_cast<uint64_t>(client_id) * 2654435761ULL + 1)
{}

// ---------------------------------------------------------------------------
void ClientSession::start() {
    do_connect();
}

// ---------------------------------------------------------------------------
void ClientSession::do_connect() {
    auto self = shared_from_this();
    resolver_.async_resolve(
        host_, std::to_string(port_),
        [this, self](boost::system::error_code ec,
                     tcp::resolver::results_type results) {
            if (ec) { metrics_.record_error(); finish(); return; }
            asio::async_connect(
                socket_, results,
                [this, self](boost::system::error_code ec2,
                             const tcp::endpoint& /*ep*/) {
                    if (ec2) { metrics_.record_error(); finish(); return; }
                    // TCP_NODELAY for low-latency.
                    socket_.set_option(tcp::no_delay(true));
                    do_send_request();
                });
        });
}

// ---------------------------------------------------------------------------
hs::Message ClientSession::next_request() {
    int total = scenario_.weight_sql
              + scenario_.weight_buy
              + scenario_.weight_sell
              + scenario_.weight_position
              + scenario_.weight_price_history;
    if (total <= 0) total = 1;

    int pick = static_cast<int>(next_rand() % static_cast<uint64_t>(total));

    auto symbols = std::array<const char*, 5>{"AAPL","MSFT","GOOGL","AMZN","TSLA"};
    const char* sym = symbols[next_rand() % symbols.size()];

    int threshold = scenario_.weight_sql;
    if (pick < threshold) {
        std::string payload = R"({"sql":"SELECT symbol, price FROM quotes WHERE symbol=')"
                            + std::string(sym) + R"('"})";
        return hs::Message::make(hs::MessageType::kSqlQuery, std::move(payload));
    }
    threshold += scenario_.weight_buy;
    if (pick < threshold) {
        std::ostringstream oss;
        int qty = 10 + static_cast<int>(next_rand() % 491);
        double price = 100.0 + static_cast<double>(next_rand() % 20000) / 100.0;
        oss << R"({"symbol":")" << sym << R"(","qty":)" << qty
            << R"(,"price":)" << price << R"(,"account":"ACC-001"})";
        return hs::Message::make(hs::MessageType::kBuyOrder, oss.str());
    }
    threshold += scenario_.weight_sell;
    if (pick < threshold) {
        std::ostringstream oss;
        int qty = 10 + static_cast<int>(next_rand() % 491);
        double price = 100.0 + static_cast<double>(next_rand() % 20000) / 100.0;
        oss << R"({"symbol":")" << sym << R"(","qty":)" << qty
            << R"(,"price":)" << price << R"(,"account":"ACC-001"})";
        return hs::Message::make(hs::MessageType::kSellOrder, oss.str());
    }
    threshold += scenario_.weight_position;
    if (pick < threshold) {
        return hs::Message::make(hs::MessageType::kGetPosition, R"({"account":"ACC-001"})");
    }
    // price history
    int days = 7 + static_cast<int>(next_rand() % 24);
    std::string payload = R"({"symbol":")" + std::string(sym)
                        + R"(","days":)" + std::to_string(days) + "}";
    return hs::Message::make(hs::MessageType::kGetPriceHistory, std::move(payload));
}

// ---------------------------------------------------------------------------
void ClientSession::do_send_request() {
    if (stop_flag_.load(std::memory_order_relaxed)) { finish(); return; }

    hs::Message req = next_request();

    // Serialise into write_buf_.
    write_buf_.resize(hs::kHeaderSize + req.payload.size());
    hs::encode_header(req.header, write_buf_.data());
    std::memcpy(write_buf_.data() + hs::kHeaderSize,
                req.payload.data(), req.payload.size());

    request_start_ = Clock::now();

    auto self = shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(write_buf_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) { metrics_.record_error(); finish(); return; }
            do_read_response_header();
        });
}

// ---------------------------------------------------------------------------
void ClientSession::do_read_response_header() {
    auto self = shared_from_this();
    asio::async_read(
        socket_,
        asio::buffer(header_buf_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) { metrics_.record_error(); finish(); return; }
            try {
                resp_header_ = hs::decode_header(header_buf_.data());
            } catch (...) {
                metrics_.record_error();
                finish();
                return;
            }
            if (resp_header_.payload_length == 0) {
                // Empty response – record and loop.
                auto us = std::chrono::duration_cast<DurationUs>(Clock::now() - request_start_);
                metrics_.record_success(us);
                do_send_request();
                return;
            }
            body_buf_.resize(resp_header_.payload_length);
            do_read_response_body();
        });
}

// ---------------------------------------------------------------------------
void ClientSession::do_read_response_body() {
    auto self = shared_from_this();
    asio::async_read(
        socket_,
        asio::buffer(body_buf_),
        [this, self](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) { metrics_.record_error(); finish(); return; }
            auto us = std::chrono::duration_cast<DurationUs>(Clock::now() - request_start_);
            metrics_.record_success(us);
            // Immediately send next request.
            do_send_request();
        });
}

// ---------------------------------------------------------------------------
void ClientSession::finish() {
    boost::system::error_code ec;
    socket_.shutdown(tcp::socket::shutdown_both, ec);
    socket_.close(ec);
    if (on_done_) on_done_();
}

} // namespace hs::loadgen
