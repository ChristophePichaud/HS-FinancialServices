// price_history_service.cpp
// Stub implementation of the price history handler.

#include "services/price_history_service.hpp"

#include <string>
#include <sstream>
#include <cmath>
#include <ctime>

namespace hs::services {

namespace {

std::string extract_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return {};
    return json.substr(pos, end - pos);
}

int extract_int(const std::string& json, const std::string& key, int def = 30) {
    std::string needle = "\"" + key + "\":";
    auto pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos += needle.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    int val = 0;
    bool found = false;
    while (pos < json.size() && std::isdigit(json[pos])) {
        val = val * 10 + (json[pos] - '0');
        ++pos;
        found = true;
    }
    return found ? val : def;
}

// Format a date string YYYY-MM-DD from time_t.
std::string format_date(std::time_t t) {
    char buf[16];
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);  // MSVC: gmtime_s(struct tm*, const time_t*)
#else
    gmtime_r(&t, &tm);  // POSIX: gmtime_r(const time_t*, struct tm*)
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
std::string handle_get_price_history(const std::string& request_json) {
    std::string symbol = extract_string(request_json, "symbol");
    if (symbol.empty()) symbol = "AAPL";
    int days = extract_int(request_json, "days", 30);
    if (days <= 0 || days > 365) days = 30;

    // Synthetic OHLCV data starting from `days` ago up to today.
    std::time_t now = std::time(nullptr);
    const int kSecsPerDay = 86400;

    double base_price = 182.5;
    // Simple deterministic price walk seeded by symbol name.
    for (char c : symbol) base_price += static_cast<int>(c) * 0.01;

    std::ostringstream oss;
    oss << "{"
        << R"("symbol":")" << symbol << "\","
        << R"("history":[)";

    for (int i = days - 1; i >= 0; --i) {
        std::time_t day_t = now - static_cast<std::time_t>(i) * kSecsPerDay;
        // Pseudo-random walk using day index.
        double delta  = std::sin(static_cast<double>(i) * 0.42) * 2.0;
        double open   = base_price + delta;
        double close_ = open + std::cos(static_cast<double>(i) * 0.31) * 1.5;
        double high   = std::max(open, close_) + std::abs(std::sin(i * 0.7)) * 0.8;
        double low    = std::min(open, close_) - std::abs(std::cos(i * 0.7)) * 0.8;
        long long vol = 20000000LL + static_cast<long long>(std::abs(std::sin(i) * 15000000.0));

        if (i < days - 1) oss << ',';
        oss << R"({"date":")" << format_date(day_t)
            << R"(","open":)"    << open
            << R"(,"high":)"     << high
            << R"(,"low":)"      << low
            << R"(,"close":)"    << close_
            << R"(,"volume":)"   << vol
            << '}';
    }
    oss << "]}";
    return oss.str();
}

} // namespace hs::services
