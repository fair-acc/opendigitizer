#include "LogHistory.hpp"

#include <boost/ut.hpp>

#include <atomic>
#include <format>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace boost::ut;
using gr::log::Level;

namespace {

struct CountingBackend final : gr::log::Backend {
    std::atomic<std::size_t> published{0UZ};
    std::atomic<std::size_t> flushed{0UZ};

    bool publish(const gr::log::LogRecord&) noexcept override {
        ++published;
        return true;
    }
    std::size_t flush() noexcept override {
        ++flushed;
        return 0UZ;
    }
};

gr::log::LogRecord makeRecord(Level level, std::string_view text) {
    gr::log::LogRecord record{};
    record.level = level;
    std::ignore  = gr::log::storeBounded(record.text, record.textLength, text);
    return record;
}

std::string textOf(const gr::log::LogRecord& record) { return std::string(record.text, record.textLength); }

std::uint64_t countOf(const DigitizerUi::LogHistory& history, Level level) { return history.counts()[static_cast<std::size_t>(level)]; }

const suite<"LogHistory"> _logHistory = [] {
    "every record reaches the next backend and is kept in order"_test = [] {
        CountingBackend         console;
        DigitizerUi::LogHistory history(console);
        std::ignore = history.publish(makeRecord(Level::warning, "first"));
        std::ignore = history.publish(makeRecord(Level::info, "second"));
        std::ignore = history.publish(makeRecord(Level::error, "third"));

        expect(eq(console.published.load(), 3UZ));
        const auto records = history.snapshot();
        expect(eq(records.size(), 3UZ)) << fatal;
        expect(textOf(records[0]) == "first" && textOf(records[1]) == "second" && textOf(records[2]) == "third");
        expect(eq(countOf(history, Level::warning), 1UZ) && eq(countOf(history, Level::info), 1UZ) && eq(countOf(history, Level::error), 1UZ));
        history.flush();
        expect(eq(console.flushed.load(), 1UZ)) << "flush is forwarded";
    };

    "the bar's entry is the latest warning or worse, not a later info or debug record"_test = [] {
        CountingBackend         console;
        DigitizerUi::LogHistory history(console);
        expect(!history.latestWarningOrWorse().has_value());
        std::ignore = history.publish(makeRecord(Level::warning, "low disk"));
        std::ignore = history.publish(makeRecord(Level::info, "started"));
        std::ignore = history.publish(makeRecord(Level::debug, "chunk"));
        expect(history.latestWarningOrWorse().has_value() && textOf(*history.latestWarningOrWorse()) == "low disk");
        std::ignore = history.publish(makeRecord(Level::failure, "device lost"));
        expect(textOf(*history.latestWarningOrWorse()) == "device lost");
    };

    "beyond the capacity the oldest records go, the newest stay in order"_test = [] {
        CountingBackend         console;
        DigitizerUi::LogHistory history(console);
        const std::size_t       total = DigitizerUi::LogHistory::kCapacity + 44UZ;
        for (std::size_t i = 0UZ; i < total; ++i) {
            std::ignore = history.publish(makeRecord(Level::warning, std::format("{}", i)));
        }
        const auto records = history.snapshot();
        expect(eq(records.size(), DigitizerUi::LogHistory::kCapacity)) << fatal;
        expect(textOf(records.front()) == "44") << textOf(records.front());
        expect(textOf(records.back()) == std::format("{}", total - 1UZ)) << textOf(records.back());
        expect(eq(countOf(history, Level::warning), total)) << "counts include the records no longer retained";
    };

    "clear empties the history, the counts and the bar's entry"_test = [] {
        CountingBackend         console;
        DigitizerUi::LogHistory history(console);
        std::ignore = history.publish(makeRecord(Level::error, "x"));
        history.clear();
        expect(history.snapshot().empty());
        expect(eq(countOf(history, Level::error), 0UZ));
        expect(!history.latestWarningOrWorse().has_value());
    };

    "records published from several threads all reach the console; the history keeps or counts each as dropped"_test = [] {
        CountingBackend          console;
        DigitizerUi::LogHistory  history(console);
        constexpr std::size_t    kThreads   = 4UZ;
        constexpr std::size_t    kPerThread = 1000UZ;
        std::vector<std::thread> threads;
        for (std::size_t t = 0UZ; t < kThreads; ++t) {
            threads.emplace_back([&history, t] {
                for (std::size_t i = 0UZ; i < kPerThread; ++i) {
                    std::ignore = history.publish(makeRecord(Level::warning, std::format("{}:{}", t, i)));
                }
            });
        }
        for (std::size_t i = 0UZ; i < 100UZ; ++i) {
            std::ignore = history.snapshot();
        }
        for (auto& thread : threads) {
            thread.join();
        }
        expect(eq(console.published.load(), kThreads * kPerThread));
        expect(eq(countOf(history, Level::warning) + history.dropped(), kThreads * kPerThread));
    };

    "the process-wide history sits in front of the active backend and sees gr::log records"_test = [] {
        DigitizerUi::LogHistory& history = DigitizerUi::logHistory();
        history.clear();
        gr::log::warning("status bar test {}", 42);
        const auto records = history.snapshot();
        expect(eq(records.size(), 1UZ)) << fatal;
        expect(textOf(records[0]) == "status bar test 42");
        expect(&DigitizerUi::logHistory() == &history) << "installed once";
    };
};

} // namespace

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
