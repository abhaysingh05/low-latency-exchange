#include "exchange/tcp_server.hpp"

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>

namespace exchange {

TcpServer::TcpServer(std::uint16_t port,
                     SPSCQueue<OrderRequest, kQueueCapacity>& inbound,
                     SPSCQueue<OrderResponse, kQueueCapacity>& outbound)
    : port_(port), inbound_(inbound), outbound_(outbound) {

    // Create TCP socket
    server_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (server_fd_ < 0) {
        std::perror("socket");
        return;
    }

    // Allow address reuse
    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    // Disable Nagle's algorithm for low latency
    ::setsockopt(server_fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // Bind
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port_);

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // Listen
    if (::listen(server_fd_, SOMAXCONN) < 0) {
        std::perror("listen");
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // Create epoll instance
    epoll_fd_ = ::epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::perror("epoll_create1");
        ::close(server_fd_);
        server_fd_ = -1;
        return;
    }

    // Register server socket with epoll (edge-triggered)
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLET;
    ev.data.fd = server_fd_;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, server_fd_, &ev) < 0) {
        std::perror("epoll_ctl");
        ::close(epoll_fd_);
        ::close(server_fd_);
        epoll_fd_ = -1;
        server_fd_ = -1;
    }
}

TcpServer::~TcpServer() {
    // Close all client connections
    for (int i = 0; i < num_clients_; ++i) {
        if (client_fds_[i] >= 0) {
            ::close(client_fds_[i]);
        }
    }
    if (epoll_fd_ >= 0) ::close(epoll_fd_);
    if (server_fd_ >= 0) ::close(server_fd_);
}

void TcpServer::run() {
    if (server_fd_ < 0 || epoll_fd_ < 0) {
        std::fprintf(stderr, "TcpServer: not initialized\n");
        return;
    }

    running_.store(true, std::memory_order_release);
    constexpr int kMaxEvents = 64;
    epoll_event events[kMaxEvents];

    while (running_.load(std::memory_order_relaxed)) {
        // Short timeout so we can also drain outbound queue
        int nfds = ::epoll_wait(epoll_fd_, events, kMaxEvents, 1 /*ms*/);

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd_) {
                accept_connections();
            } else {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    remove_client(events[i].data.fd);
                } else if (events[i].events & EPOLLIN) {
                    handle_client_data(events[i].data.fd);
                }
            }
        }

        // Drain outbound responses and send to clients
        drain_outbound();
    }
}

void TcpServer::stop() noexcept {
    running_.store(false, std::memory_order_release);
}

void TcpServer::accept_connections() {
    // Edge-triggered: drain all pending connections
    while (true) {
        int client_fd = ::accept4(server_fd_, nullptr, nullptr, SOCK_NONBLOCK);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::perror("accept4");
            break;
        }

        // Disable Nagle for client too
        int opt = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

        // Register with epoll
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = client_fd;
        if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev) < 0) {
            std::perror("epoll_ctl client");
            ::close(client_fd);
            continue;
        }

        // Track client
        if (client_fd < kMaxClients) {
            clients_[client_fd].active = true;
            clients_[client_fd].bytes_read = 0;
        }
        if (num_clients_ < kMaxClients) {
            client_fds_[num_clients_++] = client_fd;
        }

        std::printf("Client connected: fd=%d\n", client_fd);
    }
}

void TcpServer::handle_client_data(int fd) {
    if (fd < 0 || fd >= kMaxClients || !clients_[fd].active) return;

    ClientState& client = clients_[fd];

    // Edge-triggered: drain all available data
    while (true) {
        std::size_t space = sizeof(client.buf) - client.bytes_read;
        if (space == 0) {
            // Buffer full but no complete message - protocol error
            std::fprintf(stderr, "Client fd=%d buffer overflow\n", fd);
            remove_client(fd);
            return;
        }

        ssize_t n = ::recv(fd, client.buf + client.bytes_read,
                           space, MSG_DONTWAIT);
        if (n > 0) {
            client.bytes_read += static_cast<std::size_t>(n);

            // Process complete messages
            while (client.bytes_read >= Protocol::kRequestSize) {
                auto req = Protocol::deserialize_request(
                    client.buf, client.bytes_read);
                if (req) {
                    (void)inbound_.try_push(*req);
                }

                // Shift remaining bytes forward
                std::size_t consumed = Protocol::kRequestSize;
                std::size_t remaining = client.bytes_read - consumed;
                if (remaining > 0) {
                    std::memmove(client.buf, client.buf + consumed, remaining);
                }
                client.bytes_read = remaining;
            }
        } else if (n == 0) {
            // Client disconnected
            remove_client(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::perror("recv");
            remove_client(fd);
            return;
        }
    }
}

void TcpServer::drain_outbound() {
    OrderResponse resp{};
    char buf[Protocol::kResponseSize];

    while (outbound_.try_pop(resp)) {
        std::size_t len = Protocol::serialize_response(resp, buf, sizeof(buf));
        // Broadcast to all connected clients
        // (In production, you'd route to the specific client)
        for (int i = 0; i < num_clients_; ++i) {
            int fd = client_fds_[i];
            if (fd >= 0 && fd < kMaxClients && clients_[fd].active) {
                ::send(fd, buf, len, MSG_NOSIGNAL | MSG_DONTWAIT);
            }
        }
    }
}

void TcpServer::remove_client(int fd) {
    if (fd < 0 || fd >= kMaxClients) return;

    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    clients_[fd].active = false;
    clients_[fd].bytes_read = 0;

    // Remove from client_fds_ array
    for (int i = 0; i < num_clients_; ++i) {
        if (client_fds_[i] == fd) {
            client_fds_[i] = client_fds_[num_clients_ - 1];
            --num_clients_;
            break;
        }
    }

    std::printf("Client disconnected: fd=%d\n", fd);
}

int TcpServer::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

} // namespace exchange
