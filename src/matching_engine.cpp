#include "exchange/matching_engine.hpp"

namespace exchange {

// Free function matching the raw TradeCallback signature.
// user_data points to the MatchingEngine's outbound queue.
static void on_trade_callback(const Trade& trade, void* user_data) {
    auto* outbound = static_cast<SPSCQueue<OrderResponse, kQueueCapacity>*>(user_data);
    OrderResponse resp;
    resp.status     = OrderResponse::Status::Filled;
    resp.id         = trade.buyer_order_id;
    resp.price      = trade.price;
    resp.filled_qty = trade.quantity;
    while (!outbound->try_push(resp)) { __builtin_ia32_pause(); }
}

MatchingEngine::MatchingEngine()
    : book_(on_trade_callback, &outbound_) {}

void MatchingEngine::run() {
    running_.store(true, std::memory_order_release);

    OrderRequest req{};
    while (running_.load(std::memory_order_relaxed)) {
        if (inbound_.try_pop(req)) {
            process(req);
        } else {
            __builtin_ia32_pause();
        }
    }
}

void MatchingEngine::stop() noexcept {
    running_.store(false, std::memory_order_release);
}

void MatchingEngine::process(const OrderRequest& req) {
    OrderResponse resp;
    resp.id = req.id;

    switch (req.type) {
        case OrderType::Limit: {
            bool ok = book_.add(req.id, req.side, req.price, req.quantity);
            resp.status = ok ? OrderResponse::Status::Accepted
                             : OrderResponse::Status::Rejected;
            resp.price      = req.price;
            resp.filled_qty = 0;
            break;
        }
        case OrderType::Cancel: {
            bool ok = book_.cancel(req.id);
            resp.status = ok ? OrderResponse::Status::Cancelled
                             : OrderResponse::Status::Rejected;
            break;
        }
    }

    (void)outbound_.try_push(resp);
}

} // namespace exchange
