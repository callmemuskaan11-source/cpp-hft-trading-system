#pragma once

#include <filesystem>
#include <span>

#include "hft/common/market_event.hpp"

namespace hft::market_data {

// Headerless CSV with LF-terminated records:
// sequence_number,timestamp,order_id,price,quantity,event_type,side
// Integers are decimal. Event type and side use the names from hft::to_string.
// Reserved bytes are omitted. An empty stream is a zero-byte file.
// Throws std::runtime_error if the file cannot be opened or written.
void write_events(const std::filesystem::path& path, std::span<const MarketEvent> events);

} // namespace hft::market_data
