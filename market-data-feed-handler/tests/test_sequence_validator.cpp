#include "hft/market_data/feed_handler.hpp"

#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <string_view>
#include <vector>

#include "test_helpers.hpp"

namespace {

thread_local std::optional<std::size_t> allocations_before_failure;

class FailAllocation {
public:
    explicit FailAllocation(std::size_t successful_allocations) noexcept {
        allocations_before_failure = successful_allocations;
    }

    ~FailAllocation() { allocations_before_failure.reset(); }

    FailAllocation(const FailAllocation&) = delete;
    FailAllocation& operator=(const FailAllocation&) = delete;
};

} // namespace

// Test-executable replacement for the ordinary scalar allocations used by
// the book's map, list, and index. Fail once, only inside an armed scope.
void* operator new(std::size_t size) {
    if (allocations_before_failure) {
        if (*allocations_before_failure == 0) {
            allocations_before_failure.reset();
            throw std::bad_alloc{};
        }
        --*allocations_before_failure;
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    ::operator delete(memory);
}

namespace {

using namespace hft;
using namespace hft::market_data;

void test_dispatch_and_sequence_anomalies() {
    FeedHandler feed;
    CHECK_EQ(feed.health(), FeedHealth::Valid);
    CHECK_EQ(feed.last_applied(), 0U);

    const auto add = feed.process("1,1,10,100,5,Add,Buy\n");
    CHECK_EQ(add.status, FeedStatus::Applied);
    CHECK_EQ(add.book_result, BookResult::Added);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{100, 5}}));
    CHECK(feed.book().is_consistent());

    const auto modify = feed.process("2,2,10,101,4,Modify,Buy\n");
    CHECK_EQ(modify.status, FeedStatus::Applied);
    CHECK_EQ(modify.book_result, BookResult::Modified);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{101, 4}}));
    CHECK(feed.book().is_consistent());

    CHECK_EQ(feed.process("2,2,10,101,0,Cancel,Buy\n").status, FeedStatus::Duplicate);
    CHECK_EQ(feed.process("1,1,10,101,0,Cancel,Buy\n").status, FeedStatus::OutOfOrder);
    CHECK_EQ(feed.health(), FeedHealth::Valid);
    CHECK_EQ(feed.last_applied(), 2U);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{101, 4}}));

    const auto trade = feed.process("3,3,10,100,1,Trade,Buy\n");
    CHECK_EQ(trade.status, FeedStatus::Applied);
    CHECK_EQ(trade.book_result, BookResult::Traded);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{101, 3}}));
    CHECK(feed.book().is_consistent());

    const auto cancel = feed.process("4,4,10,0,0,Cancel,Buy\n");
    CHECK_EQ(cancel.status, FeedStatus::Applied);
    CHECK_EQ(cancel.book_result, BookResult::Cancelled);
    CHECK(feed.book().top_bids(1).empty());
    CHECK(feed.book().is_consistent());
    CHECK_EQ(feed.last_applied(), 4U);
    CHECK_EQ(feed.counters().duplicates, 1U);
    CHECK_EQ(feed.counters().out_of_order, 1U);
    CHECK_EQ(feed.counters().gaps, 0U);
}

