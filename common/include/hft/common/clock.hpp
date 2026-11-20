#pragma once

#include <chrono>

#include "hft/common/types.hpp"

namespace hft {

// Nanoseconds since the steady clock's epoch.
inline Timestamp now_ns() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
    return static_cast<Timestamp>(nanoseconds.count());
}

} // namespace hft
