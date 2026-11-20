#include "hft/market_data/event_writer.hpp"

#include <fstream>
#include <locale>
#include <stdexcept>

namespace hft::market_data {

void write_events(const std::filesystem::path& path, std::span<const MarketEvent> events) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Cannot open market event file for writing: " + path.string());
    }

    output.imbue(std::locale::classic());
    for (const auto& event : events) {
        output << event.sequence_number << ',' << event.timestamp << ',' << event.order_id << ','
               << event.price << ',' << event.quantity << ',' << to_string(event.event_type) << ','
               << to_string(event.side) << '\n';
        if (!output) {
            throw std::runtime_error("Cannot write market event file: " + path.string());
        }
    }
    output.close();
    if (!output) {
        throw std::runtime_error("Cannot finish writing market event file: " + path.string());
    }
}

} // namespace hft::market_data
