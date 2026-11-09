#pragma once

#include "ITCHMessages.h"
#include "RingBuffer.h"
#include "Parser.h"
#include "SocketConfig.h"
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>

namespace feeder {

/**
 * End-to-end Low-Latency Market Data Feeder.
 * Decouples network I/O from message decoding via a lock-free SPSC Ring Buffer.
 */
class MarketDataFeeder {
public:
    struct PacketSlot {
        size_t  length{0};
        uint8_t data[2048]; // Maximum MTU safe buffer
    };

    explicit MarketDataFeeder(const SocketConfig& config)
        : m_config(config), m_receiver(config), m_running(false) {}

    ~MarketDataFeeder() {
        stop();
    }

    void setEventCallback(ITCHParser::EventCallback cb) {
        m_parser.setCallback(std::move(cb));
    }

    void start() {
        m_running.store(true, std::memory_order_release);
        m_receiver.open();

        // Worker Thread: Ingest Thread (Socket -> RingBuffer)
        m_ingestThread = std::thread([this]() {
            ingestLoop();
        });

        // Worker Thread: Parser Thread (RingBuffer -> ITCH Parser -> Callback)
        m_parseThread = std::thread([this]() {
            parseLoop();
        });
    }

    void stop() {
        if (m_running.load(std::memory_order_acquire)) {
            m_running.store(false, std::memory_order_release);
            if (m_ingestThread.joinable()) m_ingestThread.join();
            if (m_parseThread.joinable()) m_parseThread.join();
        }
    }

    [[nodiscard]] const ITCHParser::ParseStats& getStats() const noexcept {
        return m_parser.getStats();
    }

    [[nodiscard]] size_t getRingBufferDepth() const noexcept {
        return m_packetRing.size();
    }

    [[nodiscard]] uint64_t getDroppedPackets() const noexcept {
        return m_droppedPackets.load(std::memory_order_relaxed);
    }

private:
    SocketConfig                     m_config;
    UDPMulticastReceiver             m_receiver;
    ITCHParser                       m_parser;
    SPSCRingBuffer<PacketSlot, 32768> m_packetRing;

    std::atomic<bool>     m_running;
    std::atomic<uint64_t> m_droppedPackets{0};
    std::thread           m_ingestThread;
    std::thread           m_parseThread;

    void ingestLoop() {
        PacketSlot slot;
        while (m_running.load(std::memory_order_relaxed)) {
            int bytes = m_receiver.receive(slot.data, sizeof(slot.data));
            if (bytes > 0) {
                slot.length = static_cast<size_t>(bytes);
                if (!m_packetRing.push(slot)) {
                    m_droppedPackets.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                // Yield or spin depending on thread policy
                std::this_thread::yield();
            }
        }
    }

    void parseLoop() {
        PacketSlot slot;
        while (m_running.load(std::memory_order_relaxed) || !m_packetRing.empty()) {
            if (m_packetRing.pop(slot)) {
                m_parser.parsePacket(slot.data, slot.length);
            } else {
                std::this_thread::yield();
            }
        }
    }
};

} // namespace feeder
