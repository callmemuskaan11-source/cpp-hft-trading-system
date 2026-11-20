#include <algorithm>
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

    auto invalid = add(3, Side::Buy, 100);
    invalid.event_type = static_cast<EventType>(255);
    CHECK_EQ(book.apply(invalid), BookResult::UnsupportedEvent);

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

MarketEvent update(EventType type, OrderId id, Side side, Price price, Quantity quantity) {
    return MarketEvent{0, 0, id, price, quantity, type, side};
}

void check_queue(const LimitOrderBook& book, Side side, Price price,
                 const std::vector<OrderId>& expected) {
    const auto* orders = book.orders_at(side, price);
    std::vector<OrderId> actual;
    if (orders != nullptr) {
        for (const auto& order : *orders) {
            actual.push_back(order.order_id);
            CHECK_EQ(book.find_order(order.order_id), &order);
        }
    }
    CHECK_EQ(actual, expected);
    CHECK_EQ(orders == nullptr, expected.empty());
    CHECK(book.is_consistent());
}

void test_modify_priority() {
    LimitOrderBook book;
    CHECK(book.is_consistent());
    CHECK_EQ(book.apply(add(1, Side::Buy, 100, 5)), BookResult::Added);
    CHECK_EQ(book.apply(add(2, Side::Buy, 100, 6)), BookResult::Added);
    CHECK_EQ(book.apply(add(3, Side::Buy, 105, 8)), BookResult::Added);
    CHECK_EQ(book.apply(add(4, Side::Sell, 110, 8)), BookResult::Added);
    const auto* first = book.find_order(1);

    CHECK_EQ(book.apply(update(EventType::Modify, 1, Side::Buy, 100, 5)), BookResult::Modified);
    check_queue(book, Side::Buy, 100, {1, 2});
    CHECK_EQ(book.apply(update(EventType::Modify, 1, Side::Buy, 100, 2)), BookResult::Modified);
    check_queue(book, Side::Buy, 100, {1, 2});
    CHECK_EQ(book.top_bids(2), (std::vector<BookLevel>{{105, 8}, {100, 8}}));

    CHECK_EQ(book.apply(update(EventType::Modify, 1, Side::Buy, 100, 4)), BookResult::Modified);
    check_queue(book, Side::Buy, 100, {2, 1});
    CHECK_EQ(book.top_bids(2), (std::vector<BookLevel>{{105, 8}, {100, 10}}));

    CHECK_EQ(book.apply(update(EventType::Modify, 1, Side::Buy, 105, 3)), BookResult::Modified);
    check_queue(book, Side::Buy, 100, {2});
    check_queue(book, Side::Buy, 105, {3, 1});
    CHECK_EQ(book.find_order(1), first);
    CHECK_EQ(book.top_bids(2), (std::vector<BookLevel>{{105, 11}, {100, 6}}));

    CHECK_EQ(book.apply(update(EventType::Modify, 2, Side::Buy, 108, 7)), BookResult::Modified);
    check_queue(book, Side::Buy, 100, {});
    check_queue(book, Side::Buy, 108, {2});
    CHECK_EQ(book.spread(), Price{2});
    CHECK_EQ(book.top_bids(2), (std::vector<BookLevel>{{108, 7}, {105, 11}}));
}

void test_cancel_and_trade() {
    LimitOrderBook book;
    CHECK_EQ(book.apply(add(1, Side::Buy, 100, 7)), BookResult::Added);
    CHECK_EQ(book.apply(add(2, Side::Buy, 100, 5)), BookResult::Added);
    CHECK_EQ(book.apply(add(3, Side::Buy, 98, 3)), BookResult::Added);
    CHECK_EQ(book.apply(add(4, Side::Sell, 105, 4)), BookResult::Added);
    CHECK_EQ(book.apply(add(5, Side::Sell, 106, 2)), BookResult::Added);
    CHECK_EQ(book.spread(), Price{5});

    CHECK_EQ(book.apply(update(EventType::Trade, 1, Side::Buy, 99, 3)), BookResult::Traded);
    check_queue(book, Side::Buy, 100, {1, 2});
    CHECK_EQ(book.top_bids(1), (std::vector<BookLevel>{{100, 9}}));
    CHECK_EQ(book.apply(update(EventType::Trade, 4, Side::Sell, 107, 4)), BookResult::Traded);
    check_queue(book, Side::Sell, 105, {});
    CHECK(book.find_order(4) == nullptr);
    CHECK_EQ(book.best_ask(), Price{106});
    CHECK_EQ(book.spread(), Price{6});
    CHECK_EQ(book.apply(update(EventType::Cancel, 2, Side::Buy, kInvalidPrice, 0)),
             BookResult::Cancelled);
    check_queue(book, Side::Buy, 100, {1});
    CHECK(book.find_order(2) == nullptr);
    CHECK_EQ(book.top_bids(1), (std::vector<BookLevel>{{100, 4}}));
    CHECK_EQ(book.apply(update(EventType::Cancel, 2, Side::Buy, 700, 0)), BookResult::UnknownOrderId);
    CHECK_EQ(book.apply(update(EventType::Trade, 1, Side::Buy, 100, 4)), BookResult::Traded);
    check_queue(book, Side::Buy, 100, {});
    CHECK_EQ(book.best_bid(), Price{98});
    CHECK_EQ(book.spread(), Price{8});
    CHECK_EQ(book.top_asks(10), (std::vector<BookLevel>{{106, 2}}));
}

