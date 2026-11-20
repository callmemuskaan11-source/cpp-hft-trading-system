#include <array>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

#include "hft/market_data/limit_order_book.hpp"
#include "hft/market_data/synthetic_generator.hpp"
#include "test_helpers.hpp"

namespace {

using namespace hft;
using namespace hft::market_data;

MarketEvent add(OrderId id, Side side, Price price, Quantity quantity = 1) {
    return MarketEvent{0, 0, id, price, quantity, EventType::Add, side};
}

void test_empty_and_one_sided() {
    LimitOrderBook book;
    CHECK(!book.best_bid());
    CHECK(!book.best_ask());
    CHECK(!book.spread());
    CHECK(book.top_bids(4).empty());
    CHECK(book.top_asks(4).empty());
    CHECK(book.find_order(1) == nullptr);
    CHECK(book.orders_at(Side::Buy, 100) == nullptr);

    CHECK_EQ(book.apply(add(1, Side::Buy, 100, 5)), BookResult::Added);
    CHECK_EQ(book.best_bid(), Price{100});
    CHECK(!book.best_ask());
    CHECK(!book.spread());
    CHECK_EQ(book.top_bids(4), (std::vector<BookLevel>{{100, 5}}));

    LimitOrderBook asks_only;
    CHECK_EQ(asks_only.apply(add(2, Side::Sell, 110, 6)), BookResult::Added);
    CHECK(!asks_only.best_bid());
    CHECK_EQ(asks_only.best_ask(), Price{110});
    CHECK(!asks_only.spread());
    CHECK_EQ(asks_only.top_asks(4), (std::vector<BookLevel>{{110, 6}}));
}

void test_depth_and_spread() {
    LimitOrderBook book;
    for (const auto& event : {add(1, Side::Buy, -7, 3), add(2, Side::Sell, 4, 4),
                              add(3, Side::Buy, -2, 5), add(4, Side::Sell, 0, 6),
                              add(5, Side::Buy, -12, 7), add(6, Side::Sell, 6, 8)}) {
        CHECK_EQ(book.apply(event), BookResult::Added);
    }
    CHECK_EQ(book.best_bid(), Price{-2});
    CHECK_EQ(book.best_ask(), Price{0});
    CHECK_EQ(book.spread(), Price{2});
    CHECK(book.top_bids(0).empty());
    CHECK(book.top_asks(0).empty());
    CHECK_EQ(book.top_bids(2), (std::vector<BookLevel>{{-2, 5}, {-7, 3}}));
    CHECK_EQ(book.top_bids(10), (std::vector<BookLevel>{{-2, 5}, {-7, 3}, {-12, 7}}));
    CHECK_EQ(book.top_asks(2), (std::vector<BookLevel>{{0, 6}, {4, 4}}));
    CHECK_EQ(book.top_asks(10), (std::vector<BookLevel>{{0, 6}, {4, 4}, {6, 8}}));

    CHECK_EQ(book.apply(add(7, Side::Buy, 0)), BookResult::Added);
    CHECK_EQ(book.spread(), Price{0});
    CHECK_EQ(book.apply(add(8, Side::Buy, 2)), BookResult::Added);
    CHECK_EQ(book.spread(), Price{-2});
    CHECK_EQ(book.top_asks(1), (std::vector<BookLevel>{{0, 6}}));
}

void test_fifo_and_lifetime() {
    LimitOrderBook book;
    CHECK_EQ(book.apply(add(9, Side::Buy, 99, 2)), BookResult::Added);
    const auto* queue = book.orders_at(Side::Buy, 99);
    const auto* first = book.find_order(9);
    CHECK(queue != nullptr);
    CHECK(first != nullptr);
    if (queue == nullptr || first == nullptr) {
        return;
    }
    const auto first_position = queue->begin();

    CHECK_EQ(book.apply(add(3, Side::Buy, 99, 4)), BookResult::Added);
    CHECK_EQ(book.apply(add(7, Side::Buy, 99, 8)), BookResult::Added);
    for (OrderId id = 100; id < 1'000; ++id) {
        CHECK_EQ(book.apply(add(id, Side::Sell, static_cast<Price>(id))), BookResult::Added);
    }

    CHECK_EQ(book.find_order(9), first);
    CHECK_EQ(&*first_position, first);
    CHECK_EQ(book.orders_at(Side::Buy, 99), queue);
    CHECK_EQ(book.top_bids(1), (std::vector<BookLevel>{{99, 14}}));
    std::vector<OrderId> order_ids;
    for (const auto& order : *queue) {
        order_ids.push_back(order.order_id);
        CHECK_EQ(book.find_order(order.order_id), &order);
    }
    CHECK_EQ(order_ids, (std::vector<OrderId>{9, 3, 7}));
}

void test_rejected_events() {
    LimitOrderBook book;
    constexpr Quantity max_quantity = std::numeric_limits<Quantity>::max();
    CHECK_EQ(book.apply(add(1, Side::Buy, 100, max_quantity - 1)), BookResult::Added);
    CHECK_EQ(book.apply(add(2, Side::Buy, 100)), BookResult::Added);
    const auto* original = book.find_order(1);

    CHECK_EQ(book.apply(add(1, Side::Sell, 101)), BookResult::DuplicateOrderId);
    CHECK_EQ(book.apply(add(3, Side::Sell, 101, 0)), BookResult::InvalidQuantity);
    CHECK_EQ(book.apply(add(3, static_cast<Side>(255), 101)), BookResult::InvalidSide);
    CHECK_EQ(book.apply(add(3, Side::Sell, kInvalidPrice)), BookResult::InvalidPrice);
    CHECK_EQ(book.apply(add(kInvalidOrderId, Side::Sell, 101)), BookResult::InvalidOrderId);
    CHECK_EQ(book.apply(add(3, Side::Buy, 100)), BookResult::QuantityOverflow);

    for (auto type : {EventType::Modify, EventType::Cancel, EventType::Trade,
                      static_cast<EventType>(255)}) {
        auto event = add(1, Side::Buy, 100);
        event.event_type = type;
        CHECK_EQ(book.apply(event), BookResult::UnsupportedEvent);
        event.order_id = 3;
        event.side = Side::Sell;
        CHECK_EQ(book.apply(event), BookResult::UnsupportedEvent);
    }

    CHECK_EQ(book.top_bids(10), (std::vector<BookLevel>{{100, max_quantity}}));
    CHECK(book.top_asks(10).empty());
    CHECK_EQ(book.find_order(1), original);
    CHECK(book.find_order(3) == nullptr);
    const auto* queue = book.orders_at(Side::Buy, 100);
    CHECK(queue != nullptr);
    if (queue != nullptr) {
        CHECK_EQ(queue->size(), std::size_t{2});
        CHECK_EQ(queue->front().quantity, max_quantity - 1);
    }

    CHECK_EQ(book.apply(add(3, Side::Buy, 99)), BookResult::Added);
}

void test_extreme_spreads() {
    constexpr Price low = kInvalidPrice + 1;
    constexpr Price high = std::numeric_limits<Price>::max();

    LimitOrderBook exact;
    CHECK_EQ(exact.apply(add(1, Side::Buy, low)), BookResult::Added);
    CHECK_EQ(exact.apply(add(2, Side::Sell, 0)), BookResult::Added);
    CHECK_EQ(exact.spread(), high);
    CHECK_EQ(exact.apply(add(3, Side::Buy, 1)), BookResult::Added);
    CHECK_EQ(exact.apply(add(4, Side::Sell, low)), BookResult::Added);
    CHECK_EQ(exact.spread(), std::numeric_limits<Price>::min());

    for (const auto& prices : {std::array{low, high}, std::array{high, low}}) {
        LimitOrderBook book;
        CHECK_EQ(book.apply(add(1, Side::Buy, prices[0])), BookResult::Added);
        CHECK_EQ(book.apply(add(2, Side::Sell, prices[1])), BookResult::Added);
        try {
            static_cast<void>(book.spread());
            CHECK(false);
        } catch (const std::overflow_error&) {
        }
        CHECK_EQ(book.best_bid(), prices[0]);
        CHECK_EQ(book.best_ask(), prices[1]);
    }
}

std::vector<BookLevel> brute_force_depth(const std::vector<MarketEvent>& events,
                                         const LimitOrderBook& book, Side side,
                                         Price first, Price last, Price step) {
    std::vector<BookLevel> expected;
    for (Price price = first; price != last; price += step) {
        std::vector<MarketEvent> resting;
        Quantity total{};
        for (const auto& event : events) {
            if (event.side == side && event.price == price) {
                resting.push_back(event);
                total += event.quantity;
            }
        }

        const auto* actual = book.orders_at(side, price);
        CHECK_EQ(actual == nullptr, resting.empty());
        if (resting.empty()) {
            continue;
        }
        expected.push_back({price, total});
        if (actual == nullptr) {
            continue;
        }
        CHECK_EQ(actual->size(), resting.size());
        std::size_t i = 0;
        for (const auto& order : *actual) {
            if (i >= resting.size()) {
                break;
            }
            const auto& event = resting[i++];
            CHECK_EQ(order.order_id, event.order_id);
            CHECK_EQ(order.price, price);
            CHECK_EQ(order.side, side);
            CHECK_EQ(order.quantity, event.quantity);
            CHECK_EQ(order.type, OrderType::Limit);
            CHECK_EQ(book.find_order(event.order_id), &order);
        }
    }
    return expected;
}

void test_generated_adds_against_reference() {
    constexpr GeneratorConfig config{42, 2'000, 17, 50};
    SyntheticGenerator generator(config);
    LimitOrderBook book;
    std::vector<MarketEvent> adds;
    for (int i = 0; i < 100'000; ++i) {
        const auto event = generator.next();
        if (event.event_type == EventType::Add) {
            adds.push_back(event);
            CHECK_EQ(book.apply(event), BookResult::Added);
        }
    }
    CHECK(adds.size() > 10'000);

    constexpr Price low = config.mid_price - config.half_band;
    constexpr Price high = config.mid_price + config.half_band;
    const auto bids = brute_force_depth(adds, book, Side::Buy, high, low - 1, -1);
    const auto asks = brute_force_depth(adds, book, Side::Sell, low, high + 1, 1);
    CHECK_EQ(book.top_bids(100), bids);
    CHECK_EQ(book.top_asks(100), asks);
    CHECK(!bids.empty() && !asks.empty());
    if (!bids.empty() && !asks.empty()) {
        CHECK_EQ(book.best_bid(), bids.front().price);
        CHECK_EQ(book.best_ask(), asks.front().price);
        CHECK_EQ(book.spread(), asks.front().price - bids.front().price);
    }
}

} // namespace

int main() {
    test_empty_and_one_sided();
    test_depth_and_spread();
    test_fifo_and_lifetime();
    test_rejected_events();
    test_extreme_spreads();
    test_generated_adds_against_reference();
    return test::failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
