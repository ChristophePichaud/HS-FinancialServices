// service_router.hpp
// Maps incoming MessageType values to registered handler functions.
//
// To add a new banking service:
//   1. Define a new MessageType in protocol.hpp.
//   2. Write a handler function/lambda with the signature:
//        std::string handler(const std::string& json_payload)
//      It must return a JSON string that becomes the response payload.
//   3. Call router.register_handler(MessageType::kYourType, handler).
//
// The router is registered once at startup and then only read at runtime,
// so no locking is needed during request dispatch.

#pragma once

#include "protocol.hpp"

#include <functional>
#include <unordered_map>
#include <string>
#include <stdexcept>

namespace hs {

/// A handler takes a JSON payload and returns a JSON response payload.
using ServiceHandler = std::function<std::string(const std::string& payload)>;

class ServiceRouter {
public:
    /// Register a handler for the given message type.
    /// Throws std::runtime_error if a handler is already registered.
    void register_handler(MessageType type, ServiceHandler handler) {
        auto [it, ok] = handlers_.emplace(static_cast<uint16_t>(type), std::move(handler));
        if (!ok) {
            throw std::runtime_error("ServiceRouter: duplicate handler for message type");
        }
    }

    /// Dispatch an incoming message.  Returns the response Message.
    /// If no handler is registered, returns an error response.
    Message dispatch(const Message& request) const {
        auto it = handlers_.find(static_cast<uint16_t>(request.header.message_type));
        if (it == handlers_.end()) {
            std::string err = R"({"code":404,"message":"Unknown message type"})";
            return Message::make(MessageType::kError, std::move(err));
        }
        try {
            std::string response_payload = it->second(request.payload);
            // Derive the canonical response type (set high bit on request type)
            uint16_t req_type = static_cast<uint16_t>(request.header.message_type);
            MessageType resp_type = static_cast<MessageType>(req_type | 0x8000u);
            return Message::make(resp_type, std::move(response_payload));
        } catch (const std::exception& ex) {
            std::string err = R"({"code":500,"message":")" + std::string(ex.what()) + R"("})";
            return Message::make(MessageType::kError, std::move(err));
        }
    }

private:
    std::unordered_map<uint16_t, ServiceHandler> handlers_;
};

} // namespace hs
