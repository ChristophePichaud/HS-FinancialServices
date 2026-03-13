// sql_service.cpp
// Stub implementation of the SQL query handler.
// Replace with a real async database client in production.

#include "services/sql_service.hpp"

#include <string>
#include <sstream>
#include <chrono>

namespace hs::services {

// ---------------------------------------------------------------------------
// Minimal JSON helpers used in this file only.
// ---------------------------------------------------------------------------
namespace {

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Very simple key extraction: finds "key":"value" or "key":number in JSON.
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
std::string handle_sql_query(const std::string& request_json) {
    std::string sql = extract_string(request_json, "sql");
    if (sql.empty()) {
        return R"({"error":"missing 'sql' field"})";
    }

    // ── Stub: return synthetic result rows ──────────────────────────────
    // In production: submit `sql` to a connection-pool and return real rows.
    std::ostringstream oss;
    oss << R"({"rows":[)"
        << R"({"symbol":"AAPL","price":182.50,"volume":52000000},)"
        << R"({"symbol":"MSFT","price":310.10,"volume":28000000},)"
        << R"({"symbol":"GOOGL","price":140.75,"volume":18000000})"
        << R"(],"affected":0,"query":")" << json_escape(sql) << R"("})";
    return oss.str();
}

} // namespace hs::services
