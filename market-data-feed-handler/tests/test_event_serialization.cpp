#include <cstdlib>
#include <fstream>
#include <iterator>
#include <locale>
#include <random>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include "hft/market_data/event_writer.hpp"
#include "hft/market_data/replay_reader.hpp"
#include "hft/market_data/synthetic_generator.hpp"
#include "test_helpers.hpp"

namespace {

using namespace hft;
using namespace hft::market_data;
namespace fs = std::filesystem;

class TestDirectory {
public:
    TestDirectory() {
        const auto parent = fs::temp_directory_path();
        std::random_device random;
        for (int attempt = 0; attempt < 10; ++attempt) {
            path = parent / ("hft-stage2-" + std::to_string(random()));
            if (fs::create_directory(path)) {
                return;
            }
        }
        throw std::runtime_error("Could not create a temporary test directory");
    }

    ~TestDirectory() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path path;
};

class GroupedNumbers : public std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};

template <typename Operation>
void check_error(Operation operation, std::string_view expected_text) {
    try {
        operation();
        CHECK(false);
    } catch (const std::runtime_error& error) {
        CHECK(std::string_view(error.what()).find(expected_text) != std::string_view::npos);
    }
}

void write_fixture(const fs::path& path, std::string_view contents) {
    std::ofstream output(path, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << contents;
    output.close();
}

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

auto fields(const MarketEvent& event) {
    return std::tie(event.sequence_number, event.timestamp, event.order_id, event.price,
                    event.quantity, event.event_type, event.side);
}

std::vector<MarketEvent> generate(GeneratorConfig config) {
    SyntheticGenerator generator(config);
    std::vector<MarketEvent> events;
    events.reserve(100'000);
    for (std::size_t index = 0; index < 100'000; ++index) {
        events.push_back(generator.next());
    }
    return events;
}

void test_round_trip(const fs::path& directory) {
    constexpr GeneratorConfig config{256, -12'000, 45, 70};
    const auto events = generate(config);
    const auto first = directory / "first.csv";
    const auto second = directory / "second.csv";
    write_events(first, events);

    const auto previous_locale = std::locale::global(std::locale(std::locale::classic(),
                                                                new GroupedNumbers));
    try {
        write_events(second, generate(config));
    } catch (...) {
        std::locale::global(previous_locale);
        throw;
    }
    std::locale::global(previous_locale);
    CHECK_EQ(read_bytes(first), read_bytes(second));

    ReplayReader reader(first);
    for (const auto& expected : events) {
        const auto replayed = reader.next();
        CHECK(replayed.has_value());
        if (!replayed) {
            return;
        }
        CHECK_EQ(fields(*replayed), fields(expected));
        CHECK_EQ(replayed->reserved[0], 0);
        CHECK_EQ(replayed->reserved[1], 0);
    }
    CHECK(!reader.next());

    constexpr MarketEvent fixture[]{MarketEvent{9, 1010, 87, -1200, 12, EventType::Trade, Side::Sell}};
    write_events(second, fixture);
    CHECK_EQ(read_bytes(second), "9,1010,87,-1200,12,Trade,Sell\n");
}

void test_empty_and_malformed(const fs::path& directory) {
    const auto empty = directory / "empty.csv";
    write_events(empty, {});
    CHECK_EQ(fs::file_size(empty), std::uintmax_t{0});
    ReplayReader empty_reader(empty);
    CHECK(!empty_reader.next());
    CHECK(!empty_reader.next());

    const auto malformed = directory / "malformed.csv";
    for (const std::string_view bad_record : {
             "\n", "1,1,1,100,5,Add\n", "1,1,1,100,5,Add,Buy,extra\n",
             "18446744073709551616,1,1,100,5,Add,Buy\n", "0,1,1,100,5,Add,Buy\n",
             "1,1,0,100,5,Add,Buy\n", "1,,1,100,5,Add,Buy\n",
             "1,1,1,100x,5,Add,Buy\n", "1,1,1,100,-5,Add,Buy\n",
             "1,1,1,100,4294967296,Add,Buy\n", "1,1,1,100,5,Invalid,Buy\n",
             "1,1,1,100,5,Add,Invalid\n", "1,1,1,100,0,Trade,Buy\n",
             "1,1,1,100,5,Cancel,Buy\n", "1,1,1,100,5,Add,Buy"}) {
        write_fixture(malformed, std::string("1,1,1,100,5,Add,Buy\n") + std::string(bad_record));
        ReplayReader reader(malformed);
        CHECK(reader.next().has_value());
        check_error([&] { reader.next(); }, "malformed.csv:2:");
    }
}

void test_io_failures(const fs::path& directory) {
    const auto missing = directory / "missing" / "events.csv";
    check_error([&] { ReplayReader reader(missing); }, "for reading");
    check_error([&] { write_events(missing, {}); }, "for writing");

    const fs::path full("/dev/full");
    if (fs::exists(full) && fs::is_character_file(full)) {
        constexpr MarketEvent events[]{MarketEvent{1, 1, 1, 100, 1}};
        check_error([&] { write_events(full, events); }, "writing market event file");
    }
}

} // namespace

int main() {
    const TestDirectory directory;
    test_round_trip(directory.path);
    test_empty_and_malformed(directory.path);
    test_io_failures(directory.path);
    return test::failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
