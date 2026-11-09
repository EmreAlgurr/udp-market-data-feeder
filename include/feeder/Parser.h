#pragma once

#include "ITCHMessages.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>

namespace feeder {

/**
 * Zero-Copy ITCH 5.0 / MoldUDP64 Packet Parser.
 * Designed for sub-microsecond binary streaming ingestion with zero memory allocation.
 */
class ITCHParser {
public:
    using EventCallback = std::function<void(const MarketEvent&)>;

    struct ParseStats {
        uint64_t totalPackets{0};
        uint64_t totalMessages{0};
        uint64_t addOrders{0};
        uint64_t executedOrders{0};
        uint64_t cancelOrders{0};
        uint64_t deleteOrders{0};
        uint64_t trades{0};
        uint64_t unknownMessages{0};
        uint64_t sequenceGaps{0};
        uint64_t expectedSeqNum{1};
    };

    explicit ITCHParser(EventCallback callback = nullptr) 
        : m_callback(std::move(callback)) {}

    void setCallback(EventCallback cb) {
        m_callback = std::move(cb);
    }

    /**
     * Parses an incoming UDP datagram in-place with zero memory allocation.
     * @param buffer Raw network buffer received from UDP socket
     * @param length Number of bytes received
     */
    inline void parsePacket(const uint8_t* buffer, size_t length) noexcept {
        if (__builtin_expect(length < sizeof(MoldHeader), 0)) {
            return;
        }

        m_stats.totalPackets++;

        // 1. Zero-copy read of MoldUDP64 Header
        const auto* moldHeader = reinterpret_cast<const MoldHeader*>(buffer);
        const uint64_t seqNum = netToHost64(moldHeader->sequenceNumber);
        const uint16_t msgCount = netToHost16(moldHeader->messageCount);

        // Sequence gap detection
        if (__builtin_expect(m_stats.expectedSeqNum > 1 && seqNum != m_stats.expectedSeqNum, 0)) {
            m_stats.sequenceGaps++;
        }
        m_stats.expectedSeqNum = seqNum + msgCount;

        // 2. Iterate message blocks
        size_t offset = sizeof(MoldHeader);
        for (uint16_t i = 0; i < msgCount && offset + sizeof(MessageBlockHeader) <= length; ++i) {
            const auto* blockHeader = reinterpret_cast<const MessageBlockHeader*>(buffer + offset);
            const uint16_t msgLen = netToHost16(blockHeader->messageLength);
            offset += sizeof(MessageBlockHeader);

            if (__builtin_expect(offset + msgLen > length, 0)) {
                break; // Corrupted or truncated packet boundary
            }

            parseSingleMessage(buffer + offset, msgLen, seqNum + i);
            offset += msgLen;
        }
    }

    [[nodiscard]] const ParseStats& getStats() const noexcept {
        return m_stats;
    }

    void resetStats() noexcept {
        m_stats = ParseStats{};
    }

private:
    EventCallback m_callback;
    ParseStats    m_stats;

    inline void parseSingleMessage(const uint8_t* msgBytes, uint16_t len, uint64_t seq) noexcept {
        if (__builtin_expect(len == 0, 0)) return;

        m_stats.totalMessages++;
        const auto type = static_cast<MessageType>(msgBytes[0]);

        MarketEvent event{};
        event.type = type;
        event.sequenceNum = seq;

        switch (type) {
            case MessageType::AddOrder: {
                if (__builtin_expect(len >= sizeof(AddOrderMessage), 1)) {
                    m_stats.addOrders++;
                    const auto* msg = reinterpret_cast<const AddOrderMessage*>(msgBytes);
                    event.side = msg->buySellIndicator;
                    event.shares = netToHost32(msg->shares);
                    event.price = static_cast<double>(netToHost32(msg->price)) / 10000.0;
                    event.orderId = netToHost64(msg->orderRefNumber);
                    event.timestampNs = parse48BitTimestamp(msg->timestamp);
                    std::memcpy(event.symbol, msg->stock, 8);

                    if (m_callback) m_callback(event);
                }
                break;
            }

            case MessageType::OrderExecuted: {
                if (__builtin_expect(len >= sizeof(OrderExecutedMessage), 1)) {
                    m_stats.executedOrders++;
                    const auto* msg = reinterpret_cast<const OrderExecutedMessage*>(msgBytes);
                    event.orderId = netToHost64(msg->orderRefNumber);
                    event.shares = netToHost32(msg->executedShares);
                    event.timestampNs = parse48BitTimestamp(msg->timestamp);

                    if (m_callback) m_callback(event);
                }
                break;
            }

            case MessageType::OrderCancel: {
                if (__builtin_expect(len >= sizeof(OrderCancelMessage), 1)) {
                    m_stats.cancelOrders++;
                    const auto* msg = reinterpret_cast<const OrderCancelMessage*>(msgBytes);
                    event.orderId = netToHost64(msg->orderRefNumber);
                    event.shares = netToHost32(msg->canceledShares);
                    event.timestampNs = parse48BitTimestamp(msg->timestamp);

                    if (m_callback) m_callback(event);
                }
                break;
            }

            case MessageType::OrderDelete: {
                if (__builtin_expect(len >= sizeof(OrderDeleteMessage), 1)) {
                    m_stats.deleteOrders++;
                    const auto* msg = reinterpret_cast<const OrderDeleteMessage*>(msgBytes);
                    event.orderId = netToHost64(msg->orderRefNumber);
                    event.timestampNs = parse48BitTimestamp(msg->timestamp);

                    if (m_callback) m_callback(event);
                }
                break;
            }

            case MessageType::Trade: {
                if (__builtin_expect(len >= sizeof(TradeMessage), 1)) {
                    m_stats.trades++;
                    const auto* msg = reinterpret_cast<const TradeMessage*>(msgBytes);
                    event.side = msg->buySellIndicator;
                    event.shares = netToHost32(msg->shares);
                    event.price = static_cast<double>(netToHost32(msg->price)) / 10000.0;
                    event.orderId = netToHost64(msg->orderRefNumber);
                    event.timestampNs = parse48BitTimestamp(msg->timestamp);
                    std::memcpy(event.symbol, msg->stock, 8);

                    if (m_callback) m_callback(event);
                }
                break;
            }

            default:
                m_stats.unknownMessages++;
                break;
        }
    }
};

} // namespace feeder