void test_rejected_updates() {
    LimitOrderBook book;
    constexpr auto max = std::numeric_limits<Quantity>::max();
    CHECK_EQ(book.apply(add(1, Side::Buy, 100, 3)), BookResult::Added);
    CHECK_EQ(book.apply(add(2, Side::Buy, 100, max - 3)), BookResult::Added);
    CHECK_EQ(book.apply(add(3, Side::Buy, 99, 2)), BookResult::Added);
    CHECK_EQ(book.apply(add(4, Side::Sell, 105, 2)), BookResult::Added);
    const auto bids = book.top_bids(10);
    const auto asks = book.top_asks(10);

    const auto reject = [&](EventType type, OrderId id, Side side, Price price,
                            Quantity quantity, BookResult expected) {
        CHECK_EQ(book.apply(update(type, id, side, price, quantity)), expected);
        CHECK_EQ(book.top_bids(10), bids);
        CHECK_EQ(book.top_asks(10), asks);
        check_queue(book, Side::Buy, 100, {1, 2});
        check_queue(book, Side::Buy, 99, {3});
        check_queue(book, Side::Sell, 105, {4});
    };
    using enum EventType;
    using enum BookResult;
    reject(Modify, 9, Side::Buy, 100, 1, UnknownOrderId);
    reject(Cancel, 9, Side::Buy, 100, 0, UnknownOrderId);
    reject(Trade, 9, Side::Buy, 100, 1, UnknownOrderId);
    reject(Modify, 1, Side::Sell, 100, 1, SideMismatch);
    reject(Cancel, 1, Side::Sell, 100, 0, SideMismatch);
    reject(Trade, 1, Side::Sell, 100, 1, SideMismatch);
    reject(Modify, 1, Side::Buy, 100, 0, InvalidQuantity);
    reject(Modify, 1, Side::Buy, kInvalidPrice, 1, InvalidPrice);
    reject(Modify, 1, Side::Buy, 100, 4, QuantityOverflow);
    reject(Modify, 3, Side::Buy, 100, 1, QuantityOverflow);
    reject(Cancel, 1, Side::Buy, 100, 1, InvalidQuantity);
    reject(Trade, 1, Side::Buy, 100, 0, InvalidQuantity);
    reject(Trade, 1, Side::Buy, 100, 4, InvalidQuantity);
    reject(Trade, 1, Side::Buy, 101, 1, InvalidPrice);
    reject(Trade, 4, Side::Sell, 104, 1, InvalidPrice);
    reject(Trade, 4, Side::Sell, kInvalidPrice, 1, InvalidPrice);
}

void test_mixed_replay_against_reference() {
    SyntheticGenerator generator({73, 2000, 17, 50});
    LimitOrderBook book;
    // Global FIFO in a flat vector, filtered by side and price for comparisons.
    std::vector<MarketEvent> resting;
    std::array<std::size_t, 4> counts{};
    for (int i = 0; i < 100'000; ++i) {
        const auto event = generator.next();
        const auto found = std::find_if(resting.begin(), resting.end(), [&](const auto& order) {
            return order.order_id == event.order_id;
        });
        ++counts[static_cast<std::size_t>(event.event_type)];
        if (event.event_type == EventType::Add) {
            CHECK(found == resting.end());
            resting.push_back(event);
            CHECK_EQ(book.apply(event), BookResult::Added);
        } else {
            CHECK(found != resting.end());
            if (found == resting.end()) {
                return;
            }
            switch (event.event_type) {
                case EventType::Modify:
                    if (event.price != found->price || event.quantity > found->quantity) {
                        resting.erase(found);
                        resting.push_back(event);
                    } else {
                        found->quantity = event.quantity;
                    }
                    CHECK_EQ(book.apply(event), BookResult::Modified);
                    break;
                case EventType::Cancel:
                    resting.erase(found);
                    CHECK_EQ(book.apply(event), BookResult::Cancelled);
                    CHECK(book.find_order(event.order_id) == nullptr);
                    break;
                case EventType::Trade:
                    if (found->quantity == event.quantity) {
                        resting.erase(found);
                    } else {
                        found->quantity -= event.quantity;
                    }
                    CHECK_EQ(book.apply(event), BookResult::Traded);
                    break;
                case EventType::Add:
                    break;
            }
        }

        CHECK(book.is_consistent());
        for (const auto side : {Side::Buy, Side::Sell}) {
            std::vector<BookLevel> expected;
            for (const auto& order : resting) {
                if (order.side != side) {
                    continue;
                }
                const auto level = std::find_if(expected.begin(), expected.end(), [&](const auto& item) {
                    return item.price == order.price;
                });
                if (level == expected.end()) {
                    expected.push_back({order.price, order.quantity});
                } else {
                    level->aggregate_quantity += order.quantity;
                }
            }
            std::sort(expected.begin(), expected.end(), [side](const auto& left, const auto& right) {
                return side == Side::Buy ? left.price > right.price : left.price < right.price;
            });
            const auto actual = side == Side::Buy ? book.top_bids(100) : book.top_asks(100);
            CHECK_EQ(actual, expected);
            for (const auto& level : expected) {
                const auto* queue = book.orders_at(side, level.price);
                CHECK(queue != nullptr);
                if (queue == nullptr) {
                    continue;
                }
                auto position = queue->begin();
                for (const auto& order : resting) {
                    if (order.side == side && order.price == level.price) {
                        CHECK(position != queue->end());
                        if (position == queue->end()) {
                            break;
                        }
                        CHECK_EQ(position->order_id, order.order_id);
                        CHECK_EQ(position->quantity, order.quantity);
                        CHECK_EQ(book.find_order(order.order_id), &*position);
                        ++position;
                    }
                }
                CHECK(position == queue->end());
            }
        }
    }
    for (const auto count : counts) {
        CHECK(count > 0);
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
    test_modify_priority();
    test_cancel_and_trade();
    test_rejected_updates();
    test_mixed_replay_against_reference();
    return test::failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
