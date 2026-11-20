#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string_view>
#include <type_traits>

#include "hft/common/clock.hpp"
#include "hft/common/execution_report.hpp"
#include "hft/common/market_event.hpp"
#include "hft/common/order.hpp"
#include "hft/common/types.hpp"
#include "test_helpers.hpp"

namespace {

using namespace hft;

void test_types() {
    static_assert(sizeof(Price) == 8);
    static_assert(sizeof(Quantity) == 4);
    static_assert(sizeof(OrderId) == 8);
    static_assert(sizeof(SequenceNumber) == 8);
    static_assert(sizeof(Timestamp) == 8);
    static_assert(std::is_enum_v<Side>);
    static_assert(std::is_enum_v<OrderType>);
    static_assert(std::is_enum_v<EventType>);
    static_assert(std::is_enum_v<ExecutionStatus>);
    CHECK_EQ(kInvalidPrice, std::numeric_limits<Price>::min());
    CHECK_EQ(kInvalidOrderId, OrderId{0});
}

template <typename Enum, std::size_t Size>
void check_enum_values(const Enum (&values)[Size], const std::string_view (&names)[Size]) {
    for (std::size_t i = 0; i < Size; ++i) {
        CHECK_EQ(std::string_view{to_string(values[i])}, names[i]);
        for (std::size_t j = 0; j < i; ++j) {
            CHECK(values[i] != values[j]);
        }
    }
}

void test_enum_values() {
    check_enum_values<Side>({Side::Buy, Side::Sell}, {"Buy", "Sell"});
    CHECK(OrderType::Limit != OrderType::Market);
    check_enum_values<EventType>(
        {EventType::Add, EventType::Modify, EventType::Cancel, EventType::Trade},
        {"Add", "Modify", "Cancel", "Trade"});
    check_enum_values<ExecutionStatus>(
        {ExecutionStatus::Ack, ExecutionStatus::Rejected, ExecutionStatus::PartialFill,
         ExecutionStatus::Filled, ExecutionStatus::Cancelled},
        {"Ack", "Rejected", "PartialFill", "Filled", "Cancelled"});
}

void test_order() {
    const Order defaults;
    CHECK_EQ(defaults.order_id, kInvalidOrderId);
    CHECK_EQ(defaults.price, Price{0});
    CHECK_EQ(defaults.quantity, Quantity{0});
    CHECK_EQ(defaults.side, Side::Buy);
    CHECK_EQ(defaults.type, OrderType::Limit);

    const Order order{1001, 2000, 50, Side::Buy, OrderType::Limit};
    CHECK_EQ(order.order_id, OrderId{1001});
    CHECK_EQ(order.price, Price{2000});
    CHECK_EQ(order.quantity, Quantity{50});
    CHECK_EQ(order.side, Side::Buy);
    CHECK_EQ(order.type, OrderType::Limit);

    const Order market_order{1002, kInvalidPrice, 50, Side::Sell, OrderType::Market};
    CHECK_EQ(market_order.type, OrderType::Market);
    CHECK_EQ(market_order.price, kInvalidPrice);
}

void test_market_event() {
    const MarketEvent defaults;
    CHECK_EQ(defaults.sequence_number, SequenceNumber{0});
    CHECK_EQ(defaults.timestamp, Timestamp{0});
    CHECK_EQ(defaults.order_id, kInvalidOrderId);
    CHECK_EQ(defaults.price, Price{0});
    CHECK_EQ(defaults.quantity, Quantity{0});
    CHECK_EQ(defaults.event_type, EventType::Add);
    CHECK_EQ(defaults.side, Side::Buy);

    const MarketEvent event{1, 1000000, 1001, 2000, 50, EventType::Add, Side::Buy};
    CHECK_EQ(event.sequence_number, SequenceNumber{1});
    CHECK_EQ(event.timestamp, Timestamp{1000000});
    CHECK_EQ(event.order_id, OrderId{1001});
    CHECK_EQ(event.price, Price{2000});
    CHECK_EQ(event.quantity, Quantity{50});
    CHECK_EQ(event.event_type, EventType::Add);
    CHECK_EQ(event.side, Side::Buy);

    const MarketEvent trade{2, 1000010, 1002, 2001, 20, EventType::Trade, Side::Sell};
    MarketEvent copy;
    std::memcpy(&copy, &trade, sizeof(copy));
    CHECK_EQ(copy.sequence_number, trade.sequence_number);
    CHECK_EQ(copy.timestamp, trade.timestamp);
    CHECK_EQ(copy.order_id, trade.order_id);
    CHECK_EQ(copy.price, trade.price);
    CHECK_EQ(copy.quantity, trade.quantity);
    CHECK_EQ(copy.event_type, trade.event_type);
    CHECK_EQ(copy.side, trade.side);
}

void test_execution_report() {
    const ExecutionReport defaults;
    CHECK_EQ(defaults.order_id, kInvalidOrderId);
    CHECK_EQ(defaults.timestamp, Timestamp{0});
    CHECK_EQ(defaults.execution_price, Price{0});
    CHECK_EQ(defaults.filled_quantity, Quantity{0});
    CHECK_EQ(defaults.remaining_quantity, Quantity{0});
    CHECK_EQ(defaults.status, ExecutionStatus::Ack);
    CHECK_EQ(defaults.side, Side::Buy);

    const ExecutionReport report{1001, 1000000, 2000, 20, 30,
                                 ExecutionStatus::PartialFill, Side::Sell};
    CHECK_EQ(report.order_id, OrderId{1001});
    CHECK_EQ(report.timestamp, Timestamp{1000000});
    CHECK_EQ(report.execution_price, Price{2000});
    CHECK_EQ(report.filled_quantity, Quantity{20});
    CHECK_EQ(report.remaining_quantity, Quantity{30});
    CHECK_EQ(report.status, ExecutionStatus::PartialFill);
    CHECK_EQ(report.side, Side::Sell);
}

void test_clock() {
    const Timestamp first = now_ns();
    const Timestamp second = now_ns();
    CHECK(second >= first);
}

} // namespace

int main() {
    test_types();
    test_enum_values();
    test_order();
    test_market_event();
    test_execution_report();
    test_clock();

    if (test::failures != 0) {
        std::cerr << test::failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Stage 1 unit tests passed!\n";
    return EXIT_SUCCESS;
}
