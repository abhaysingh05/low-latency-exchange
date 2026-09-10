#include "bench_common.hpp"
#include "exchange/order_book.hpp"
#include <cstdio>
#include <random>

static constexpr std::size_t kIterations = 1'000'000;

void bench_insert() {
    exchange::OrderBook book;
    std::mt19937 rng(42);
    std::uniform_int_distribution<exchange::Price> price_dist(1000, 99000);

    std::vector<exchange::Price> prices(kIterations);
    for (std::size_t i = 0; i < kIterations; ++i) {
        auto side = (i % 2 == 0) ? exchange::Side::Buy : exchange::Side::Sell;
        auto price = price_dist(rng);
        if (side == exchange::Side::Buy)  price = std::min(price, exchange::Price(49000));
        if (side == exchange::Side::Sell) price = std::max(price, exchange::Price(51000));
        prices[i] = price;
    }

    bench::run("order_book_insert", kIterations, [&](std::size_t i) {
        auto id    = static_cast<exchange::OrderId>(i + 1);
        auto side  = (i % 2 == 0) ? exchange::Side::Buy : exchange::Side::Sell;
        book.add(id, side, prices[i], 100);
        bench::do_not_optimize(book.best_bid());
    });
}

void bench_cancel() {
    exchange::OrderBook book;
    std::mt19937 rng(42);
    std::uniform_int_distribution<exchange::Price> price_dist(1000, 49000);

    // Pre-populate orders
    for (std::size_t i = 0; i < kIterations; ++i) {
        auto id    = static_cast<exchange::OrderId>(i + 1);
        auto side  = (i % 2 == 0) ? exchange::Side::Buy : exchange::Side::Sell;
        auto price = price_dist(rng);
        if (side == exchange::Side::Sell) price += 51000;
        book.add(id, side, price, 100);
    }

    // Shuffle cancel order for realistic access patterns
    std::vector<exchange::OrderId> ids(kIterations);
    for (std::size_t i = 0; i < kIterations; ++i) ids[i] = static_cast<exchange::OrderId>(i + 1);
    // std::shuffle(ids.begin(), ids.end(), rng); // Sequential access for max throughput

    bench::run("order_book_cancel", kIterations, [&](std::size_t i) {
        if (i + 16 < kIterations) {
            book.prefetch_order_map(ids[i + 16]);
        }
        book.cancel(ids[i]);
        bench::do_not_optimize(book.best_ask());
    });
}

void bench_match() {
    // Pre-populate one side, then match against it
    std::size_t match_iters = 500'000;

    exchange::OrderBook book;

    // Add resting sell orders at various prices
    for (std::size_t i = 0; i < match_iters; ++i) {
        auto id    = static_cast<exchange::OrderId>(i + 1);
        auto price = static_cast<exchange::Price>(50000 + (i % 1000));
        book.add(id, exchange::Side::Sell, price, 1);
    }

    std::size_t trade_count = 0;
    exchange::OrderBook match_book(
        [](const exchange::Trade&, void* ud) {
            ++(*static_cast<std::size_t*>(ud));
        }, &trade_count);

    // Re-populate for the actual timed benchmark
    for (std::size_t i = 0; i < match_iters; ++i) {
        auto id    = static_cast<exchange::OrderId>(i + 1);
        auto price = static_cast<exchange::Price>(50000 + (i % 1000));
        match_book.add(id, exchange::Side::Sell, price, 1);
    }

    // Now send crossing buy orders
    bench::run("order_book_match", match_iters, [&](std::size_t i) {
        auto id    = static_cast<exchange::OrderId>(match_iters + i + 1);
        auto price = static_cast<exchange::Price>(50000 + (i % 1000));
        match_book.add(id, exchange::Side::Buy, price, 1);
        bench::do_not_optimize(trade_count);
    });

    std::printf("  -> Total trades executed: %zu\n", trade_count);
}

int main() {
    std::printf("\n=== Order Book Benchmarks ===\n");
    std::printf("CPU: run with 'taskset -c 0' for best results\n\n");

    bench::print_header();

    bench_insert();
    bench_cancel();
    bench_match();

    std::printf("\nDone.\n");
    return 0;
}
