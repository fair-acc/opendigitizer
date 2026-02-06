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

namespace opendigitizer::charts {

/**
 * @brief Global registry for all SignalSink instances.
 *
 * Manages the lifetime of signal sinks and provides lookup by name.
 * Charts hold shared_ptr references to sinks from this registry.
 *
 * Thread-safe: all operations are protected by mutex.
 */
class SinkRegistry {
public:
    using Listener = std::function<void(SignalSink&, bool /*isAdded*/)>;

    static SinkRegistry& instance() {
        static SinkRegistry s_instance;
        return s_instance;
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

        auto it = _sinks.find(std::string(uniqueName));
        if (it != _sinks.end()) {
            return it->second;
        }

        return nullptr;
    }

    template<typename Pred>
    [[nodiscard]] std::shared_ptr<SignalSink> findSink(Pred predicate) const {
        std::lock_guard lock(_mutex);

        for (const auto& [_, sink] : _sinks) {
            if (predicate(*sink)) {
                return sink;
            }
        }

        return nullptr;
    }

    template<typename Pred>
    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>> findSinks(Pred predicate) const {
        std::lock_guard lock(_mutex);

        std::vector<std::shared_ptr<SignalSink>> result;
        for (const auto& [_, sink] : _sinks) {
            if (predicate(*sink)) {
                result.push_back(sink);
            }
        }

        return result;
    }

    [[nodiscard]] bool hasSink(std::string_view uniqueName) const {
        std::lock_guard lock(_mutex);
        return _sinks.contains(std::string(uniqueName));
    }

    [[nodiscard]] std::vector<std::string> sinkNames() const {
        std::lock_guard lock(_mutex);

        std::vector<std::string> names;
        names.reserve(_sinks.size());
        for (const auto& [name, _] : _sinks) {
            names.push_back(name);
        }

        return names;
    }

    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>> allSinks() const {
        std::lock_guard lock(_mutex);

        std::vector<std::shared_ptr<SignalSink>> sinks;
        sinks.reserve(_sinks.size());
        for (const auto& [_, sink] : _sinks) {
            sinks.push_back(sink);
        }

        return sinks;
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
            sinksToNotify.reserve(_sinks.size());
            for (auto& [_, sink] : _sinks) {
                sinksToNotify.push_back(sink);
            }
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
            sinksSnapshot.reserve(_sinks.size());
            for (const auto& [_, sink] : _sinks) {
                sinksSnapshot.push_back(sink);
            }
        }
        for (const auto& sink : sinksSnapshot) {
            fn(*sink);
        }
    }

private:
    SinkRegistry() = default;
    ~SinkRegistry() { clear(); }

    SinkRegistry(const SinkRegistry&)            = delete;
    SinkRegistry& operator=(const SinkRegistry&) = delete;

    [[nodiscard]] std::vector<Listener> snapshotListeners() const {
        std::vector<Listener> snapshot;
        snapshot.reserve(_listeners.size());
        for (const auto& [_, listener] : _listeners) {
            snapshot.push_back(listener);
        }
        return snapshot;
    }

    mutable std::mutex                                           _mutex;
    std::unordered_map<std::string, std::shared_ptr<SignalSink>> _sinks;
    std::unordered_map<void*, Listener>                          _listeners;
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_SINKREGISTRY_HPP
