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
    switch (event.event_type) {
        case EventType::Add:
        case EventType::Modify:
        case EventType::Cancel:
        case EventType::Trade:
            break;
        default:
            return BookResult::UnsupportedEvent;
    }
    if (event.order_id == kInvalidOrderId) {
        return BookResult::InvalidOrderId;
    }
    if (event.side != Side::Buy && event.side != Side::Sell) {
        return BookResult::InvalidSide;
    }
    if (event.event_type != EventType::Cancel && event.price == kInvalidPrice) {
        return BookResult::InvalidPrice;
    }
    if ((event.quantity == 0) != (event.event_type == EventType::Cancel)) {
        return BookResult::InvalidQuantity;
    }

    const auto indexed = index_.find(event.order_id);
    if (event.event_type == EventType::Add) {
        if (indexed != index_.end()) {
            return BookResult::DuplicateOrderId;
        }
        if (last_priority_ == std::numeric_limits<std::uint64_t>::max()) {
            return BookResult::PriorityOverflow;
        }
        return event.side == Side::Buy ? add_to(bids_, event) : add_to(asks_, event);
    }
    if (indexed == index_.end()) {
        return BookResult::UnknownOrderId;
    }
    auto& location = indexed->second;
    if (location.side != event.side) {
        return BookResult::SideMismatch;
    }
    if (event.event_type == EventType::Modify) {
        return modify(location, event);
    }

    auto& level = *location.level;
    auto& order = *location.order;
    const bool cancel = event.event_type == EventType::Cancel;
    if (!cancel) {
        if ((event.side == Side::Buy && event.price > order.price) ||
            (event.side == Side::Sell && event.price < order.price)) {
            return BookResult::InvalidPrice;
        }
        if (event.quantity > order.quantity) {
            return BookResult::InvalidQuantity;
        }
    }

    const Quantity removed = cancel ? order.quantity : event.quantity;
    level.aggregate_quantity -= removed;
    if (removed == order.quantity) {
        const auto position = location.order;
        index_.erase(indexed);
        level.orders.erase(position);
        erase_empty_level(event.side, level);
    } else {
        order.quantity -= removed;
    }
    return cancel ? BookResult::Cancelled : BookResult::Traded;
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
                       OrderLocation{event.side, &level, std::prev(level.orders.end()),
                                     last_priority_ + 1});
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
    ++last_priority_;
    return BookResult::Added;
}

BookResult LimitOrderBook::modify(OrderLocation& location, const MarketEvent& event) {
    auto& old_level = *location.level;
    auto& order = *location.order;
    const bool price_changed = event.price != order.price;
    const bool loses_priority = price_changed || event.quantity > order.quantity;
    if (loses_priority && last_priority_ == std::numeric_limits<std::uint64_t>::max()) {
        return BookResult::PriorityOverflow;
    }

    PriceLevel* destination = &old_level;
    Quantity other_quantity = old_level.aggregate_quantity - order.quantity;
    if (price_changed) {
        // Check an existing destination before creating a level or moving the node.
        const auto find_level = [price = event.price](auto& levels) -> PriceLevel* {
            const auto position = levels.find(price);
            return position == levels.end() ? nullptr : &position->second;
        };
        destination = event.side == Side::Buy ? find_level(bids_) : find_level(asks_);
        other_quantity = destination == nullptr ? 0 : destination->aggregate_quantity;
    }
    if (event.quantity > std::numeric_limits<Quantity>::max() - other_quantity) {
        return BookResult::QuantityOverflow;
    }

    if (destination == nullptr) {
        destination = event.side == Side::Buy ?
            &bids_.try_emplace(event.price, event.price).first->second :
            &asks_.try_emplace(event.price, event.price).first->second;
    }
    // All potentially throwing work is complete. Splicing preserves the
    // order's address and iterator, including across distinct price levels.
    if (loses_priority) {
        destination->orders.splice(destination->orders.end(), old_level.orders, location.order);
        location.priority = ++last_priority_;
    }
    if (price_changed) {
        old_level.aggregate_quantity -= order.quantity;
        location.level = destination;
        order.price = event.price;
        erase_empty_level(event.side, old_level);
    }
    order.quantity = event.quantity;
    destination->aggregate_quantity = other_quantity + event.quantity;
    return BookResult::Modified;
}

void LimitOrderBook::erase_empty_level(Side side, const PriceLevel& level) {
    if (level.orders.empty()) {
        const Price price = level.price;
        if (side == Side::Buy) {
            bids_.erase(price);
        } else {
            asks_.erase(price);
        }
    }
}

bool LimitOrderBook::is_consistent() const {
    std::size_t order_count = 0;
    const auto check_side = [this, &order_count](const auto& levels, Side side) {
        for (const auto& [price, level] : levels) {
            if (level.price != price || price == kInvalidPrice || level.orders.empty()) {
                return false;
            }
            Quantity total = 0;
            std::uint64_t previous_priority = 0;
            for (auto order = level.orders.cbegin(); order != level.orders.cend(); ++order) {
                if (order_count == index_.size() || order->order_id == kInvalidOrderId ||
                    order->price != price || order->side != side || order->quantity == 0 ||
                    order->type != OrderType::Limit ||
                    order->quantity > std::numeric_limits<Quantity>::max() - total) {
                    return false;
                }
                const auto indexed = index_.find(order->order_id);
                if (indexed == index_.end()) {
                    return false;
                }
                const auto& location = indexed->second;
                if (location.side != side || location.level != &level || location.order != order ||
                    location.priority <= previous_priority || location.priority > last_priority_) {
                    return false;
                }
                previous_priority = location.priority;
                total += order->quantity;
                ++order_count;
            }
            if (total != level.aggregate_quantity) {
                return false;
            }
        }
        return true;
    };
    return check_side(bids_, Side::Buy) && check_side(asks_, Side::Sell) &&
           order_count == index_.size();
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
