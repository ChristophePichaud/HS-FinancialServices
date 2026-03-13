// position_service.cpp
// Stub implementation of the position query handler.

#include "services/position_service.hpp"

#include <string>
#include <sstream>

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

} // anonymous namespace

// ---------------------------------------------------------------------------
std::string handle_get_position(const std::string& request_json) {
    std::string account = extract_string(request_json, "account");
    if (account.empty()) account = "UNKNOWN";

    // ── Stub: return synthetic positions ────────────────────────────────
    std::ostringstream oss;
    oss << "{"
        << R"("account":")" << account << "\","
        << R"("positions":[)"
        << R"({"symbol":"AAPL","qty":500,"avg_cost":178.30,"current":182.50,"pnl":2100.00},)"
        << R"({"symbol":"MSFT","qty":200,"avg_cost":305.10,"current":310.10,"pnl":1000.00},)"
        << R"({"symbol":"GOOGL","qty":50,"avg_cost":138.20,"current":140.75,"pnl":127.50})"
        << "],"
        << R"("cash":12345.67)"
        << "}";
    return oss.str();
}

} // namespace hs::services
