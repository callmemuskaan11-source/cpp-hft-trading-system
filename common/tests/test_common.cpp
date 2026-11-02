
#include <cassert>
#include <cstdint>
#include <iostream>
#include <type_traits>

#include "hft/common/types.hpp"
#include "hft/common/order.hpp"
#include "hft/common/market_event.hpp"
#include "hft/common/execution_report.hpp"

int main() {
    using namespace hft;

    // Test 1: Fundamental types
    static_assert(sizeof(Price) == 8);
    static_assert(sizeof(Quantity) == 4);
    static_assert(sizeof(OrderId) == 8);
    static_assert(sizeof(SequenceNumber) == 8);
    static_assert(sizeof(Timestamp) == 8);

    // Test 2: Enum types
    static_assert(std::is_enum_v<Side>);
    static_assert(std::is_enum_v<EventType>);
    static_assert(std::is_enum_v<ExecutionStatus>);

    assert(Side::Buy != Side::Sell);
    assert(EventType::Add != EventType::Cancel);

    // Test 3: Order structure
    Order order{
        1001,
        Side::Buy,
        2000,
        50
    };

    assert(order.order_id == 1001);
    assert(order.side == Side::Buy);
    assert(order.price == 2000);
    assert(order.quantity == 50);

    // Test 4: MarketEvent structure
    MarketEvent event{
        1,
        1000000,
        EventType::Add,
        1001,
        Side::Buy,
        2000,
        50
    };

    assert(event.sequence_number == 1);
    assert(event.timestamp == 1000000);
    assert(event.event_type == EventType::Add);
    assert(event.order_id == 1001);
    assert(event.side == Side::Buy);
    assert(event.price == 2000);
    assert(event.quantity == 50);

    // Test 5: ExecutionReport structure
    ExecutionReport report{
        1001,
        ExecutionStatus::PartialFill,
        20,
        30,
        2000
    };

    assert(report.order_id == 1001);
    assert(report.status == ExecutionStatus::PartialFill);
    assert(report.filled_quantity == 20);
    assert(report.remaining_quantity == 30);
    assert(report.execution_price == 2000);

    std::cout << "All Stage 1 unit tests passed!\n";

    return 0;
}
