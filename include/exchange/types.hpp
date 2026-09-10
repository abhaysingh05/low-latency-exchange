#pragma once
#include <cstdint>
#include <type_traits>

namespace exchange {

// Fixed-point price: 1 tick = 0.01
using OrderId = std::uint64_t;
using Price   = std::int32_t;    // Ticks - never floating-point
using Qty     = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t {
    Limit,
    Cancel
};

// Intrusive order node - lives in ObjectPool, linked into PriceLevel lists
struct PriceLevel; // Forward declaration

struct Order {
    OrderId   id{0};
    Price     price{0};
    Qty       quantity{0};
    Side      side{Side::Buy};
    OrderType type{OrderType::Limit};
    Order*    prev{nullptr};
    Order*    next{nullptr};
    PriceLevel* level{nullptr};
};

struct Trade {
    OrderId buyer_order_id;
    OrderId seller_order_id;
    Price   price;
    Qty     quantity;
};

// Wire protocol messages (trivially copyable POD)
struct OrderRequest {
    OrderType type;
    Side      side;
    std::uint8_t padding[2]{}; // explicit padding for deterministic layout
    Price     price;
    OrderId   id;
    Qty       quantity;
};

struct OrderResponse {
    enum class Status : std::uint8_t {
        Accepted,
        Filled,
        PartialFill,
        Cancelled,
        Rejected
    };
    Status    status;
    std::uint8_t padding[3]{};
    Price     price;
    OrderId   id;
    Qty       filled_qty;
};

static_assert(std::is_trivially_copyable_v<OrderRequest>,  "OrderRequest must be trivially copyable");
static_assert(std::is_trivially_copyable_v<OrderResponse>, "OrderResponse must be trivially copyable");

} // namespace exchange
