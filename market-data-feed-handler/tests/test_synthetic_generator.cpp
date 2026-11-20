#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

#include "hft/market_data/synthetic_generator.hpp"
#include "test_helpers.hpp"

namespace {

using namespace hft;
using namespace hft::market_data;

auto fields(const MarketEvent& event) {
    return std::tie(event.sequence_number, event.timestamp, event.order_id, event.price,
                    event.quantity, event.event_type, event.side);
}

void test_stream() {
    constexpr GeneratorConfig config{42, 2'000, 17, 50};
    SyntheticGenerator generator(config);
    SyntheticGenerator duplicate(config);
    std::unordered_map<OrderId, Order> active;
    OrderId highest_id{};
    bool saw_modify = false;
    bool saw_cancel = false;
    bool saw_partial = false;
    bool saw_full = false;

    for (SequenceNumber sequence = 1; sequence <= 100'000; ++sequence) {
        const auto event = generator.next();
        const auto same_seed = duplicate.next();
        CHECK_EQ(fields(event), fields(same_seed));
        CHECK_EQ(event.sequence_number, sequence);
        CHECK_EQ(event.timestamp, sequence);
        CHECK(event.order_id != kInvalidOrderId);
        CHECK(event.price >= config.mid_price - config.half_band);
        CHECK(event.price <= config.mid_price + config.half_band);

        if (active.empty()) {
            CHECK_EQ(event.event_type, EventType::Add);
        }
        if (event.event_type == EventType::Add) {
            CHECK(event.order_id > highest_id);
            highest_id = event.order_id;
            CHECK(event.quantity > 0 && event.quantity <= config.max_quantity);
            CHECK(active.emplace(event.order_id,
                                 Order{event.order_id, event.price, event.quantity, event.side}).second);
            continue;
        }
        const auto resting = active.find(event.order_id);
        CHECK(resting != active.end());
        if (resting == active.end()) {
            continue;
        }
        auto& order = resting->second;
        CHECK_EQ(event.side, order.side);
        switch (event.event_type) {
            case EventType::Modify:
                saw_modify = true;
                CHECK(event.quantity > 0 && event.quantity <= config.max_quantity);
                order.quantity = event.quantity;
                order.price = event.price;
                break;
            case EventType::Cancel:
                saw_cancel = true;
                CHECK_EQ(event.quantity, Quantity{0});
                CHECK_EQ(event.price, order.price);
                active.erase(resting);
                break;
            case EventType::Trade:
                CHECK_EQ(event.price, order.price);
                CHECK(event.quantity > 0 && event.quantity <= order.quantity);
                if (event.quantity < order.quantity) {
                    saw_partial = true;
                    order.quantity -= event.quantity;
                } else {
                    saw_full = true;
                    active.erase(resting);
                }
                break;
            case EventType::Add:
                CHECK(false);
                break;
        }
    }

    CHECK(highest_id > 0);
    CHECK(saw_modify);
    CHECK(saw_cancel);
    CHECK(saw_partial);
    CHECK(saw_full);
}

void test_configuration() {
    constexpr Price max_price = std::numeric_limits<Price>::max();
    SyntheticGenerator single_tick({17, max_price, 0, 1});
    const auto event = single_tick.next();
    CHECK_EQ(event.price, max_price);
    CHECK_EQ(event.quantity, Quantity{1});

    for (const GeneratorConfig invalid : {GeneratorConfig{1, 0, -1, 1},
                                          GeneratorConfig{1, 0, 0, 0},
                                          GeneratorConfig{1, max_price, 1, 1},
                                          GeneratorConfig{1, kInvalidPrice + 1, 1, 1}}) {
        try {
            SyntheticGenerator generator(invalid);
            CHECK(false);
        } catch (const std::invalid_argument&) {
        }
    }
}

} // namespace

int main() {
    test_stream();
    test_configuration();
    return test::failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