void test_gap_and_stale_precedence() {
    FeedHandler initial_gap;
    CHECK_EQ(initial_gap.process("2,2,10,100,5,Add,Buy\n").status, FeedStatus::Gap);
    CHECK_EQ(initial_gap.health(), FeedHealth::Stale);
    CHECK_EQ(initial_gap.last_applied(), 0U);
    CHECK(initial_gap.book().top_bids(1).empty());

    FeedHandler feed;
    CHECK_EQ(feed.process("1,1,10,100,5,Add,Buy\n").status, FeedStatus::Applied);
    CHECK_EQ(feed.process("2,2,10,100,4,Modify,Buy\n").status, FeedStatus::Applied);
    const auto gap = feed.process("4,4,10,100,0,Cancel,Buy\n");
    CHECK_EQ(gap.status, FeedStatus::Gap);
    CHECK(!gap.book_result);
    CHECK_EQ(feed.health(), FeedHealth::Stale);
    CHECK_EQ(feed.process("3,3,10,100,0,Cancel,Buy\n").status, FeedStatus::Stale);
    CHECK_EQ(feed.process("2,2,10,100,0,Cancel,Buy\n").status, FeedStatus::Stale);
    const auto older = feed.process("1,1,10,100,0,Cancel,Buy\n");
    CHECK_EQ(older.status, FeedStatus::Stale);
    CHECK(!older.book_result);
    CHECK_EQ(feed.process("5,5,10,100,0,Cancel,Buy\n").status, FeedStatus::Stale);
    CHECK_EQ(feed.process("broken\n").status, FeedStatus::MalformedMessage);
    CHECK_EQ(feed.health(), FeedHealth::Stale);
    CHECK_EQ(feed.last_applied(), 2U);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{100, 4}}));
    CHECK(feed.book().is_consistent());
    CHECK_EQ(feed.counters().gaps, 1U);
    CHECK_EQ(feed.counters().malformed_messages, 1U);
    CHECK_EQ(feed.counters().duplicates, 0U);
    CHECK_EQ(feed.counters().out_of_order, 0U);
    CHECK_EQ(feed.counters().book_rejections, 0U);
}

void test_malformed_messages() {
    FeedHandler feed;
    constexpr std::string_view malformed[]{
        "",
        "1,1,10,100,5,Add,Buy",
        "1,1,10,100,5,Add,Buy\r\n",
        "1,1,10,100,5,Add,Buy\n2,2,11,100,5,Add,Buy\n",
        "0,1,10,100,5,Add,Buy\n",
        "-1,1,10,100,5,Add,Buy\n",
        "1x,1,10,100,5,Add,Buy\n",
        "18446744073709551616,1,10,100,5,Add,Buy\n",
        "1,-1,10,100,5,Add,Buy\n",
        "1,18446744073709551616,10,100,5,Add,Buy\n",
        "1,1,0,100,5,Add,Buy\n",
        "1,1,18446744073709551616,100,5,Add,Buy\n",
        "1,1,10,100x,5,Add,Buy\n",
        "1,1,10,9223372036854775808,5,Add,Buy\n",
        "1,1,10,-9223372036854775809,5,Add,Buy\n",
        "1,1,10,100,,Add,Buy\n",
        "1,1,10,100,-1,Add,Buy\n",
        "1,1,10,100,4294967296,Add,Buy\n",
        "1,1,10,100,5,Unknown,Buy\n",
        "1,1,10,100,5,Add,Unknown\n",
        "1,1,10,100,5,Cancel,Buy\n",
    };
    for (const auto message : malformed) {
        const auto result = feed.process(message);
        CHECK_EQ(result.status, FeedStatus::MalformedMessage);
        CHECK(!result.book_result);
    }
    CHECK_EQ(feed.counters().malformed_messages, std::size(malformed));
    CHECK_EQ(feed.counters().gaps, 0U);
    CHECK_EQ(feed.health(), FeedHealth::Valid);
    CHECK_EQ(feed.last_applied(), 0U);
    CHECK(feed.book().top_bids(1).empty());
    CHECK_EQ(feed.process("1,1,10,100,5,Add,Buy\n").status, FeedStatus::Applied);
    CHECK(feed.book().is_consistent());
}

