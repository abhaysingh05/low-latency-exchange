#include "exchange/order_book.hpp"
#include <algorithm>

namespace exchange {



bool OrderBook::add(OrderId id, Side side, Price price, Qty qty) {
    if (price <= 0 || qty == 0) return false;
    if (order_map_.contains(id)) return false;

    Order* order = pool_.create();
    if (!order) return false;

    order->id       = id;
    order->side     = side;
    order->price    = price;
    order->quantity = qty;
    
    match_order(order);

    if (order->quantity > 0) {
        order_map_.insert(id, order);
        insert_at_level(order);
    } else {
        pool_.destroy(order);
    }

    return true;
}

bool OrderBook::cancel(OrderId id) {
    Order* const* order_ptr = order_map_.find(id);
    if (!order_ptr || !(*order_ptr)) return false;

    Order* order = *order_ptr;
    remove_from_level(order);

    order_map_.erase(id);
    pool_.destroy(order);

    return true;
}

void OrderBook::match_order(Order* incoming) {
    if (incoming->side == Side::Buy) {
        while (incoming->quantity > 0 && best_ask_ != kInvalidAsk && incoming->price >= best_ask_) {
            PriceLevel* level = ask_levels_.get(best_ask_);
            if (!level) break;

            Order* resting = level->head;
            while (resting && incoming->quantity > 0) {
                Qty fill_qty = std::min(incoming->quantity, resting->quantity);

                incoming->quantity -= fill_qty;
                resting->quantity  -= fill_qty;
                level->total_qty   -= fill_qty;

                if (on_trade_) {
                    Trade t{incoming->id, resting->id, resting->price, fill_qty};
                    on_trade_(t, user_data_);
                }

                Order* next = resting->next;
                if (resting->quantity == 0) {
                    remove_from_level(resting);
                    order_map_.erase(resting->id);
                    pool_.destroy(resting);
                }
                resting = next;
            }
        }
    } else {
        while (incoming->quantity > 0 && best_bid_ != kInvalidBid && incoming->price <= best_bid_) {
            PriceLevel* level = bid_levels_.get(best_bid_);
            if (!level) break;

            Order* resting = level->head;
            while (resting && incoming->quantity > 0) {
                Qty fill_qty = std::min(incoming->quantity, resting->quantity);

                incoming->quantity -= fill_qty;
                resting->quantity  -= fill_qty;
                level->total_qty   -= fill_qty;

                if (on_trade_) {
                    Trade t{resting->id, incoming->id, resting->price, fill_qty};
                    on_trade_(t, user_data_);
                }

                Order* next = resting->next;
                if (resting->quantity == 0) {
                    remove_from_level(resting);
                    order_map_.erase(resting->id);
                    pool_.destroy(resting);
                }
                resting = next;
            }
        }
    }
}

void OrderBook::insert_at_level(Order* order) {
    auto& levels = (order->side == Side::Buy) ? bid_levels_ : ask_levels_;

    bool created = false;
    PriceLevel* level = levels.get_or_create(order->price, created);

    if (created) {
        if (order->side == Side::Buy) {
            best_bids_.set(order->price);
            if (order->price > best_bid_) best_bid_ = order->price;
        } else {
            best_asks_.set(order->price);
            if (order->price < best_ask_) best_ask_ = order->price;
        }
    }

    order->level = level;
    order->prev = level->tail;
    order->next = nullptr;

    if (level->tail) level->tail->next = order;
    else             level->head = order;

    level->tail = order;
    level->total_qty += order->quantity;
    ++level->count;
}

void OrderBook::remove_from_level(Order* order) {
    PriceLevel* level = order->level;
    if (!level) return;

    if (order->prev) order->prev->next = order->next;
    else             level->head = order->next;

    if (order->next) order->next->prev = order->prev;
    else             level->tail = order->prev;

    level->total_qty -= order->quantity;
    --level->count;

    if (level->empty()) {
        auto& levels = (order->side == Side::Buy) ? bid_levels_ : ask_levels_;
        levels.erase(order->price, level);
        update_best_after_remove(order->side, order->price);
    }
}

void OrderBook::update_best_after_remove(Side side, Price price) {
    if (side == Side::Buy) {
        best_bids_.reset(price);
        if (price == best_bid_) {
            best_bid_ = best_bids_.find_max_from(price);
        }
    } else {
        best_asks_.reset(price);
        if (price == best_ask_) {
            best_ask_ = best_asks_.find_min_from(price);
        }
    }
}

} // namespace exchange
