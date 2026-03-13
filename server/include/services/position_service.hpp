// services/position_service.hpp
// Handler for kGetPosition messages.
//
// Request JSON:  { "account": "ACC-001" }
// Response JSON: { "account": "ACC-001",
//                  "positions": [
//                    { "symbol": "AAPL", "qty": 500, "avg_cost": 178.30 },
//                    { "symbol": "MSFT", "qty": 200, "avg_cost": 310.10 }
//                  ],
//                  "cash": 12345.67 }

#pragma once

#include <string>

namespace hs::services {

std::string handle_get_position(const std::string& request_json);

} // namespace hs::services
