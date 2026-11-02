
#pragma once

#include <cstdint>

namespace hft {

// Step 1: Define fundamental data types
using Price = std::int64_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using Timestamp = std::uint64_t;

// Step 2: Define order side
enum class Side : std::uint8_t {
    Buy,
    Sell
};

} // namespace hft
