#pragma once

#include <type_traits>

#include "hft/common/types.hpp"

namespace hft {

// 24 bytes.
struct Order {
    OrderId order_id{};
    Price price{};  // Use kInvalidPrice for market orders.
    Quantity quantity{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    std::uint8_t reserved[2]{};  // Zero-initialized tail bytes.
};

static_assert(std::is_trivially_copyable_v<Order>);
static_assert(std::is_standard_layout_v<Order>);
static_assert(sizeof(Order) == 24);

} // namespace hft
