#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <cstring>
#include <array>

#if defined(_MSC_VER)
    #define PACKED_STRUCT(name) __pragma(pack(push, 1)) struct name __pragma(pack(pop))
#elif defined(__GNUC__) || defined(__clang__)
    #define PACKED_STRUCT(name) struct __attribute__((packed)) name
#else
    #define PACKED_STRUCT(name) struct name
#endif

namespace feeder {

#pragma pack(push, 1)

/**
 * MoldUDP64 Protocol Header (Standard Multicast framing for ITCH)
 * 20 bytes total.
 */
struct MoldHeader {
    char session[10];           // Session ID
    uint64_t sequenceNumber;    // First message sequence number
    uint16_t messageCount;      // Number of discrete ITCH messages in payload
};

/**
 * Common payload message block header in MoldUDP64.
 * Each ITCH message is prefixed with its 2-byte length.
 */
struct MessageBlockHeader {
    uint16_t messageLength;     // Length of the following ITCH message
};

/**
 * ITCH 5.0 Message Types
 */
enum class MessageType : uint8_t {
    SystemEvent         = 'S',
    StockDirectory      = 'R',
    AddOrder            = 'A',
    AddOrderMPID        = 'F',
    OrderExecuted       = 'E',
    OrderExecutedPrice  = 'C',
    OrderCancel         = 'X',
    OrderDelete         = 'D',
    OrderReplace        = 'U',
    Trade               = 'P',
    CrossTrade          = 'Q',
    BrokenTrade         = 'B',
    NOII                = 'I'
};

/**
 * 'A' - Add Order Message (36 bytes)
 */
struct AddOrderMessage {
    uint8_t  type;              // 'A'
    uint16_t stockLocate;       // Locate code identifying the security
    uint16_t trackingNumber;    // Nasdaq internal tracking
    uint8_t  timestamp[6];      // Nanoseconds since midnight (48-bit int)
    uint64_t orderRefNumber;    // Unique order identifier
    char     buySellIndicator;  // 'B' = Buy, 'S' = Sell
    uint32_t shares;            // Total shares
    char     stock[8];          // Stock ticker (space padded)
    uint32_t price;             // Price fixed point (4 decimal places: / 10000.0)
};

/**
 * 'E' - Order Executed Message (31 bytes)
 */
struct OrderExecutedMessage {
    uint8_t  type;              // 'E'
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t  timestamp[6];
    uint64_t orderRefNumber;
    uint32_t executedShares;
    uint64_t matchNumber;
};

/**
 * 'X' - Order Cancel Message (23 bytes)
 */
struct OrderCancelMessage {
    uint8_t  type;              // 'X'
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t  timestamp[6];
    uint64_t orderRefNumber;
    uint32_t canceledShares;
};

/**
 * 'D' - Order Delete Message (19 bytes)
 */
struct OrderDeleteMessage {
    uint8_t  type;              // 'D'
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t  timestamp[6];
    uint64_t orderRefNumber;
};

/**
 * 'P' - Trade Message (Non-Cross) (44 bytes)
 */
struct TradeMessage {
    uint8_t  type;              // 'P'
    uint16_t stockLocate;
    uint16_t trackingNumber;
    uint8_t  timestamp[6];
    uint64_t orderRefNumber;
    char     buySellIndicator;
    uint32_t shares;
    char     stock[8];
    uint32_t price;
    uint64_t matchNumber;
};

#pragma pack(pop)

/**
 * High-Level Parsed Event Representation
 * Unified internal representation optimized for L1 cache alignment.
 */
struct alignas(64) MarketEvent {
    MessageType type;
    char        side;           // 'B' or 'S'
    uint32_t    shares;
    double      price;          // Floating point representation for processing
    uint64_t    orderId;
    uint64_t    timestampNs;    // Converted 64-bit nanosecond timestamp
    char        symbol[8];      // Cleaned ticker
    uint64_t    sequenceNum;
};

// Endian conversion helper (Mold/ITCH is Big-Endian on network wire)
inline uint16_t netToHost16(uint16_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap16(val);
#elif defined(_MSC_VER)
    return _byteswap_ushort(val);
#else
    return ((val & 0xFF00) >> 8) | ((val & 0x00FF) << 8);
#endif
}

inline uint32_t netToHost32(uint32_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#elif defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8)  |
           ((val & 0x0000FF00) << 8)  |
           ((val & 0x000000FF) << 24);
#endif
}

inline uint64_t netToHost64(uint64_t val) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#elif defined(_MSC_VER)
    return _byteswap_uint64(val);
#else
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
#endif
}

inline uint64_t parse48BitTimestamp(const uint8_t bytes[6]) noexcept {
    return (static_cast<uint64_t>(bytes[0]) << 40) |
           (static_cast<uint64_t>(bytes[1]) << 32) |
           (static_cast<uint64_t>(bytes[2]) << 24) |
           (static_cast<uint64_t>(bytes[3]) << 16) |
           (static_cast<uint64_t>(bytes[4]) << 8)  |
           (static_cast<uint64_t>(bytes[5]));
}

} // namespace feeder
