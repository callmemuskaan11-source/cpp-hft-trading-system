#pragma once

#include <type_traits>

#include "hft/common/types.hpp"

namespace hft {

enum class EventType : std::uint8_t {
    Add,
    Modify,
    Cancel,
    Trade
};

constexpr const char* to_string(EventType type) noexcept {
    switch (type) {
        case EventType::Add: return "Add";
        case EventType::Modify: return "Modify";
        case EventType::Cancel: return "Cancel";
        case EventType::Trade: return "Trade";
    }
    return "Unknown";
}

// 40 bytes.
// ADD: order_id = new order, side, price, quantity = order size.
// MODIFY: order_id = existing order, price = new price, quantity = NEW total remaining quantity of that order (not a delta).
// CANCEL: order_id = existing order; the whole order is removed; quantity is unused and must be 0.
// TRADE: order_id = the RESTING order that was hit, price = trade price, quantity = quantity executed in this event (not remaining). side = side of the resting order.
// sequence_number starts at 1 and increases by exactly 1 per message; 0 is invalid.
struct MarketEvent {
    SequenceNumber sequence_number{};
    Timestamp timestamp{};
    OrderId order_id{};
    Price price{};
    Quantity quantity{};
    EventType event_type{EventType::Add};
    Side side{Side::Buy};
    std::uint8_t reserved[2]{};  // Zero-initialized tail bytes.
};

static_assert(std::is_trivially_copyable_v<MarketEvent>);
static_assert(std::is_standard_layout_v<MarketEvent>);
static_assert(sizeof(MarketEvent) == 40);

} // namespace hft
