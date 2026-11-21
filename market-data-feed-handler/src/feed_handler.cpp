#include "hft/market_data/feed_handler.hpp"

#include <new>

#include "hft/market_data/replay_reader.hpp"

namespace hft::market_data {

FeedResult FeedHandler::process(std::string_view message) {
    const auto event = decode_market_message(message);
    if (!event) {
        ++counters_.malformed_messages;
        return {FeedStatus::MalformedMessage};
    }
    if (health_ == FeedHealth::Stale) {
        return {FeedStatus::Stale};
    }

    switch (sequence_.classify(event->sequence_number)) {
        case SequenceStatus::Invalid:
            ++counters_.malformed_messages;
            return {FeedStatus::MalformedMessage};
        case SequenceStatus::Duplicate:
            ++counters_.duplicates;
            return {FeedStatus::Duplicate};
        case SequenceStatus::OutOfOrder:
            ++counters_.out_of_order;
            return {FeedStatus::OutOfOrder};
        case SequenceStatus::Gap:
            ++counters_.gaps;
            health_ = FeedHealth::Stale;
            return {FeedStatus::Gap};
        case SequenceStatus::Expected:
            break;
    }

    BookResult result;
    try {
        result = book_.apply(*event);
    } catch (const std::bad_alloc&) {
        health_ = FeedHealth::Stale;
        return {FeedStatus::AllocationFailure};
    }

    switch (result) {
        case BookResult::Added:
        case BookResult::Modified:
        case BookResult::Cancelled:
        case BookResult::Traded:
            sequence_.record_applied(event->sequence_number);
            return {FeedStatus::Applied, result};
        default:
            ++counters_.book_rejections;
            health_ = FeedHealth::Stale;
            return {FeedStatus::BookRejected, result};
    }
}

} // namespace hft::market_data
