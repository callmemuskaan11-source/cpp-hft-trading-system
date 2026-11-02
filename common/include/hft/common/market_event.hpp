
#pragma once

#include "hft/common/types.hpp"

namespace hft {

// Step 1: Define market event types
enum class EventType : std::uint8_t {
    Add,
    Modify,
    Cancel,
    Trade
};

// Step 2: Define market event structure
struct MarketEvent {
    SequenceNumber sequence_number{};
    Timestamp timestamp{};
    EventType event_type{EventType::Add};

    OrderId order_id{};
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
};

} // namespace hft
