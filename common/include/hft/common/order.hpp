
#pragma once

#include "hft/common/types.hpp"

namespace hft {

// Represents an order in the trading system
struct Order {
    OrderId order_id{};
    Side side{Side::Buy};
    Price price{};
    Quantity quantity{};
};

} // namespace hft
