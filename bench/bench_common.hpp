#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace bench {

struct Stats {
    double mean_ns;
    double p50_ns;
    double p99_ns;
    double ops_per_sec;
    std::size_t iterations;
};

inline Stats compute(std::vector<double>& latencies) {
    std::sort(latencies.begin(), latencies.end());
    const auto n = latencies.size();
    double sum = 0.0;
    for (auto v : latencies) sum += v;
    return {
        .mean_ns     = sum / static_cast<double>(n),
        .p50_ns      = latencies[n * 50 / 100],
        .p99_ns      = latencies[n * 99 / 100],
        .ops_per_sec = 1e9 * static_cast<double>(n) / sum,
        .iterations  = n,
    };
}

inline void print_header() {
    std::printf("%-32s  %10s  %10s  %10s  %10s  %12s\n",
                "Benchmark", "ns/op", "p50 (ns)", "p99 (ns)", "Mops/s", "iterations");
    std::printf("%-32s  %10s  %10s  %10s  %10s  %12s\n",
                "-------------------------------", "----------", "----------",
                "----------", "----------", "------------");
}

inline void report(std::string_view name, const Stats& s) {
    std::printf("%-32s  %10.1f  %10.0f  %10.0f  %10.2f  %12zu\n",
                name.data(), s.mean_ns, s.p50_ns, s.p99_ns,
                s.ops_per_sec / 1e6, s.iterations);
}

// Prevent compiler from optimizing away a value
template <typename T>
inline void do_not_optimize(T const& value) {
    asm volatile("" : : "r,m"(value) : "memory");
}

// Run a benchmark: measure each invocation individually for percentile stats
template <typename Fn>
Stats run(std::string_view name, std::size_t iterations, Fn&& fn) {
    std::vector<double> latencies;
    latencies.reserve(iterations);

    // Warmup: 10% of iterations
    const std::size_t warmup = iterations / 10;
    for (std::size_t i = 0; i < warmup; ++i) {
        fn(i);
    }

    for (std::size_t i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        fn(i);
        auto end = std::chrono::high_resolution_clock::now();
        latencies.push_back(
            std::chrono::duration<double, std::nano>(end - start).count());
    }

    auto stats = compute(latencies);
    report(name, stats);
    return stats;
}

// Run a bulk benchmark: time the entire batch, then divide
template <typename SetupFn, typename Fn>
Stats run_bulk(std::string_view name, std::size_t iterations,
               SetupFn&& setup, Fn&& fn) {
    setup();

    auto start = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        fn(i);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double total_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double mean = total_ns / static_cast<double>(iterations);

    Stats s{
        .mean_ns     = mean,
        .p50_ns      = mean,  // Bulk mode can't measure percentiles per-op
        .p99_ns      = mean,
        .ops_per_sec = 1e9 * static_cast<double>(iterations) / total_ns,
        .iterations  = iterations,
    };
    report(name, s);
    return s;
}

} // namespace bench
