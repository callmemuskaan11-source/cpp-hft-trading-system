#include "hft/market_data/replay_reader.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace hft::market_data {
namespace {

template <typename Integer>
bool parse_integer(std::string_view field, Integer& value) {
    if (field.empty()) {
        return false;
    }
    const auto end = field.data() + field.size();
    const auto result = std::from_chars(field.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

template <typename Enum, std::size_t Size>
bool parse_enum(std::string_view field, const Enum (&values)[Size], Enum& value) {
    for (const auto candidate : values) {
        if (field == to_string(candidate)) {
            value = candidate;
            return true;
        }
    }
    return false;
}

const char* parse_record(std::string_view record, MarketEvent& event) {
    std::array<std::string_view, 7> fields;
    for (std::size_t index = 0; index < fields.size() - 1; ++index) {
        const auto comma = record.find(',');
        if (comma == std::string_view::npos) {
            return "expected seven CSV fields";
        }
        fields[index] = record.substr(0, comma);
        record.remove_prefix(comma + 1);
    }
    if (record.find(',') != std::string_view::npos) {
        return "expected seven CSV fields";
    }
    fields.back() = record;

    constexpr EventType event_types[]{EventType::Add, EventType::Modify,
                                      EventType::Cancel, EventType::Trade};
    constexpr Side sides[]{Side::Buy, Side::Sell};
    if (!parse_integer(fields[0], event.sequence_number) || event.sequence_number == 0) {
        return "invalid sequence_number";
    }
    if (!parse_integer(fields[1], event.timestamp)) {
        return "invalid timestamp";
    }
    if (!parse_integer(fields[2], event.order_id) || event.order_id == kInvalidOrderId) {
        return "invalid order_id";
    }
    if (!parse_integer(fields[3], event.price)) {
        return "invalid price";
    }
    if (!parse_enum(fields[5], event_types, event.event_type)) {
        return "invalid event_type";
    }
    if (!parse_enum(fields[6], sides, event.side)) {
        return "invalid side";
    }
    if (!parse_integer(fields[4], event.quantity) ||
        (event.quantity == 0) != (event.event_type == EventType::Cancel)) {
        return "invalid quantity for event_type";
    }
    return nullptr;
}

} // namespace

ReplayReader::ReplayReader(const std::filesystem::path& path)
    : path_(path), input_(path, std::ios::binary) {
    if (!input_) {
        throw std::runtime_error("Cannot open market event file for reading: " + path_.string());
    }
}

std::optional<MarketEvent> ReplayReader::next() {
    if (!std::getline(input_, line_)) {
        if (input_.eof() && !input_.bad()) {
            return std::nullopt;
        }
        throw std::runtime_error("Cannot read market event file: " + path_.string());
    }

    ++line_number_;
    MarketEvent event{};
    const auto* error = input_.eof() ? "unexpected EOF before record terminator" :
                                      parse_record(line_, event);
    if (error != nullptr) {
        throw std::runtime_error(path_.string() + ':' + std::to_string(line_number_) + ": " + error);
    }
    return event;
}

} // namespace hft::market_data
