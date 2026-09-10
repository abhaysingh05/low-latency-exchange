#include "exchange/types.hpp"
#include "exchange/spsc_queue.hpp"
#include "exchange/object_pool.hpp"
#include "exchange/order_book.hpp"
#include "exchange/protocol.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                   \
    static void test_##name();                                       \
    struct Register_##name {                                         \
        Register_##name() { run_test(#name, test_##name); }          \
    };                                                               \
    static void test_##name()

static void run_test(const char* name, void (*fn)()) {
    try {
        fn();
        std::printf("  [PASS] %s\n", name);
        ++tests_passed;
    } catch (...) {
        std::printf("  [FAIL] %s\n", name);
        ++tests_failed;
    }
}

#define ASSERT_TRUE(expr)                                            \
    do { if (!(expr)) {                                              \
        std::printf("    ASSERT_TRUE failed: %s (line %d)\n",        \
                    #expr, __LINE__);                                 \
        throw 0;                                                     \
    }} while(0)

#define ASSERT_EQ(a, b)                                              \
    do { if ((a) != (b)) {                                           \
        std::printf("    ASSERT_EQ failed: %s != %s (line %d)\n",    \
                    #a, #b, __LINE__);                                \
        throw 0;                                                     \
    }} while(0)

// ============================================================
// SPSC Queue Tests
// ============================================================

void test_spsc_basic() {
    exchange::SPSCQueue<int, 8> q;

    ASSERT_TRUE(q.empty());
    ASSERT_EQ(q.size(), std::size_t(0));

    ASSERT_TRUE(q.try_push(42));
    ASSERT_TRUE(!q.empty());
    ASSERT_EQ(q.size(), std::size_t(1));

    int val = 0;
    ASSERT_TRUE(q.try_pop(val));
    ASSERT_EQ(val, 42);
    ASSERT_TRUE(q.empty());

    // Pop from empty
    ASSERT_TRUE(!q.try_pop(val));
}

void test_spsc_full() {
    exchange::SPSCQueue<int, 4> q; // capacity = 3 (one slot reserved)

    ASSERT_TRUE(q.try_push(1));
    ASSERT_TRUE(q.try_push(2));
    ASSERT_TRUE(q.try_push(3));
    ASSERT_TRUE(!q.try_push(4)); // Full

    int val = 0;
    ASSERT_TRUE(q.try_pop(val)); ASSERT_EQ(val, 1);
    ASSERT_TRUE(q.try_push(4));  // Now there's room
    ASSERT_TRUE(q.try_pop(val)); ASSERT_EQ(val, 2);
    ASSERT_TRUE(q.try_pop(val)); ASSERT_EQ(val, 3);
    ASSERT_TRUE(q.try_pop(val)); ASSERT_EQ(val, 4);
    ASSERT_TRUE(q.empty());
}

void test_spsc_threaded() {
    exchange::SPSCQueue<std::uint64_t, 1024> q;
    constexpr std::size_t N = 100'000;

    std::thread producer([&q]() {
        for (std::uint64_t i = 0; i < N; ++i) {
            while (!q.try_push(i)) {} // spin
        }
    });

    std::uint64_t expected = 0;
    std::uint64_t val = 0;
    while (expected < N) {
        if (q.try_pop(val)) {
            assert(val == expected);
            ++expected;
        }
    }

    producer.join();
    ASSERT_EQ(expected, static_cast<std::uint64_t>(N));
}

// ============================================================
// Object Pool Tests
// ============================================================

void test_pool_basic() {
    exchange::ObjectPool<exchange::Order, 16> pool;

    ASSERT_EQ(pool.available(), std::size_t(16));
    ASSERT_EQ(pool.allocated(), std::size_t(0));

    auto* o1 = pool.create();
    ASSERT_TRUE(o1 != nullptr);
    ASSERT_EQ(pool.allocated(), std::size_t(1));
    ASSERT_EQ(pool.available(), std::size_t(15));

    auto* o2 = pool.create();
    ASSERT_TRUE(o2 != nullptr);
    ASSERT_TRUE(o1 != o2);

    pool.destroy(o1);
    ASSERT_EQ(pool.allocated(), std::size_t(1));

    pool.destroy(o2);
    ASSERT_EQ(pool.allocated(), std::size_t(0));
}

void test_pool_exhaustion() {
    exchange::ObjectPool<int, 4> pool;

    int* ptrs[4];
    for (int i = 0; i < 4; ++i) {
        ptrs[i] = pool.create(i);
        ASSERT_TRUE(ptrs[i] != nullptr);
    }

    // Pool should be exhausted
    ASSERT_TRUE(pool.create(999) == nullptr);
    ASSERT_EQ(pool.available(), std::size_t(0));

    // Free one and reallocate
    pool.destroy(ptrs[2]);
    ASSERT_EQ(pool.available(), std::size_t(1));

    int* reused = pool.create(42);
    ASSERT_TRUE(reused != nullptr);
    ASSERT_EQ(*reused, 42);

    // Clean up
    pool.destroy(ptrs[0]);
    pool.destroy(ptrs[1]);
    pool.destroy(ptrs[3]);
    pool.destroy(reused);
}

// ============================================================
// Order Book Tests
// ============================================================

// Trade collector callback for tests (matches raw function pointer signature)
static void collect_trades(const exchange::Trade& t, void* user_data) {
    auto* trades = static_cast<std::vector<exchange::Trade>*>(user_data);
    trades->push_back(t);
}

void test_orderbook_add() {
    exchange::OrderBook book;

    ASSERT_TRUE(book.add(1, exchange::Side::Buy,  50000, 100));
    ASSERT_TRUE(book.add(2, exchange::Side::Sell, 51000, 100));

    ASSERT_EQ(book.best_bid(), exchange::Price(50000));
    ASSERT_EQ(book.best_ask(), exchange::Price(51000));
    ASSERT_EQ(book.order_count(), std::size_t(2));
}

void test_orderbook_match() {
    std::vector<exchange::Trade> trades;
    exchange::OrderBook book(collect_trades, &trades);

    // Add sell order at 50000
    ASSERT_TRUE(book.add(1, exchange::Side::Sell, 50000, 100));
    ASSERT_EQ(book.best_ask(), exchange::Price(50000));

    // Add crossing buy order at 50000
    ASSERT_TRUE(book.add(2, exchange::Side::Buy, 50000, 100));

    // Should have matched
    ASSERT_EQ(trades.size(), std::size_t(1));
    ASSERT_EQ(trades[0].buyer_order_id, exchange::OrderId(2));
    ASSERT_EQ(trades[0].seller_order_id, exchange::OrderId(1));
    ASSERT_EQ(trades[0].price, exchange::Price(50000));
    ASSERT_EQ(trades[0].quantity, exchange::Qty(100));

    // Both orders should be fully filled (no resting orders)
    ASSERT_EQ(book.order_count(), std::size_t(0));
}

void test_orderbook_cancel() {
    exchange::OrderBook book;

    ASSERT_TRUE(book.add(1, exchange::Side::Buy, 50000, 100));
    ASSERT_EQ(book.order_count(), std::size_t(1));

    ASSERT_TRUE(book.cancel(1));
    ASSERT_EQ(book.order_count(), std::size_t(0));

    // Cancel non-existent
    ASSERT_TRUE(!book.cancel(999));
}

void test_price_time_priority() {
    std::vector<exchange::Trade> trades;
    exchange::OrderBook book(collect_trades, &trades);

    // Add multiple sell orders at the same price - order 1 first, then 2
    ASSERT_TRUE(book.add(1, exchange::Side::Sell, 50000, 50));
    ASSERT_TRUE(book.add(2, exchange::Side::Sell, 50000, 50));

    // Crossing buy should match order 1 first (FIFO)
    ASSERT_TRUE(book.add(3, exchange::Side::Buy, 50000, 50));

    ASSERT_EQ(trades.size(), std::size_t(1));
    ASSERT_EQ(trades[0].seller_order_id, exchange::OrderId(1)); // FIFO: order 1 first!
    ASSERT_EQ(trades[0].quantity, exchange::Qty(50));

    // Second buy should match order 2
    trades.clear();
    ASSERT_TRUE(book.add(4, exchange::Side::Buy, 50000, 50));
    ASSERT_EQ(trades.size(), std::size_t(1));
    ASSERT_EQ(trades[0].seller_order_id, exchange::OrderId(2));
}

void test_partial_fill() {
    std::vector<exchange::Trade> trades;
    exchange::OrderBook book(collect_trades, &trades);

    // Add small sell orders
    ASSERT_TRUE(book.add(1, exchange::Side::Sell, 50000, 30));
    ASSERT_TRUE(book.add(2, exchange::Side::Sell, 50000, 40));
    ASSERT_TRUE(book.add(3, exchange::Side::Sell, 50000, 50));

    // Large crossing buy - should partially fill
    ASSERT_TRUE(book.add(4, exchange::Side::Buy, 50000, 80));

    // Should fill orders 1 (30) and 2 (40) completely, then 10 from order 3
    ASSERT_EQ(trades.size(), std::size_t(3));
    ASSERT_EQ(trades[0].quantity, exchange::Qty(30)); // order 1 fully filled
    ASSERT_EQ(trades[1].quantity, exchange::Qty(40)); // order 2 fully filled
    ASSERT_EQ(trades[2].quantity, exchange::Qty(10)); // order 3 partial

    // Order 3 should still be resting with remaining qty of 40
    ASSERT_EQ(book.order_count(), std::size_t(1)); // only order 3 remains
}

void test_duplicate_id_rejected() {
    exchange::OrderBook book;

    ASSERT_TRUE(book.add(1, exchange::Side::Buy, 50000, 100));
    ASSERT_TRUE(!book.add(1, exchange::Side::Buy, 50000, 100)); // Duplicate!
    ASSERT_EQ(book.order_count(), std::size_t(1));
}

void test_best_price_tracking() {
    exchange::OrderBook book;

    // Add bids at different prices
    ASSERT_TRUE(book.add(1, exchange::Side::Buy, 49000, 100));
    ASSERT_TRUE(book.add(2, exchange::Side::Buy, 50000, 100));
    ASSERT_TRUE(book.add(3, exchange::Side::Buy, 48000, 100));

    ASSERT_EQ(book.best_bid(), exchange::Price(50000)); // Highest bid

    // Cancel the best bid
    ASSERT_TRUE(book.cancel(2));
    ASSERT_EQ(book.best_bid(), exchange::Price(49000)); // Next best

    // Add asks
    ASSERT_TRUE(book.add(10, exchange::Side::Sell, 52000, 100));
    ASSERT_TRUE(book.add(11, exchange::Side::Sell, 51000, 100));
    ASSERT_EQ(book.best_ask(), exchange::Price(51000)); // Lowest ask
}

// ============================================================
// Protocol Tests
// ============================================================

void test_protocol_roundtrip() {
    exchange::OrderRequest req{};
    req.type     = exchange::OrderType::Limit;
    req.side     = exchange::Side::Buy;
    req.id       = 12345;
    req.price    = 50000;
    req.quantity = 999;

    char buf[256];
    std::memcpy(buf, &req, sizeof(req));

    auto decoded = exchange::Protocol::deserialize_request(buf, sizeof(buf));
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->type, exchange::OrderType::Limit);
    ASSERT_EQ(decoded->side, exchange::Side::Buy);
    ASSERT_EQ(decoded->id, exchange::OrderId(12345));
    ASSERT_EQ(decoded->price, exchange::Price(50000));
    ASSERT_EQ(decoded->quantity, exchange::Qty(999));
}

void test_protocol_response() {
    exchange::OrderResponse resp{};
    resp.status     = exchange::OrderResponse::Status::Filled;
    resp.id         = 42;
    resp.price      = 50000;
    resp.filled_qty = 100;

    char buf[256];
    auto len = exchange::Protocol::serialize_response(resp, buf, sizeof(buf));
    ASSERT_EQ(len, exchange::Protocol::kResponseSize);

    exchange::OrderResponse decoded{};
    std::memcpy(&decoded, buf, len);
    ASSERT_EQ(decoded.status, exchange::OrderResponse::Status::Filled);
    ASSERT_EQ(decoded.id, exchange::OrderId(42));
}

void test_protocol_insufficient_data() {
    char buf[2] = {0, 0};
    auto result = exchange::Protocol::deserialize_request(buf, 2);
    ASSERT_TRUE(!result.has_value());
}

// ============================================================
// Main
// ============================================================

int main() {
    std::printf("\n=== Low-Latency Exchange Tests ===\n\n");

    // SPSC Queue
    std::printf("SPSC Queue:\n");
    run_test("spsc_basic", test_spsc_basic);
    run_test("spsc_full", test_spsc_full);
    run_test("spsc_threaded", test_spsc_threaded);

    // Object Pool
    std::printf("\nObject Pool:\n");
    run_test("pool_basic", test_pool_basic);
    run_test("pool_exhaustion", test_pool_exhaustion);

    // Order Book
    std::printf("\nOrder Book:\n");
    run_test("orderbook_add", test_orderbook_add);
    run_test("orderbook_match", test_orderbook_match);
    run_test("orderbook_cancel", test_orderbook_cancel);
    run_test("price_time_priority", test_price_time_priority);
    run_test("partial_fill", test_partial_fill);
    run_test("duplicate_id_rejected", test_duplicate_id_rejected);
    run_test("best_price_tracking", test_best_price_tracking);

    // Protocol
    std::printf("\nProtocol:\n");
    run_test("protocol_roundtrip", test_protocol_roundtrip);
    run_test("protocol_response", test_protocol_response);
    run_test("protocol_insufficient_data", test_protocol_insufficient_data);

    std::printf("\n=== Results: %d passed, %d failed ===\n\n",
                tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
