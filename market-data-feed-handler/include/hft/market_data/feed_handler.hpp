#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "hft/market_data/limit_order_book.hpp"
#include "hft/market_data/sequence_validator.hpp"

namespace hft::market_data {

enum class FeedHealth { Valid, Stale };

enum class FeedStatus {
    Applied, Gap, Duplicate, OutOfOrder, MalformedMessage, BookRejected, AllocationFailure, Stale
};

struct FeedResult {
    FeedStatus status;
    // Engaged when the order book returned a result.
    std::optional<BookResult> book_result{};
};

struct FeedCounters {
    std::uint64_t gaps{};
    // Duplicates only count repeats of the last applied sequence.
    std::uint64_t duplicates{};
    std::uint64_t out_of_order{};
    std::uint64_t malformed_messages{};
    std::uint64_t book_rejections{};
};

class FeedHandler {
public:
    // Decode one LF-terminated CSV message. Malformed messages are counted
    // even while Stale. Well-formed messages while Stale bypass classification.
    [[nodiscard]] FeedResult process(std::string_view message);

    [[nodiscard]] FeedHealth health() const noexcept { return health_; }
    [[nodiscard]] FeedCounters counters() const noexcept { return counters_; }
    [[nodiscard]] SequenceNumber last_applied() const noexcept { return sequence_.last_applied(); }
    [[nodiscard]] const LimitOrderBook& book() const noexcept { return book_; }

private:
    LimitOrderBook book_;
    SequenceValidator sequence_;
    FeedCounters counters_;
    FeedHealth health_{FeedHealth::Valid};
};

} // namespace hft::market_data
