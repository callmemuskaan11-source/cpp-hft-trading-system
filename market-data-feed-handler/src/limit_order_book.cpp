#include "hft/market_data/limit_order_book.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace hft::market_data {
namespace {

template <typename Levels>
std::vector<BookLevel> top_levels(const Levels& levels, std::size_t count) {
    std::vector<BookLevel> result;
    const auto limit = std::min(count, levels.size());
    result.reserve(limit);
    auto level = levels.begin();
    for (std::size_t i = 0; i < limit; ++i, ++level) {
        result.push_back({level->second.price, level->second.aggregate_quantity});
    }
    return result;
}

template <typename Levels>
const std::list<Order>* level_orders(const Levels& levels, Price price) {
    const auto level = levels.find(price);
    return level == levels.end() ? nullptr : &level->second.orders;
}

} // namespace

BookResult LimitOrderBook::apply(const MarketEvent& event) {
    if (event.event_type != EventType::Add) {
        return BookResult::UnsupportedEvent;
    }
    if (event.order_id == kInvalidOrderId) {
        return BookResult::InvalidOrderId;
    }
    if (event.side != Side::Buy && event.side != Side::Sell) {
        return BookResult::InvalidSide;
    }
    if (event.price == kInvalidPrice) {
        return BookResult::InvalidPrice;
    }
    if (event.quantity == 0) {
        return BookResult::InvalidQuantity;
    }
    if (index_.contains(event.order_id)) {
        return BookResult::DuplicateOrderId;
    }

    return event.side == Side::Buy ? add_to(bids_, event) : add_to(asks_, event);
}

template <typename Levels>
BookResult LimitOrderBook::add_to(Levels& levels, const MarketEvent& event) {
    const auto [position, created] = levels.try_emplace(event.price, event.price);
    auto& level = position->second;
    if (event.quantity > std::numeric_limits<Quantity>::max() - level.aggregate_quantity) {
        return BookResult::QuantityOverflow;
    }

    // The index insertion is the last throwing step. Roll back the append and
    // any newly created level if either allocation fails.
    bool appended = false;
    try {
        level.orders.push_back(Order{event.order_id, event.price, event.quantity, event.side});
        appended = true;
        index_.emplace(event.order_id,
                       OrderLocation{event.side, &level, std::prev(level.orders.cend())});
    } catch (...) {
        if (appended) {
            level.orders.pop_back();
        }
        if (created) {
            levels.erase(position);
        }
        throw;
    }

    level.aggregate_quantity += event.quantity;
    return BookResult::Added;
}

std::optional<Price> LimitOrderBook::best_bid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.begin()->first;
}

std::optional<Price> LimitOrderBook::best_ask() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

std::optional<Price> LimitOrderBook::spread() const {
    const auto bid = best_bid();
    const auto ask = best_ask();
    if (!bid || !ask) {
        return std::nullopt;
    }

    if ((*bid > 0 && *ask < std::numeric_limits<Price>::min() + *bid) ||
        (*bid < 0 && *ask > std::numeric_limits<Price>::max() + *bid)) {
        throw std::overflow_error("Order book spread is outside the Price range");
    }
    return *ask - *bid;
}

std::vector<BookLevel> LimitOrderBook::top_bids(std::size_t count) const {
    return top_levels(bids_, count);
}

std::vector<BookLevel> LimitOrderBook::top_asks(std::size_t count) const {
    return top_levels(asks_, count);
}

const Order* LimitOrderBook::find_order(OrderId id) const {
    const auto position = index_.find(id);
    return position == index_.end() ? nullptr : &*position->second.order;
}

const std::list<Order>* LimitOrderBook::orders_at(Side side, Price price) const {
    switch (side) {
        case Side::Buy: return level_orders(bids_, price);
        case Side::Sell: return level_orders(asks_, price);
    }
    return nullptr;
}

} // namespace hft::market_data