void test_book_rejection() {
    FeedHandler feed;
    CHECK_EQ(feed.process("1,1,10,100,5,Add,Buy\n").status, FeedStatus::Applied);
    const auto rejected = feed.process("2,2,10,101,1,Trade,Buy\n");
    CHECK_EQ(rejected.status, FeedStatus::BookRejected);
    CHECK_EQ(rejected.book_result, BookResult::InvalidPrice);
    CHECK_EQ(feed.health(), FeedHealth::Stale);
    CHECK_EQ(feed.process("2,2,10,100,1,Trade,Buy\n").status, FeedStatus::Stale);
    CHECK_EQ(feed.counters().book_rejections, 1U);
    CHECK_EQ(feed.counters().gaps, 0U);
    CHECK_EQ(feed.counters().malformed_messages, 0U);
    CHECK_EQ(feed.last_applied(), 1U);
    CHECK_EQ(feed.book().top_bids(1), (std::vector<BookLevel>{{100, 5}}));
    CHECK(feed.book().is_consistent());
}

void test_sell_dispatch_and_full_trade() {
    FeedHandler feed;
    CHECK_EQ(feed.process("1,1,10,100,5,Add,Sell\n").book_result, BookResult::Added);
    CHECK_EQ(feed.book().best_ask(), 100);
    CHECK(feed.book().is_consistent());

    CHECK_EQ(feed.process("2,2,10,101,4,Modify,Sell\n").book_result, BookResult::Modified);
    CHECK_EQ(feed.book().top_asks(2), (std::vector<BookLevel>{{101, 4}}));
    CHECK(feed.book().is_consistent());

    const auto trade = feed.process("3,3,10,102,4,Trade,Sell\n");
    CHECK_EQ(trade.status, FeedStatus::Applied);
    CHECK_EQ(trade.book_result, BookResult::Traded);
    CHECK(feed.book().find_order(10) == nullptr);
    CHECK(feed.book().orders_at(Side::Sell, 101) == nullptr);
    CHECK(!feed.book().best_ask());
    CHECK(!feed.book().best_bid());
    CHECK_EQ(feed.last_applied(), 3U);
    CHECK_EQ(feed.health(), FeedHealth::Valid);
    CHECK(feed.book().is_consistent());
}

void test_unknown_order_rejection() {
    constexpr std::string_view messages[]{
        "2,2,99,101,4,Modify,Sell\n",
        "2,2,99,100,0,Cancel,Sell\n",
        "2,2,99,100,4,Trade,Sell\n",
    };
    for (const auto message : messages) {
        FeedHandler feed;
        CHECK_EQ(feed.process("1,1,10,100,5,Add,Sell\n").status, FeedStatus::Applied);
        const auto rejected = feed.process(message);
        CHECK_EQ(rejected.status, FeedStatus::BookRejected);
        CHECK_EQ(rejected.book_result, BookResult::UnknownOrderId);
        CHECK_EQ(feed.health(), FeedHealth::Stale);
        CHECK_EQ(feed.last_applied(), 1U);
        CHECK_EQ(feed.counters().book_rejections, 1U);
        CHECK_EQ(feed.process("2,2,10,100,0,Cancel,Sell\n").status, FeedStatus::Stale);
        CHECK_EQ(feed.book().top_asks(2), (std::vector<BookLevel>{{100, 5}}));
        CHECK(feed.book().is_consistent());
    }
}

