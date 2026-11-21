#include "hft/market_data/sequence_validator.hpp"

#include <limits>

namespace hft::market_data {

std::optional<SequenceNumber> SequenceValidator::next_expected() const noexcept {
    if (last_applied_ == std::numeric_limits<SequenceNumber>::max()) {
        return std::nullopt;
    }
    return last_applied_ + 1;
}

SequenceStatus SequenceValidator::classify(SequenceNumber sequence) const noexcept {
    if (sequence == 0) {
        return SequenceStatus::Invalid;
    }
    if (sequence == last_applied_) {
        return SequenceStatus::Duplicate;
    }
    if (sequence < last_applied_) {
        return SequenceStatus::OutOfOrder;
    }
    return sequence == next_expected() ? SequenceStatus::Expected : SequenceStatus::Gap;
}

void SequenceValidator::record_applied(SequenceNumber sequence) noexcept {
    if (sequence == next_expected()) {
        last_applied_ = sequence;
    }
}

} // namespace hft::market_data
