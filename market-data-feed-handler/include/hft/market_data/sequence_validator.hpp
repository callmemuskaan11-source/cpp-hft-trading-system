#pragma once

#include <optional>

#include "hft/common/types.hpp"

namespace hft::market_data {

enum class SequenceStatus { Expected, Gap, Duplicate, OutOfOrder, Invalid };

class SequenceValidator {
public:
    // Zero represents a new stream with no applied sequence.
    explicit SequenceValidator(SequenceNumber last_applied = 0) noexcept
        : last_applied_(last_applied) {}

    [[nodiscard]] SequenceStatus classify(SequenceNumber sequence) const noexcept;
    [[nodiscard]] std::optional<SequenceNumber> next_expected() const noexcept;
    [[nodiscard]] SequenceNumber last_applied() const noexcept { return last_applied_; }

    // Advance only after successful application of the expected event.
    void record_applied(SequenceNumber sequence) noexcept;

private:
    SequenceNumber last_applied_;
};

} // namespace hft::market_data
