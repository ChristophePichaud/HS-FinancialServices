// services/trade_service.hpp
// Handlers for kBuyOrder and kSellOrder messages.
//
// Buy request JSON:  { "symbol": "AAPL", "qty": 100, "price": 182.5,
//                      "account": "ACC-001" }
// Sell request JSON: { "symbol": "AAPL", "qty": 100, "price": 183.0,
//                      "account": "ACC-001" }
// Response JSON (kOrderAck):
//   { "order_id": "ORD-00001", "symbol": "AAPL", "side": "BUY",
//     "qty": 100, "price": 182.5, "status": "FILLED" }

#pragma once

#include <string>

namespace hs::services {

std::string handle_buy_order (const std::string& request_json);
std::string handle_sell_order(const std::string& request_json);

} // namespace hs::services
