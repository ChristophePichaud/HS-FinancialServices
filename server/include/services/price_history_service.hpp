// services/price_history_service.hpp
// Handler for kGetPriceHistory messages.
//
// Request JSON:  { "symbol": "AAPL", "days": 30 }
// Response JSON: { "symbol": "AAPL",
//                  "history": [
//                    { "date": "2024-01-02", "open": 184.2, "high": 185.0,
//                      "low": 183.5, "close": 184.9, "volume": 52000000 },
//                    …
//                  ] }

#pragma once

#include <string>

namespace hs::services {

std::string handle_get_price_history(const std::string& request_json);

} // namespace hs::services
