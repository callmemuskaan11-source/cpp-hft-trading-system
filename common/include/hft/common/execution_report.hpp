
#pragma once

#include "hft/common/types.hpp"

namespace hft {

// Step 1: Define execution status types
enum class ExecutionStatus : std::uint8_t {
    Ack,
    Rejected,
    PartialFill,
    Filled,
    Cancelled
};

// Step 2: Define execution report structure
struct ExecutionReport {
    OrderId order_id{};
    ExecutionStatus status{ExecutionStatus::Ack};

    Quantity filled_quantity{};
    Quantity remaining_quantity{};
    Price execution_price{};
};

} // namespace hft
