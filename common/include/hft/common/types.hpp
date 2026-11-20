#pragma once

#include <cstdint>
#include <limits>

namespace hft {

using Price = std::int64_t;            // Price in integer ticks.
using Quantity = std::uint32_t;        // Number of units.
using OrderId = std::uint64_t;         // Identifier for an order.
using SequenceNumber = std::uint64_t;  // Position in a market message stream.
using Timestamp = std::uint64_t;       // Time in nanoseconds.

inline constexpr Price kInvalidPrice = std::numeric_limits<Price>::min();
inline constexpr OrderId kInvalidOrderId = 0;

enum class Side : std::uint8_t {
    Buy,
    Sell
};

enum class OrderType : std::uint8_t {
    Limit,
    Market
};

constexpr const char* to_string(Side side) noexcept {
    switch (side) {
        case Side::Buy: return "Buy";
        case Side::Sell: return "Sell";
    }
    return "Unknown";
}

} // namespace hft
