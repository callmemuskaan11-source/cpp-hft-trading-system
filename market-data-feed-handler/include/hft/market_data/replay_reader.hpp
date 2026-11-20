#pragma once

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "hft/common/market_event.hpp"

namespace hft::market_data {

// Reads the headerless CSV format documented in event_writer.hpp.
class ReplayReader {
public:
    explicit ReplayReader(const std::filesystem::path& path);

    // Returns nullopt at clean EOF. Throws std::runtime_error on I/O or parse failure.
    std::optional<MarketEvent> next();

private:
    std::filesystem::path path_;
    std::ifstream input_;
    std::string line_;
    std::size_t line_number_{};
};

} // namespace hft::market_data
