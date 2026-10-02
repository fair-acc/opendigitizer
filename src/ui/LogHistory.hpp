#ifndef OPENDIGITIZER_UI_LOGHISTORY_HPP
#define OPENDIGITIZER_UI_LOGHISTORY_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include <gnuradio-4.0/Logger.hpp>

namespace DigitizerUi {

/// GR4 log backend that forwards every record to the backend it was put in front of (by default the console) and keeps
/// the most recent ones for display. Publishing never blocks: a record that arrives while the history is being read is
/// still forwarded and is counted as dropped from the history.
/// @code
/// gr::log::warning("rate {} Hz", rate);                // reaches the console and DigitizerUi::logHistory()
/// for (const auto& record : DigitizerUi::logHistory().snapshot()) { ... }
/// @endcode
class LogHistory final : public gr::log::Backend {
public:
    static constexpr std::size_t kCapacity = 256UZ;
    static constexpr std::size_t kLevels   = static_cast<std::size_t>(gr::log::Level::trace) + 1UZ;

    explicit LogHistory(gr::log::Backend& next) noexcept : _next(next) {}

    bool        publish(const gr::log::LogRecord& record) noexcept override;
    std::size_t drain(gr::log::RecordConsumer consumer, void* user) noexcept override { return _next.drain(consumer, user); }
    std::size_t flush() noexcept override { return _next.flush(); }

    /// keeps a record without forwarding it, e.g. a UI notification
    void record(gr::log::Level level, std::string_view text) noexcept;

    [[nodiscard]] std::vector<gr::log::LogRecord>    snapshot() const; // oldest first
    [[nodiscard]] std::optional<gr::log::LogRecord>  latestWarningOrWorse() const;
    [[nodiscard]] std::array<std::uint64_t, kLevels> counts() const; // since the last clear()
    [[nodiscard]] std::uint64_t                      dropped() const noexcept { return _dropped.load(std::memory_order_relaxed); }
    void                                             clear();

private:
    gr::log::Backend&                         _next;
    mutable std::mutex                        _mutex;
    std::array<gr::log::LogRecord, kCapacity> _records{};
    std::size_t                               _oldest = 0UZ;
    std::size_t                               _size   = 0UZ;
    std::array<std::uint64_t, kLevels>        _counts{};
    std::optional<gr::log::LogRecord>         _latestWarningOrWorse;
    std::atomic<std::uint64_t>                _dropped{0UZ};

    bool store(const gr::log::LogRecord& record) noexcept;
};

/// the process-wide history, put in front of the active GR4 log backend on first use and kept for the process lifetime
/// (worker threads may publish at any time, so it is never removed)
[[nodiscard]] LogHistory& logHistory();

} // namespace DigitizerUi

#endif
