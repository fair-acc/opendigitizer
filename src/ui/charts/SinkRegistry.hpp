#ifndef OPENDIGITIZER_CHARTS_SINKREGISTRY_HPP
#define OPENDIGITIZER_CHARTS_SINKREGISTRY_HPP

#include "SignalSink.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gr::od::charts {

/// owns the SignalSinks and finds them by name; charts hold shared_ptrs to them; every operation takes one mutex
struct SinkRegistry {
    using Listener = std::function<void(SignalSink&, bool /*isAdded*/)>;

    mutable std::mutex                                           _mutex;
    std::unordered_map<std::string, std::shared_ptr<SignalSink>> _sinks;
    std::unordered_map<void*, Listener>                          _listeners;

    SinkRegistry() = default;
    ~SinkRegistry() { clear(); }

    SinkRegistry(const SinkRegistry&)            = delete;
    SinkRegistry& operator=(const SinkRegistry&) = delete;

    static SinkRegistry& instance() {
        static SinkRegistry registry;
        return registry;
    }

    bool registerSink(std::shared_ptr<SignalSink> sink) {
        if (!sink) {
            return false;
        }

        std::shared_ptr<SignalSink> sinkToNotify;
        std::vector<Listener>       listenersSnapshot;
        bool                        inserted = false;
        {
            std::lock_guard lock(_mutex);
            std::string     name(sink->uniqueName());
            auto [it, didInsert] = _sinks.try_emplace(name, std::move(sink));
            inserted             = didInsert;
            if (inserted) {
                sinkToNotify      = it->second;
                listenersSnapshot = snapshotListeners();
            }
        }

        if (inserted) {
            for (const auto& listener : listenersSnapshot) {
                listener(*sinkToNotify, true);
            }
        }

        return inserted;
    }

    bool unregisterSink(std::string_view uniqueName) {
        std::shared_ptr<SignalSink> sinkToNotify;
        std::vector<Listener>       listenersSnapshot;
        {
            std::lock_guard lock(_mutex);
            auto            it = _sinks.find(std::string(uniqueName));
            if (it == _sinks.end()) {
                return false;
            }
            sinkToNotify      = it->second;
            listenersSnapshot = snapshotListeners();
            _sinks.erase(it);
        }

        for (const auto& listener : listenersSnapshot) {
            listener(*sinkToNotify, false);
        }
        return true;
    }

    [[nodiscard]] std::shared_ptr<SignalSink> getSink(std::string_view uniqueName) const {
        std::lock_guard lock(_mutex);

        const auto it = _sinks.find(std::string(uniqueName));
        return it != _sinks.end() ? it->second : nullptr;
    }

    template<typename Pred>
    [[nodiscard]] std::shared_ptr<SignalSink> findSink(Pred predicate) const {
        std::lock_guard lock(_mutex);

        const auto sinks = _sinks | std::views::values;
        const auto found = std::ranges::find_if(sinks, [&predicate](const auto& sink) { return predicate(*sink); });
        return found != sinks.end() ? *found : nullptr;
    }

    template<typename Pred>
    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>> findSinks(Pred predicate) const {
        std::lock_guard lock(_mutex);

        return _sinks | std::views::values | std::views::filter([&predicate](const auto& sink) { return predicate(*sink); }) | std::ranges::to<std::vector>();
    }

    [[nodiscard]] std::size_t sinkCount() const {
        std::lock_guard lock(_mutex);
        return _sinks.size();
    }

    void clear() {
        std::vector<std::shared_ptr<SignalSink>> sinksToNotify;
        std::vector<Listener>                    listenersSnapshot;
        {
            std::lock_guard lock(_mutex);
            listenersSnapshot = snapshotListeners();
            sinksToNotify     = _sinks | std::views::values | std::ranges::to<std::vector>();
            _sinks.clear();
        }

        for (const auto& sink : sinksToNotify) {
            for (const auto& listener : listenersSnapshot) {
                listener(*sink, false);
            }
        }
    }

    void addListener(void* owner, Listener listener) {
        std::lock_guard lock(_mutex);
        _listeners[owner] = std::move(listener);
    }

    void removeListener(void* owner) {
        std::lock_guard lock(_mutex);
        _listeners.erase(owner);
    }

    template<typename Fn>
    void forEach(Fn&& fn) const {
        std::vector<std::shared_ptr<SignalSink>> sinksSnapshot;
        {
            std::lock_guard lock(_mutex);
            sinksSnapshot = _sinks | std::views::values | std::ranges::to<std::vector>();
        }
        for (const auto& sink : sinksSnapshot) {
            fn(*sink);
        }
    }

    [[nodiscard]] std::vector<Listener> snapshotListeners() const { return _listeners | std::views::values | std::ranges::to<std::vector>(); }
};

} // namespace gr::od::charts

#endif // OPENDIGITIZER_CHARTS_SINKREGISTRY_HPP
