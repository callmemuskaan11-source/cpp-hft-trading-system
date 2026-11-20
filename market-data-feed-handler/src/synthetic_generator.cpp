#include "hft/market_data/synthetic_generator.hpp"

#include <limits>
#include <stdexcept>

namespace hft::market_data {

SyntheticGenerator::SyntheticGenerator(GeneratorConfig config)
    : config_(config), random_(config.seed) {
    if (config_.max_quantity == 0 || config_.half_band < 0 ||
        config_.mid_price <= kInvalidPrice + config_.half_band ||
        config_.mid_price > std::numeric_limits<Price>::max() - config_.half_band) {
        throw std::invalid_argument("Invalid synthetic generator price band or maximum quantity");
    }
}

Price SyntheticGenerator::next_price() {
    return std::uniform_int_distribution<Price>(
        config_.mid_price - config_.half_band, config_.mid_price + config_.half_band)(random_);
}

Quantity SyntheticGenerator::next_quantity(Quantity max) {
    return std::uniform_int_distribution<Quantity>(1, max)(random_);
}

MarketEvent SyntheticGenerator::next() {
    if (last_sequence_ == std::numeric_limits<SequenceNumber>::max()) {
        throw std::overflow_error("Synthetic market data sequence exhausted");
    }

    MarketEvent event{};
    event.sequence_number = last_sequence_ + 1;
    event.timestamp = event.sequence_number;

    constexpr EventType event_types[]{EventType::Add, EventType::Modify,
                                      EventType::Cancel, EventType::Trade};
    event.event_type = active_.empty() ? EventType::Add :
        event_types[std::uniform_int_distribution<unsigned>(0, 3)(random_)];

    if (event.event_type == EventType::Add) {
        event.order_id = last_order_id_ + 1;
        event.price = next_price();
        event.quantity = next_quantity(config_.max_quantity);
        event.side = std::bernoulli_distribution(0.5)(random_) ? Side::Buy : Side::Sell;
        active_.push_back(Order{event.order_id, event.price, event.quantity, event.side});
        last_order_id_ = event.order_id;
    } else {
        const auto index = std::uniform_int_distribution<std::size_t>(0, active_.size() - 1)(random_);
        auto& order = active_[index];
        event.order_id = order.order_id;
        event.price = order.price;
        event.side = order.side;

        switch (event.event_type) {
            case EventType::Modify:
                order.price = event.price = next_price();
                order.quantity = event.quantity = next_quantity(config_.max_quantity);
                break;
            case EventType::Cancel:
                order.quantity = 0;
                break;
            case EventType::Trade:
                event.quantity = next_quantity(order.quantity);
                order.quantity -= event.quantity;
                break;
            case EventType::Add:
                break;
        }

        if (order.quantity == 0) {
            order = active_.back();
            active_.pop_back();
        }
    }

    last_sequence_ = event.sequence_number;
    return event;
}

} // namespace hft::market_data
