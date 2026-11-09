#pragma once

#include <string>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#if defined(__linux__)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <sys/epoll.h>
#elif defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

namespace feeder {

/**
 * Socket Tuning Configuration.
 * Incorporates Linux kernel tuning parameters for high-throughput zero-drop multicast feeds.
 */
struct SocketConfig {
    std::string multicastIp   = "239.255.0.1";
    uint16_t    port          = 15000;
    std::string interfaceIp   = "0.0.0.0";
    
    // Linux Socket Tuning: 64 MB OS Receive Buffer to absorb order bursts without drops
    int receiveBufferSize     = 64 * 1024 * 1024; // 64 MB SO_RCVBUF
    
    // Low latency busy-polling in microseconds (Linux SO_BUSY_POLL)
    int busyPollUs            = 50; 
    
    bool enableMulticast      = true;
    bool enableNonBlocking    = true;
};

/**
 * Portable High-Performance UDP Socket with Linux Kernel Tuning & Windows Fallback.
 */
class UDPMulticastReceiver {
public:
    explicit UDPMulticastReceiver(const SocketConfig& config) 
        : m_config(config), m_sockFd(-1) {
        initNetwork();
    }

    ~UDPMulticastReceiver() {
        closeSocket();
    }

    void open() {
        m_sockFd = socket(AF_INET, SOCK_DGRAM, 0);
        if (m_sockFd < 0) {
            throw std::runtime_error("Failed to create UDP socket");
        }

        // 1. SO_REUSEADDR (allows multiple listeners on the same multicast group)
        int reuse = 1;
        setsockopt(m_sockFd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        // 2. Kernel Socket Buffer Tuning (SO_RCVBUF)
        if (setsockopt(m_sockFd, SOL_SOCKET, SO_RCVBUF, 
                       reinterpret_cast<const char*>(&m_config.receiveBufferSize), 
                       sizeof(m_config.receiveBufferSize)) < 0) {
            std::cerr << "[WARN] Could not set SO_RCVBUF to " << m_config.receiveBufferSize << " bytes.\n";
        } else {
            std::cout << "[INFO] Socket SO_RCVBUF successfully tuned to " << (m_config.receiveBufferSize / (1024 * 1024)) << " MB.\n";
        }

#if defined(__linux__)
        // 3. Linux Low-Latency Busy Polling (SO_BUSY_POLL)
        #ifdef SO_BUSY_POLL
        if (m_config.busyPollUs > 0) {
            setsockopt(m_sockFd, SOL_SOCKET, SO_BUSY_POLL, &m_config.busyPollUs, sizeof(m_config.busyPollUs));
            std::cout << "[INFO] Linux SO_BUSY_POLL enabled (" << m_config.busyPollUs << " us).\n";
        }
        #endif

        // 4. Non-blocking mode
        if (m_config.enableNonBlocking) {
            int flags = fcntl(m_sockFd, F_GETFL, 0);
            fcntl(m_sockFd, F_SETFL, flags | O_NONBLOCK);
        }
#elif defined(_WIN32)
        if (m_config.enableNonBlocking) {
            u_long mode = 1;
            ioctlsocket(m_sockFd, FIONBIO, &mode);
        }
#endif

        // 5. Bind socket
        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(m_config.port);
        localAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(m_sockFd, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) < 0) {
            closeSocket();
            throw std::runtime_error("Failed to bind UDP multicast socket to port " + std::to_string(m_config.port));
        }

        // 6. Join Multicast Group (IGMP)
        if (m_config.enableMulticast) {
            ip_mreq mreq{};
            mreq.imr_multiaddr.s_addr = inet_addr(m_config.multicastIp.c_str());
            mreq.imr_interface.s_addr = inet_addr(m_config.interfaceIp.c_str());

            if (setsockopt(m_sockFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
                           reinterpret_cast<const char*>(&mreq), sizeof(mreq)) < 0) {
                std::cerr << "[WARN] Failed to join multicast group " << m_config.multicastIp << " (Normal if no multicast routing).\n";
            } else {
                std::cout << "[INFO] Joined Multicast Group: " << m_config.multicastIp << ":" << m_config.port << "\n";
            }
        }
    }

    /**
     * Non-blocking receive datagram.
     * Returns bytes read, 0 if no data, or -1 on error.
     */
    int receive(uint8_t* buffer, size_t maxLen) noexcept {
        if (m_sockFd < 0) return -1;
#if defined(__linux__)
        ssize_t bytes = recv(m_sockFd, buffer, maxLen, 0);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        return static_cast<int>(bytes);
#elif defined(_WIN32)
        int bytes = recv(m_sockFd, reinterpret_cast<char*>(buffer), static_cast<int>(maxLen), 0);
        if (bytes < 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) return 0;
            return -1;
        }
        return bytes;
#endif
    }

#if defined(__linux__)
    [[nodiscard]] int getRawFd() const noexcept { return m_sockFd; }
#endif

private:
    SocketConfig m_config;
    int          m_sockFd;

    void initNetwork() {
#if defined(_WIN32)
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    void closeSocket() {
        if (m_sockFd >= 0) {
#if defined(__linux__)
            close(m_sockFd);
#elif defined(_WIN32)
            closesocket(m_sockFd);
            WSACleanup();
#endif
            m_sockFd = -1;
        }
    }
};

} // namespace feeder
