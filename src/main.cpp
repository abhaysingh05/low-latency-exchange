#include "exchange/matching_engine.hpp"
#include "exchange/tcp_server.hpp"

#include <thread>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <atomic>

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    g_running.store(false, std::memory_order_release);
}

int main(int argc, char* argv[]) {
    std::uint16_t port = 9000;
    if (argc > 1) port = static_cast<std::uint16_t>(std::atoi(argv[1]));

    // Register signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::printf("=== Low-Latency Exchange ===\n");
    std::printf("Starting on port %u...\n", port);

    exchange::MatchingEngine engine;

    // Engine thread: tight spin loop processing orders
    std::thread engine_thread([&engine]() {
        engine.run();
    });

    // Network thread: epoll event loop (runs on main thread)
    exchange::TcpServer server(port, engine.inbound(), engine.outbound());
    std::printf("Exchange listening on port %u\n", port);
    std::printf("Press Ctrl+C to shutdown\n\n");

    server.run();

    // Shutdown
    std::printf("\nShutting down...\n");
    engine.stop();
    server.stop();

    if (engine_thread.joinable()) {
        engine_thread.join();
    }

    std::printf("Exchange stopped.\n");
    return 0;
}
