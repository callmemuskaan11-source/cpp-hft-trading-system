#pragma once

#include <type_traits>

#include "hft/common/types.hpp"

namespace hft {

enum class ExecutionStatus : std::uint8_t {
    Ack,
    Rejected,
    PartialFill,
    Filled,
    Cancelled
};

constexpr const char* to_string(ExecutionStatus status) noexcept {
    switch (status) {
        case ExecutionStatus::Ack: return "Ack";
        case ExecutionStatus::Rejected: return "Rejected";
        case ExecutionStatus::PartialFill: return "PartialFill";
        case ExecutionStatus::Filled: return "Filled";
        case ExecutionStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

// 40 bytes.
struct ExecutionReport {
    OrderId order_id{};
    Timestamp timestamp{};
    Price execution_price{};
    Quantity filled_quantity{};     // Quantity executed in this fill.
    Quantity remaining_quantity{};  // Quantity left after this fill.
    ExecutionStatus status{ExecutionStatus::Ack};
    Side side{};
    std::uint8_t reserved[6]{};  // Zero-initialized tail bytes.
};

static_assert(std::is_trivially_copyable_v<ExecutionReport>);
static_assert(std::is_standard_layout_v<ExecutionReport>);
static_assert(sizeof(ExecutionReport) == 40);

} // namespace hft