void test_allocation_rollback(std::string_view message, BookResult success,
                              std::size_t minimum_failures) {
    // Rebuild the same pre-state for each allocation. Stop when no allocation
    // reaches the fault index, allowing for different container implementations.
    std::size_t failures = 0;
    bool reached_success = false;
    for (std::size_t index = 0; index < 16; ++index) {
        FeedHandler feed;
        CHECK_EQ(feed.process("1,1,10,100,5,Add,Buy\n").status, FeedStatus::Applied);
        CHECK_EQ(feed.process("2,2,11,100,7,Add,Buy\n").status, FeedStatus::Applied);
        const auto* first = feed.book().find_order(10);
        const auto* second = feed.book().find_order(11);
        const auto* queue = feed.book().orders_at(Side::Buy, 100);

        const auto result = [&] {
            const FailAllocation failure(index);
            return feed.process(message);
        }();
        if (result.status == FeedStatus::Applied) {
            CHECK_EQ(result.book_result, success);
            CHECK_EQ(feed.last_applied(), 3U);
            CHECK(feed.book().is_consistent());
            reached_success = true;
            break;
        }

        CHECK_EQ(result.status, FeedStatus::AllocationFailure);
        CHECK(!result.book_result);
        ++failures;
        CHECK_EQ(feed.health(), FeedHealth::Stale);
        CHECK_EQ(feed.last_applied(), 2U);
        CHECK_EQ(feed.counters().book_rejections, 0U);
        CHECK_EQ(feed.process(message).status, FeedStatus::Stale);
        CHECK_EQ(feed.book().top_bids(2), (std::vector<BookLevel>{{100, 12}}));
        CHECK(feed.book().find_order(12) == nullptr);
        CHECK(feed.book().orders_at(Side::Buy, 101) == nullptr);
        CHECK_EQ(feed.book().find_order(10), first);
        CHECK_EQ(feed.book().find_order(11), second);
        const auto* current_queue = feed.book().orders_at(Side::Buy, 100);
        CHECK_EQ(current_queue, queue);
        CHECK(current_queue != nullptr);
        if (current_queue != nullptr && current_queue->size() == 2) {
            CHECK_EQ(current_queue->front().order_id, 10U);
            CHECK_EQ(current_queue->front().quantity, 5U);
            CHECK_EQ(current_queue->back().order_id, 11U);
            CHECK_EQ(current_queue->back().quantity, 7U);
        } else {
            CHECK(false);
        }
        CHECK(feed.book().is_consistent());
    }
    CHECK(reached_success);
    CHECK(failures >= minimum_failures);
}

void test_allocation_failures() {
    // ADD allocates at least a level, a list node, and an index node.
    test_allocation_rollback("3,3,12,101,6,Add,Buy\n", BookResult::Added, 3);
    // Moving to a new price allocates a level before the order is spliced.
    test_allocation_rollback("3,3,10,101,6,Modify,Buy\n", BookResult::Modified, 1);
}

void test_sequence_boundaries() {
    SequenceValidator fresh;
    CHECK_EQ(fresh.next_expected(), 1U);
    CHECK_EQ(fresh.classify(0), SequenceStatus::Invalid);
    fresh.record_applied(2);
    CHECK_EQ(fresh.last_applied(), 0U);

    constexpr auto max = std::numeric_limits<SequenceNumber>::max();
    SequenceValidator near_end(max - 2);
    CHECK_EQ(near_end.classify(max), SequenceStatus::Gap);
    CHECK_EQ(near_end.classify(max - 1), SequenceStatus::Expected);
    near_end.record_applied(max - 1);
    CHECK_EQ(near_end.next_expected(), max);
    CHECK_EQ(near_end.classify(max), SequenceStatus::Expected);
    near_end.record_applied(max);
    CHECK_EQ(near_end.last_applied(), max);
    CHECK(!near_end.next_expected());
    CHECK_EQ(near_end.classify(max), SequenceStatus::Duplicate);
    CHECK_EQ(near_end.classify(max - 1), SequenceStatus::OutOfOrder);
    CHECK_EQ(near_end.classify(0), SequenceStatus::Invalid);
    near_end.record_applied(0);
    CHECK_EQ(near_end.last_applied(), max);

    FeedHandler feed;
    CHECK_EQ(feed.process("18446744073709551615,1,10,100,5,Add,Buy\n").status,
             FeedStatus::Gap);
    CHECK_EQ(feed.last_applied(), 0U);
    CHECK(feed.book().is_consistent());
}

} // namespace

int main() {
    test_dispatch_and_sequence_anomalies();
    test_gap_and_stale_precedence();
    test_malformed_messages();
    test_book_rejection();
    test_sell_dispatch_and_full_trade();
    test_unknown_order_rejection();
    test_allocation_failures();
    test_sequence_boundaries();
    return test::failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
