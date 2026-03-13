// services/sql_service.hpp
// Handler for kSqlQuery messages.
//
// Request JSON:  { "sql": "SELECT symbol, price FROM quotes WHERE …" }
// Response JSON: { "rows": [ {"symbol":"AAPL","price":182.5}, … ], "affected": 0 }
//
// Production note: Replace the stub body with a real database client
// (e.g. libpq for PostgreSQL, ODBC, or an ORM).  Keep the call async or
// offload to a dedicated DB thread pool so the Asio I/O threads never block.

#pragma once

#include <string>

namespace hs::services {

/// Returns the response JSON for a SQL query request.
std::string handle_sql_query(const std::string& request_json);

} // namespace hs::services
