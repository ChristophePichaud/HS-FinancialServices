// protocol.hpp
// Defines the wire protocol for the HS-FinancialServices banking server.
//
// Wire format (framed binary):
//   ┌──────────────────────────────────────────────────────────┐
//   │ Header (8 bytes)                                         │
//   │   payload_length : uint32_t  (network byte order)        │
//   │   message_type   : uint16_t  (network byte order)        │
//   │   flags          : uint8_t                               │
//   │   reserved       : uint8_t   (must be 0)                 │
//   ├──────────────────────────────────────────────────────────┤
//   │ Payload (payload_length bytes)                           │
//   │   UTF-8 JSON string                                      │
//   └──────────────────────────────────────────────────────────┘
//
// Adding a new banking service:
//   1. Add a new MessageType enum value below (client request < 0x8000,
//      server response ≥ 0x8000).
//   2. Define the JSON schema for request and response in a comment here.
//   3. Implement a handler in server/include/services/ and register it
//      in ServiceRouter.

#pragma once

#include <cstdint>
#include <string>
#include <stdexcept>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <arpa/inet.h>
#endif

namespace hs {

// ---------------------------------------------------------------------------
// Protocol constants
// ---------------------------------------------------------------------------

/// Maximum allowed payload size (16 MiB) – guards against memory exhaustion.
constexpr uint32_t kMaxPayloadSize = 16u * 1024u * 1024u;

/// Size of the fixed wire header in bytes.
constexpr std::size_t kHeaderSize = 8;

// ---------------------------------------------------------------------------
// Message types
// ---------------------------------------------------------------------------

/// Each 16-bit message type encodes direction:
///   client → server : high bit 0  (0x0000 – 0x7FFF)
///   server → client : high bit 1  (0x8000 – 0xFFFF)
enum class MessageType : uint16_t {
    // ── Client requests ─────────────────────────────────────────────────
    kSqlQuery        = 0x0001,   ///< { "sql": "SELECT …" }
    kBuyOrder        = 0x0002,   ///< { "symbol": "AAPL", "qty": 100, "price": 182.5 }
    kSellOrder       = 0x0003,   ///< { "symbol": "AAPL", "qty": 100, "price": 183.0 }
    kGetPosition     = 0x0004,   ///< { "account": "ACC-001" }
    kGetPriceHistory = 0x0005,   ///< { "symbol": "AAPL", "days": 30 }
    kPing            = 0x0006,   ///< { "seq": 42 }

    // ── Server responses ─────────────────────────────────────────────────
    kSqlResult       = 0x8001,   ///< { "rows": [...], "affected": 0 }
    kOrderAck        = 0x8002,   ///< { "order_id": "ORD-1234", "status": "FILLED" }
    kPositionResult  = 0x8004,   ///< { "account": "ACC-001", "positions": [...] }
    kPriceHistResult = 0x8005,   ///< { "symbol": "AAPL", "history": [...] }
    kPong            = 0x8006,   ///< { "seq": 42 }
    kError           = 0xFFFF,   ///< { "code": 42, "message": "…" }
};

// ---------------------------------------------------------------------------
// Wire header (unpacked view)
// ---------------------------------------------------------------------------

struct MessageHeader {
    uint32_t payload_length{0};
    MessageType message_type{MessageType::kPing};
    uint8_t  flags{0};
    uint8_t  reserved{0};
};

// ---------------------------------------------------------------------------
// Serialisation helpers
// ---------------------------------------------------------------------------

/// Serialise a MessageHeader into an 8-byte buffer (network byte order).
inline void encode_header(const MessageHeader& hdr, uint8_t out[kHeaderSize]) noexcept {
    uint32_t net_len  = htonl(hdr.payload_length);
    uint16_t net_type = htons(static_cast<uint16_t>(hdr.message_type));
    out[0] = static_cast<uint8_t>((net_len >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((net_len >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((net_len >>  8) & 0xFF);
    out[3] = static_cast<uint8_t>( net_len        & 0xFF);
    out[4] = static_cast<uint8_t>((net_type >> 8) & 0xFF);
    out[5] = static_cast<uint8_t>( net_type       & 0xFF);
    out[6] = hdr.flags;
    out[7] = 0; // reserved
}

/// Deserialise a MessageHeader from an 8-byte buffer (network byte order).
inline MessageHeader decode_header(const uint8_t in[kHeaderSize]) {
    MessageHeader hdr;
    uint32_t net_len = (static_cast<uint32_t>(in[0]) << 24)
                     | (static_cast<uint32_t>(in[1]) << 16)
                     | (static_cast<uint32_t>(in[2]) <<  8)
                     |  static_cast<uint32_t>(in[3]);
    uint16_t net_type = (static_cast<uint16_t>(in[4]) << 8)
                      |  static_cast<uint16_t>(in[5]);
    hdr.payload_length = ntohl(net_len);
    hdr.message_type   = static_cast<MessageType>(ntohs(net_type));
    hdr.flags          = in[6];
    hdr.reserved       = in[7];
    if (hdr.payload_length > kMaxPayloadSize) {
        throw std::runtime_error("payload_length exceeds kMaxPayloadSize");
    }
    return hdr;
}

// ---------------------------------------------------------------------------
// Message (header + payload)
// ---------------------------------------------------------------------------

struct Message {
    MessageHeader header;
    std::string   payload; ///< UTF-8 JSON

    /// Build a complete Message from parts.
    static Message make(MessageType type, std::string json_payload, uint8_t flags = 0) {
        Message m;
        m.header.message_type   = type;
        m.header.payload_length = static_cast<uint32_t>(json_payload.size());
        m.header.flags          = flags;
        m.payload               = std::move(json_payload);
        return m;
    }
};

} // namespace hs
