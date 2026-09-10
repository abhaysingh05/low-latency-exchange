#include "bench_common.hpp"
#include "exchange/matching_engine.hpp"
#include <thread>
#include <cstdio>
#include <chrono>
#include <random>

#include <pthread.h>
#include <sched.h>

std::vector<int> get_available_cpus() {
    std::vector<int> cpus;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &cpuset) == 0) {
        for (int i = 0; i < CPU_SETSIZE; ++i) {
            if (CPU_ISSET(i, &cpuset)) cpus.push_back(i);
        }
    }
    return cpus;
}

void pin_current_thread(int cpu) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
}

void bench_e2e_throughput(int engine_cpu) {
    static constexpr std::size_t kIterations = 500'000;

    exchange::MatchingEngine engine;

    std::thread engine_thread([&engine, engine_cpu]() {
        if (engine_cpu != -1) pin_current_thread(engine_cpu);
        engine.run();
    });

    auto& inbound  = engine.inbound();
    auto& outbound = engine.outbound();

    // Pre-build requests: alternating buys and sells that don't cross
    std::vector<exchange::OrderRequest> requests(kIterations);
    for (std::size_t i = 0; i < kIterations; ++i) {
        requests[i].type     = exchange::OrderType::Limit;
        requests[i].side     = (i % 2 == 0) ? exchange::Side::Buy : exchange::Side::Sell;
        requests[i].id       = static_cast<exchange::OrderId>(i + 1);
        requests[i].quantity = 100;
        // Spread prices so orders don't cross
        if (requests[i].side == exchange::Side::Buy)
            requests[i].price = static_cast<exchange::Price>(40000 + (i % 1000));
        else
            requests[i].price = static_cast<exchange::Price>(60000 + (i % 1000));
    }

    // Warmup
    for (std::size_t i = 0; i < 1000; ++i) {
        while (!inbound.try_push(requests[i % kIterations])) {}
    }
    exchange::OrderResponse resp{};
    std::size_t drained = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (drained < 1000 && std::chrono::steady_clock::now() < deadline) {
        if (outbound.try_pop(resp)) ++drained;
    }

    // --- Timed run: interleave send and receive to avoid queue overflow ---
    std::size_t sent = 0;
    std::size_t received = 0;

    auto start = std::chrono::high_resolution_clock::now();

    while (received < kIterations) {
        if (sent < kIterations && inbound.try_push(requests[sent])) {
            ++sent;
        }
        if (outbound.try_pop(resp)) {
            ++received;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double mean_ns = total_ns / static_cast<double>(kIterations);
    double mops = static_cast<double>(kIterations) / (total_ns / 1e9) / 1e6;

    std::printf("%-32s  %10.1f  %10s  %10s  %10.2f  %12zu\n",
                "e2e_throughput (order-to-ack)",
                mean_ns, "N/A", "N/A", mops, kIterations);

    engine.stop();
    engine_thread.join();
}

void bench_e2e_matching_throughput(int engine_cpu) {
    // Sell-then-buy pairs that match immediately
    static constexpr std::size_t kPairs = 200'000;
    static constexpr std::size_t kTotal = kPairs * 2;

    exchange::MatchingEngine engine;

    std::thread engine_thread([&engine, engine_cpu]() {
        if (engine_cpu != -1) pin_current_thread(engine_cpu);
        engine.run();
    });

    auto& inbound  = engine.inbound();
    auto& outbound = engine.outbound();

    // Build crossing order pairs
    std::vector<exchange::OrderRequest> requests(kTotal);
    for (std::size_t i = 0; i < kPairs; ++i) {
        // Sell order
        requests[i * 2].type     = exchange::OrderType::Limit;
        requests[i * 2].side     = exchange::Side::Sell;
        requests[i * 2].id       = static_cast<exchange::OrderId>(i * 2 + 1);
        requests[i * 2].price    = 50000;
        requests[i * 2].quantity = 1;

        // Matching buy order
        requests[i * 2 + 1].type     = exchange::OrderType::Limit;
        requests[i * 2 + 1].side     = exchange::Side::Buy;
        requests[i * 2 + 1].id       = static_cast<exchange::OrderId>(i * 2 + 2);
        requests[i * 2 + 1].price    = 50000;
        requests[i * 2 + 1].quantity = 1;
    }

    // Each order generates an acceptance response.
    // Each match also generates a trade fill response from the callback.
    // So total responses = kTotal (acceptances) + kPairs (trade fills) = 3 * kPairs
    std::size_t expected_responses = kTotal + kPairs;

    std::size_t sent = 0;
    std::size_t received = 0;
    exchange::OrderResponse resp{};

    auto start = std::chrono::high_resolution_clock::now();

    while (received < expected_responses) {
        if (sent < kTotal && inbound.try_push(requests[sent])) {
            ++sent;
        }
        if (outbound.try_pop(resp)) {
            ++received;
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double mean_ns = total_ns / static_cast<double>(kPairs);
    double mops = static_cast<double>(kPairs) / (total_ns / 1e9) / 1e6;

    std::printf("%-32s  %10.1f  %10s  %10s  %10.2f  %12zu\n",
                "e2e_matching (order-to-trade)",
                mean_ns, "N/A", "N/A", mops, kPairs);

    engine.stop();
    engine_thread.join();
}

int main() {
    std::printf("\n=== End-to-End Throughput Benchmarks ===\n");
    std::printf("Measures full order-to-response pipeline through SPSC queues\n\n");

    int engine_cpu = -1;
    auto cpus = get_available_cpus();
    if (cpus.size() >= 2) {
        pin_current_thread(cpus.front());
        engine_cpu = cpus.back();
    }

    bench::print_header();

    bench_e2e_throughput(engine_cpu);
    bench_e2e_matching_throughput(engine_cpu);

    std::printf("\nDone.\n");
    return 0;
}
