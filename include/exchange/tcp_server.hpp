#pragma once
#include "exchange/types.hpp"
#include "exchange/protocol.hpp"
#include "exchange/spsc_queue.hpp"
#include "exchange/matching_engine.hpp"
#include <cstdint>
#include <atomic>

namespace exchange {

// Edge-triggered epoll TCP server.
// Accepts connections, reads binary OrderRequest messages,
// pushes them into the inbound SPSC queue, and drains
// outbound OrderResponses back to clients.
class TcpServer {
public:
    explicit TcpServer(std::uint16_t port,
                       SPSCQueue<OrderRequest, kQueueCapacity>& inbound,
                       SPSCQueue<OrderResponse, kQueueCapacity>& outbound);
    ~TcpServer();

    // Non-copyable, non-movable
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    // Blocking event loop - call from network thread
    void run();
    void stop() noexcept;

private:
    void accept_connections();
    void handle_client_data(int fd);
    void drain_outbound();
    void remove_client(int fd);

    static int set_nonblocking(int fd);

    int server_fd_{-1};
    int epoll_fd_{-1};
    std::uint16_t port_;
    std::atomic<bool> running_{false};

    SPSCQueue<OrderRequest, kQueueCapacity>&  inbound_;
    SPSCQueue<OrderResponse, kQueueCapacity>& outbound_;

    // Per-client state for partial message reads
    static constexpr int kMaxClients = 256;
    struct ClientState {
        char buf[512]{};
        std::size_t bytes_read{0};
        bool active{false};
    };
    ClientState clients_[kMaxClients]{};

    // Track connected client fds for broadcasting responses
    int client_fds_[kMaxClients]{};
    int num_clients_{0};
};

} // namespace exchange
