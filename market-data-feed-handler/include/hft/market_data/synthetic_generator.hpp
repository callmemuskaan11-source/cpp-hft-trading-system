#pragma once

#include <random>
#include <vector>

#include "hft/common/market_event.hpp"
#include "hft/common/order.hpp"

namespace hft::market_data {

struct GeneratorConfig {
    std::uint64_t seed{1};
    Price mid_price{10'000};
    Price half_band{100};  // Inclusive range [mid_price - half_band, mid_price + half_band].
    Quantity max_quantity{100};
};

class SyntheticGenerator {
public:
    explicit SyntheticGenerator(GeneratorConfig config = {});

    // Timestamps are synthetic nanoseconds, one per sequence number.
    MarketEvent next();

private:
    Price next_price();
    Quantity next_quantity(Quantity max);

    GeneratorConfig config_;
    std::mt19937_64 random_;
    std::vector<Order> active_;
    SequenceNumber last_sequence_{};
    OrderId last_order_id_{};
};

} // namespace hft::market_data
