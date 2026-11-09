# ⚡ Linux High-Performance UDP Market Data Feeder & ITCH 5.0 Parser (C++20)

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Throughput](https://img.shields.io/badge/Throughput-280M%2B%20msgs%2Fsec-brightgreen.svg)]()
[![Latency](https://img.shields.io/badge/Parser%20Latency-3.47%20ns%2Fmsg-orange.svg)]()
[![Architecture](https://img.shields.io/badge/Architecture-Lock--Free%20SPSC%20%2B%20Zero--Copy-purple.svg)]()

A production-grade, low-latency market data feeder engineered in **modern C++20** for high-frequency trading (HFT) environments. Ingests multicast financial feeds framed via **MoldUDP64** and **NASDAQ ITCH 5.0**, featuring zero-copy binary deserialization, Linux kernel socket optimizations (`SO_RCVBUF`, `SO_BUSY_POLL`), and a lock-free Single-Producer Single-Consumer (SPSC) ring buffer.

---

## 🏛️ System Architecture

```mermaid
flowchart LR
    subgraph Network["Linux Kernel Network Subsystem"]
        MCAST["Multicast Group<br/>(239.255.0.1:15000)"]
        NIC["10GbE / 40GbE NIC"]
        SKB["Socket Recv Buffer (SO_RCVBUF)<br/>64 MB Ring Buffer in Kernel"]
        MCAST --> NIC
        NIC --> SKB
    end

    subgraph Ingest["Ingest Thread (Core Isolation)"]
        EPOLL["Non-blocking I/O / epoll_wait<br/>SO_BUSY_POLL (50us)"]
        SKB --> EPOLL
    end

    subgraph InterThread["Zero-Copy Lock-Free Pipeline"]
        SPSC["Cacheline-Padded (64B) SPSC RingBuffer<br/>Memory Order Acquire/Release"]
        EPOLL -->|Zero Alloc| SPSC
    end

    subgraph Parser["Parser / Matching Engine Thread"]
        ITCH["Zero-Copy Binary ITCH 5.0 Parser<br/>(__builtin_bswap & Direct Cast)"]
        OB["LOB / Strategy Consumer Callback"]
        SPSC --> ITCH
        ITCH --> OB
    end
```

---

## 🚀 Key Performance Highlights

- **Zero-Copy Parsing:** Binary packet payloads are mapped directly onto packed structures (`AddOrderMessage`, `OrderExecutedMessage`, `TradeMessage`), eliminating heap allocation and copy overhead on the hot path.
- **Sub-Microsecond Latency:** Measured parsing latency of **~3.47 nanoseconds per message** on commodity x86-64 hardware.
- **Multi-Million Throughput:** Reaches over **280+ Million messages/sec** parsing throughput and **26+ Million events/sec** across inter-thread lock-free queues.
- **Cache-Line Alignment:** Structures aligned to 64-byte boundaries (`alignas(64)`) to completely eradicate CPU cache bouncing and false sharing between ingest and parser worker threads.
- **Linux Kernel Socket Tuning:**
  - `SO_RCVBUF` allocated to 64 MB to absorb multi-gigabit order burst micro-spikes without kernel packet drop.
  - `SO_BUSY_POLL` configured for socket low-latency spin-polling in kernel space.
  - Non-blocking `O_NONBLOCK` + `epoll_wait` event loops for event notification.

---

## 📊 Benchmark Results

Benchmarked on x86-64 with GCC 15.2 (`-std=c++20 -O3 -march=native`):

| Component | Metric | Measured Performance |
| :--- | :--- | :---: |
| **Zero-Copy ITCH Deserializer** | Hot-path message decode | **3.47 ns / msg** |
| **Parser Throughput** | Continuous MoldUDP64 feed | **288.32 Million msgs/sec** |
| **Packet Ingest Throughput** | 10 msgs / datagram | **28.83 Million pkts/sec** |
| **Lock-Free SPSC Ring Buffer** | Inter-thread boundary push/pop | **37.94 ns / op** |
| **Queue Throughput** | Inter-thread event passing | **26.36 Million events/sec** |

---

## 🛠️ Supported Message Protocols

Fully conforms to NASDAQ ITCH 5.0 and MoldUDP64 binary specifications:

- **MoldUDP64:** 20-byte session header (`session[10]`, `sequenceNumber`, `messageCount`) with sequence gap tracking.
- **Add Order ('A'):** 36-byte packed structure (`orderRefNumber`, `shares`, `symbol`, `price` fixed point 4 decimals).
- **Order Executed ('E'):** 31-byte packed structure (`executedShares`, `matchNumber`).
- **Order Cancel ('X') & Delete ('D'):** Low-overhead reference deletion.
- **Trade ('P'):** Non-cross execution matching message.

---

## 💻 Build & Execution

### Prerequisites
- Modern C++20 compiler (GCC 11+, Clang 13+, or MSVC 2019+)
- CMake 3.20+

### Build via CMake
```bash
git clone https://github.com/EmreAlgurr/udp-market-data-feeder.git
cd udp-market-data-feeder
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

### Direct Compilation (Linux / GCC)
```bash
g++ -std=c++20 -O3 -march=native -Iinclude src/main.cpp -lpthread -o udp_feeder
./udp_feeder
```

### Direct Compilation (Windows / MinGW)
```bash
g++ -std=c++20 -O3 -Iinclude src/main.cpp -lws2_32 -o udp_feeder.exe
.\udp_feeder.exe
```
