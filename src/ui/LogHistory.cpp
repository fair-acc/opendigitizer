#include "LogHistory.hpp"

#include <chrono>
#include <cstddef>
#include <memory>

namespace DigitizerUi {

namespace {
[[nodiscard]] bool isWarningOrWorse(gr::log::Level level) noexcept { return level <= gr::log::Level::warning; }
} // namespace

bool LogHistory::store(const gr::log::LogRecord& record) noexcept {
    std::unique_lock lock(_mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        _dropped.fetch_add(1UZ, std::memory_order_relaxed);
        return false;
    }
    _records.push_back(record);
    ++_counts[static_cast<std::size_t>(record.level)];
    if (isWarningOrWorse(record.level)) {
        _latestWarningOrWorse = record;
    }
    return true;
}

bool LogHistory::publish(const gr::log::LogRecord& record) noexcept {
    const bool forwarded = _next.publish(record);
    std::ignore          = store(record);
    return forwarded;
}

void LogHistory::record(gr::log::Level level, std::string_view text) noexcept {
    gr::log::LogRecord logRecord{};
    logRecord.level          = level;
    logRecord.timestampNanos = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); // GR4 stamps its records with system_clock too
    logRecord.textTruncated  = gr::log::storeBounded(logRecord.text, logRecord.textLength, text);
    std::ignore              = store(logRecord);
}

std::vector<gr::log::LogRecord> LogHistory::snapshot() const {
    std::scoped_lock lock(_mutex);
    return {_records.begin(), _records.end()};
}

std::optional<gr::log::LogRecord> LogHistory::latestWarningOrWorse() const {
    std::scoped_lock lock(_mutex);
    return _latestWarningOrWorse;
}

std::array<std::uint64_t, LogHistory::kLevels> LogHistory::counts() const {
    std::scoped_lock lock(_mutex);
    return _counts;
}

void LogHistory::clear() {
    std::scoped_lock lock(_mutex);
    _records.reset();
    _counts = {};
    _latestWarningOrWorse.reset();
    _dropped.store(0UZ, std::memory_order_relaxed);
}

LogHistory& logHistory() {
    // constructed once and never destroyed: threads may still log while static objects are torn down at exit
    alignas(LogHistory) static std::byte storage[sizeof(LogHistory)];
    static LogHistory* const             history = [] {
        LogHistory* constructed = std::construct_at(reinterpret_cast<LogHistory*>(storage), gr::log::detail::activeBackend());
        gr::log::setBackend(constructed);
        return constructed;
    }();
    return *history;
}

} // namespace DigitizerUi
