#pragma once
#include "exchange/types.hpp"
#include "exchange/order_book.hpp"
#include "exchange/spsc_queue.hpp"
#include <atomic>

namespace exchange {

static constexpr std::size_t kQueueCapacity = 1u << 16;  // 65536 slots

// Matching engine: owns the OrderBook and SPSC queues.
// Runs a tight spin loop on the engine thread, processing inbound OrderRequests
// and producing outbound OrderResponses.
class MatchingEngine {
public:
    MatchingEngine();

    // Run the engine loop (blocking, call from engine thread)
    void run();

    // Signal the engine to stop
    void stop() noexcept;

    // Queue accessors - used by the network thread
    [[nodiscard]] SPSCQueue<OrderRequest, kQueueCapacity>&  inbound()  noexcept { return inbound_; }
    [[nodiscard]] SPSCQueue<OrderResponse, kQueueCapacity>& outbound() noexcept { return outbound_; }

private:
    void process(const OrderRequest& req);

    SPSCQueue<OrderRequest, kQueueCapacity>  inbound_;
    SPSCQueue<OrderResponse, kQueueCapacity> outbound_;

    OrderBook book_;
    std::atomic<bool> running_{false};
};

} // namespace exchange
