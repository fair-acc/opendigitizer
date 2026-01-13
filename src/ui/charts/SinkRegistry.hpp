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

    /**
     * @brief Get the singleton instance.
     */
    static SinkRegistry& instance() {
        static SinkRegistry s_instance;
        return s_instance;
    }

    /**
     * @brief Register a sink.
     * @param sink The sink to register.
     * @return true if registered, false if a sink with the same name already exists.
     */
    bool registerSink(std::shared_ptr<SignalSink> sink) {
        if (!sink) {
            return false;
        }

        std::lock_guard lock(_mutex);

        std::string name(sink->uniqueName());
        auto [it, inserted] = _sinks.try_emplace(name, std::move(sink));

        if (inserted) {
            notifyListeners(*it->second, true);
        }

        return inserted;
    }

    /**
     * @brief Unregister a sink by name.
     * @param uniqueName The unique name of the sink.
     * @return true if removed, false if not found.
     */
    bool unregisterSink(std::string_view uniqueName) {
        std::lock_guard lock(_mutex);

        auto it = _sinks.find(std::string(uniqueName));
        if (it != _sinks.end()) {
            notifyListeners(*it->second, false);
            _sinks.erase(it);
            return true;
        }

        return false;
    }

    /**
     * @brief Get a sink by unique name.
     * @param uniqueName The unique name of the sink.
     * @return Shared pointer to the sink, or nullptr if not found.
     */
    [[nodiscard]] std::shared_ptr<SignalSink> getSink(std::string_view uniqueName) const {
        std::lock_guard lock(_mutex);

        auto it = _sinks.find(std::string(uniqueName));
        if (it != _sinks.end()) {
            return it->second;
        }

        return nullptr;
    }

    /**
     * @brief Find a sink matching a predicate.
     * @param predicate Function taking SignalSink& and returning bool.
     * @return Shared pointer to the first matching sink, or nullptr if not found.
     */
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

    /**
     * @brief Find all sinks matching a predicate.
     * @param predicate Function taking SignalSink& and returning bool.
     * @return Vector of shared pointers to matching sinks.
     */
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

    /**
     * @brief Check if a sink exists.
     * @param uniqueName The unique name to check.
     */
    [[nodiscard]] bool hasSink(std::string_view uniqueName) const {
        std::lock_guard lock(_mutex);
        return _sinks.contains(std::string(uniqueName));
    }

    /**
     * @brief Get all registered sink names.
     */
    [[nodiscard]] std::vector<std::string> sinkNames() const {
        std::lock_guard lock(_mutex);

        std::vector<std::string> names;
        names.reserve(_sinks.size());
        for (const auto& [name, _] : _sinks) {
            names.push_back(name);
        }

        return names;
    }

    /**
     * @brief Get all registered sinks.
     */
    [[nodiscard]] std::vector<std::shared_ptr<SignalSink>> allSinks() const {
        std::lock_guard lock(_mutex);

        std::vector<std::shared_ptr<SignalSink>> sinks;
        sinks.reserve(_sinks.size());
        for (const auto& [_, sink] : _sinks) {
            sinks.push_back(sink);
        }

        return sinks;
    }

    /**
     * @brief Get the number of registered sinks.
     */
    [[nodiscard]] std::size_t sinkCount() const {
        std::lock_guard lock(_mutex);
        return _sinks.size();
    }

    /**
     * @brief Clear all registered sinks.
     */
    void clear() {
        std::lock_guard lock(_mutex);

        for (auto& [_, sink] : _sinks) {
            notifyListeners(*sink, false);
        }
        _sinks.clear();
    }

    /**
     * @brief Add a listener for sink registration events.
     * @param owner Identifier for the listener (used for removal).
     * @param listener Callback function.
     */
    void addListener(void* owner, Listener listener) {
        std::lock_guard lock(_mutex);
        _listeners[owner] = std::move(listener);
    }

    /**
     * @brief Remove a listener.
     * @param owner The identifier used when adding.
     */
    void removeListener(void* owner) {
        std::lock_guard lock(_mutex);
        _listeners.erase(owner);
    }

    /**
     * @brief Iterate over all sinks (with lock held).
     * @param fn Function to call for each sink.
     */
    template<typename Fn>
    void forEach(Fn&& fn) const {
        std::lock_guard lock(_mutex);
        for (const auto& [_, sink] : _sinks) {
            fn(*sink);
        }
    }

private:
    SinkRegistry() = default;
    ~SinkRegistry() { clear(); }

    SinkRegistry(const SinkRegistry&)            = delete;
    SinkRegistry& operator=(const SinkRegistry&) = delete;

    void notifyListeners(SignalSink& sink, bool isAdded) {
        // Called while mutex is held
        for (const auto& [_, listener] : _listeners) {
            listener(sink, isAdded);
        }
    }

    mutable std::mutex                                           _mutex;
    std::unordered_map<std::string, std::shared_ptr<SignalSink>> _sinks;
    std::unordered_map<void*, Listener>                          _listeners;
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_SINKREGISTRY_HPP
