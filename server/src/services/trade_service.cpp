// trade_service.cpp
// Stub implementation of Buy/Sell order handlers.

#include "services/trade_service.hpp"

#include <string>
#include <sstream>
#include <atomic>

namespace hs::services {

namespace {

// Thread-safe monotonic order counter.
std::atomic<uint64_t> g_order_counter{1};

std::string extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

std::string extract_number(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return "0";
    pos += needle.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    auto end = pos;
    while (end < json.size() && (std::isdigit(json[end]) || json[end] == '.' || json[end] == '-')) ++end;
    return json.substr(pos, end - pos);
}

std::string build_order_ack(const std::string& request_json, const char* side) {
    std::string symbol  = extract_string(request_json, "symbol");
    std::string qty_s   = extract_number(request_json, "qty");
    std::string price_s = extract_number(request_json, "price");
    std::string account = extract_string(request_json, "account");

    if (symbol.empty()) symbol = "UNKNOWN";
    if (qty_s.empty())  qty_s  = "0";
    if (price_s.empty()) price_s = "0";

    uint64_t order_id = g_order_counter.fetch_add(1, std::memory_order_relaxed);

    std::ostringstream oss;
    oss << "{"
        << R"("order_id":"ORD-)" << order_id << "\","
        << R"("symbol":")"  << symbol  << "\","
        << R"("side":")"    << side    << "\","
        << R"("account":")" << (account.empty() ? "N/A" : account) << "\","
        << R"("qty":)"      << qty_s   << ","
        << R"("price":)"    << price_s << ","
        << R"("status":"FILLED")"
        << "}";
    return oss.str();
}

} // anonymous namespace

// ---------------------------------------------------------------------------
std::string handle_buy_order(const std::string& request_json) {
    return build_order_ack(request_json, "BUY");
}

std::string handle_sell_order(const std::string& request_json) {
    return build_order_ack(request_json, "SELL");
}

} // namespace hs::services
