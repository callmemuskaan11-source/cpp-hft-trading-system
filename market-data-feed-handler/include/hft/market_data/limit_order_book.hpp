#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "hft/common/market_event.hpp"
#include "hft/common/order.hpp"

namespace hft::market_data {

enum class BookResult {
    Added,
    UnsupportedEvent,
    InvalidOrderId,
    DuplicateOrderId,
    InvalidSide,
    InvalidPrice,
    InvalidQuantity,
    QuantityOverflow,
    Modified,
    Cancelled,
    Traded,
    UnknownOrderId,
    SideMismatch,
    PriorityOverflow
};

struct BookLevel {
    Price price;
    Quantity aggregate_quantity;

    bool operator==(const BookLevel&) const = default;
};

// Mirrors exchange events. Locked and crossed prices are allowed and remain resting.
class LimitOrderBook {
public:
    LimitOrderBook() = default;
    LimitOrderBook(const LimitOrderBook&) = delete;
    LimitOrderBook& operator=(const LimitOrderBook&) = delete;

    // MODIFY sets remaining quantity. A price change or quantity increase
    // loses FIFO priority. CANCEL removes the whole order with quantity zero
    // and ignores price. TRADE applies an execution within the resting limit.
    // Allocation failures propagate with the book unchanged.
    [[nodiscard]] BookResult apply(const MarketEvent& event);

    [[nodiscard]] std::optional<Price> best_bid() const noexcept;
    [[nodiscard]] std::optional<Price> best_ask() const noexcept;
    // Empty if either side is missing. Throws std::overflow_error if the signed
    // tick difference cannot fit in Price.
    [[nodiscard]] std::optional<Price> spread() const;

    [[nodiscard]] std::vector<BookLevel> top_bids(std::size_t count) const;
    [[nodiscard]] std::vector<BookLevel> top_asks(std::size_t count) const;

    // Borrowed views. An order pointer and its iterator survive relocation
    // (the iterator follows the order to its new queue). Removal invalidates
    // both. A queue pointer expires when its last order leaves the level.
    // All views expire when the book is destroyed.
    [[nodiscard]] const Order* find_order(OrderId id) const;
    [[nodiscard]] const std::list<Order>* orders_at(Side side, Price price) const;

    [[nodiscard]] bool is_consistent() const;

private:
    struct PriceLevel {
        explicit PriceLevel(Price level_price) : price(level_price) {}

        Price price;
        std::list<Order> orders;
        Quantity aggregate_quantity{};
    };

    // The maps own levels, and each level owns its orders. Map insertion keeps
    // level pointers valid, and list append keeps order iterators valid. Index
    // rehashing only affects the index's own iterators. Copy and move are disabled
    // so the index always refers to nodes owned by this book.
    struct OrderLocation {
        Side side;
        PriceLevel* level;
        std::list<Order>::iterator order;
        std::uint64_t priority;
    };

    template <typename Levels>
    BookResult add_to(Levels& levels, const MarketEvent& event);

    BookResult modify(OrderLocation& location, const MarketEvent& event);
    void erase_empty_level(Side side, const PriceLevel& level);

    std::map<Price, PriceLevel, std::greater<Price>> bids_;
    std::map<Price, PriceLevel, std::less<Price>> asks_;
    std::unordered_map<OrderId, OrderLocation> index_;
    std::uint64_t last_priority_{};
};

} // namespace hft::market_data
