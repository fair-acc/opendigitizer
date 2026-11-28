#ifndef OPENDIGITIZER_REMOTESOURCE_HPP
#define OPENDIGITIZER_REMOTESOURCE_HPP

#include <algorithm>
#include <charconv>
#include <format>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/algorithm/dataset/DataSetHelper.hpp>

#include <daq_api.hpp>

#include <opencmw.hpp>

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#ifndef __EMSCRIPTEN__
#include <openssl/ssl.h>
#endif

#include "settings.hpp"
#include "stdex/cast.hpp"

#include "../utils/TransparentStringHash.hpp"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wuseless-cast"
#endif

namespace gr::od {

inline opencmw::URI<> resolveRelativeTopic(const std::string& remote, const std::string& base) {
    auto pathUrl = opencmw::URI<>(remote);
    if (!pathUrl.hostName().has_value() || pathUrl.hostName()->empty()) {
        auto baseUrl = opencmw::URI<>(base.empty() ? "https://localhost:8080" : base);
        auto result  = opencmw::URI<>::UriFactory().scheme(baseUrl.scheme().value()).authority(baseUrl.authority().value()).path(pathUrl.path().value_or("")).queryParam(pathUrl.queryParam().value_or("")).fragment(pathUrl.fragment().value_or("")).build();
        return result;
    }
    return pathUrl;
}

struct RemoteSourceInfo {
    std::string uniqueName;
    std::string typeName;
    const void* block = nullptr;
};

struct RemoteSourceBase {
    std::string remote_uri;
    std::string host = "ADDA";
};

class RemoteSubscriptions;
namespace detail {
struct RemoteSourceSubscription;
}

class RemoteSubscriptionHandle {
    detail::RemoteSourceSubscription* _subscription = nullptr;
    std::size_t                       _id           = 0;
    explicit RemoteSubscriptionHandle(detail::RemoteSourceSubscription* subscription, std::size_t id) : _subscription(subscription), _id(id) {}

public:
    friend class RemoteSubscriptions;

    constexpr RemoteSubscriptionHandle() = default;
    constexpr explicit operator bool() const { return _subscription; }
    RemoteSubscriptionHandle(const RemoteSubscriptionHandle&)            = delete;
    RemoteSubscriptionHandle& operator=(const RemoteSubscriptionHandle&) = delete;

    constexpr RemoteSubscriptionHandle(RemoteSubscriptionHandle&& other) noexcept : _subscription(std::exchange(other._subscription, nullptr)), _id(std::exchange(other._id, 0)) {}
    constexpr RemoteSubscriptionHandle& operator=(RemoteSubscriptionHandle&& other) noexcept {
        _subscription = std::exchange(other._subscription, nullptr);
        _id           = std::exchange(other._id, 0);
        return *this;
    }

    ~RemoteSubscriptionHandle() { reset(); }
    void reset() noexcept;

    /// safe to call during the user's callback: it does not modify subscriptions
    void reconnect() noexcept;
};

template<typename Derived, typename QueueContents>
struct RemoteSourceCommon {
    struct Queue {
        std::deque<QueueContents> data;
        std::mutex                mutex;
    };

    RemoteSubscriptionHandle   _subscription;
    std::shared_ptr<Queue>     _queue = std::make_shared<Queue>();
    std::atomic<std::uint64_t> _reconnect{0ULL}; // 0 == disabled, otherwise time since epoch in ns

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& newSettings);
    void stopSubscription() { _subscription.reset(); }
    void startSubscription();
    void start();
    void stop() { stopSubscription(); }
    void maybeReconnect() {
        const auto*         self        = static_cast<Derived*>(this);
        std::uint64_t       reconnectNs = _reconnect.load(std::memory_order_seq_cst);
        const std::uint64_t nowNs       = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        if (reconnectNs != 0ULL && reconnectNs < nowNs && !self->host.empty() && !self->remote_uri.empty() && _reconnect.compare_exchange_strong(reconnectNs, 0ULL, std::memory_order_seq_cst)) {
            if (_subscription) {
                _subscription.reconnect();
            } else {
                startSubscription();
            }
        }
    }
};

namespace detail {
struct RemoteSourceSubscription {
    /// a valid RemoteSubscriptionHandle returned from a Callback closes the subscription after the callback
    using Callback = std::function<RemoteSubscriptionHandle(const opencmw::mdp::Message&)>;

    const std::string                         subscribedUri;
    mutable std::mutex                        callbacksMutex;
    std::unordered_map<std::size_t, Callback> userCallbacks;

    explicit RemoteSourceSubscription(std::string_view uri) : subscribedUri(uri) {}
    ~RemoteSourceSubscription() { disconnect(); }

    [[nodiscard]] std::size_t pushCallback(Callback&& callback) {
        static std::size_t lastUsedId = 0;
        const auto         guard      = std::scoped_lock{callbacksMutex};
        std::size_t        id         = lastUsedId;
        ++lastUsedId;
        [[maybe_unused]] const auto wasEmplaced = userCallbacks.try_emplace(id, std::move(callback)).second;
        assert(wasEmplaced);
        return id;
    }

    void removeCallback(std::size_t id) {
        const auto guard = std::scoped_lock{callbacksMutex};
        userCallbacks.erase(id);
    }

    void reconnect() {
        disconnect();
        start();
    }

    void start();

    void                     disconnect();
    opencmw::client::Command buildSubscribeCommand();
};
} // namespace detail

class RemoteSubscriptions {
    using Subscription = detail::RemoteSourceSubscription;

private:
#ifndef __EMSCRIPTEN__
    // OpenSSL's atexit cleanup must run after this static joins the client's TLS worker, i.e. be registered before it
    [[maybe_unused]] bool _sslInitialised = OPENSSL_init_ssl(0, nullptr) == 1;
#endif
    // mutual exclusion between subscribing/unsubscribing (writing), and the rest client worker thread calling the callback (reading)
    mutable std::shared_mutex                                                                              _mutex;
    std::unordered_map<std::string, std::unique_ptr<Subscription>, TransparentStringHash, std::equal_to<>> _subscriptions;
    opencmw::client::RestClient                                                                            _client = opencmw::client::RestClient(opencmw::client::VerifyServerCertificates(digitizer::Settings::instance().checkCertificates));

    static RemoteSubscriptions& instance();

public:
    [[nodiscard]] static RemoteSubscriptionHandle subscribe(const RemoteSourceBase& source, Subscription::Callback&& callback) {
        auto&      instance = RemoteSubscriptions::instance();
        auto       uri      = resolveRelativeTopic(source.remote_uri, source.host).str();
        const auto guard    = std::unique_lock{instance._mutex}; // reading + potentially writing operation

        auto       subscriptionIter                   = instance._subscriptions.find(uri);
        const bool noOtherSubscriptionsOnThisUriExist = subscriptionIter == std::end(instance._subscriptions);
        if (noOtherSubscriptionsOnThisUriExist) {
            auto [newIter, _] = instance._subscriptions.try_emplace(uri, std::make_unique<Subscription>(uri));
            subscriptionIter  = newIter;
        }

        Subscription*     subscription = subscriptionIter->second.get();
        const std::size_t id           = subscription->pushCallback(std::move(callback));

        if (noOtherSubscriptionsOnThisUriExist) {
            subscription->start();
        }

        return RemoteSubscriptionHandle(subscription, id);
    }

    friend class RemoteSubscriptionHandle;
    friend struct detail::RemoteSourceSubscription;
};

inline RemoteSubscriptions& RemoteSubscriptions::instance() {
    static RemoteSubscriptions manager;
    return manager;
}

inline opencmw::client::Command detail::RemoteSourceSubscription::buildSubscribeCommand() {
    opencmw::client::Command result;
    result.command = opencmw::mdp::Command::Subscribe;
    result.topic   = opencmw::URI<>(subscribedUri);

    result.callback = [uri = subscribedUri](const opencmw::mdp::Message& response) {
        const auto&                           instance         = RemoteSubscriptions::instance();
        auto                                  guard            = std::shared_lock{instance._mutex};
        const auto                            subscriptionIter = std::as_const(instance._subscriptions).find(uri);
        std::vector<RemoteSubscriptionHandle> queuedUnsubscriptions;
        if (subscriptionIter != std::end(instance._subscriptions)) {
            const RemoteSourceSubscription& subscription      = *subscriptionIter->second;
            const auto                      subscriptionGuard = std::lock_guard{subscription.callbacksMutex};
            for (auto& [_, callback] : subscription.userCallbacks) {
                if (auto unsubscription = callback(response)) {
                    queuedUnsubscriptions.emplace_back(std::move(unsubscription));
                }
            }
        }
        guard.unlock(); // queued unsubscriptions should be destroyed (and disconnected) after unlock
        queuedUnsubscriptions.clear();
    };

    return result;
}

inline void detail::RemoteSourceSubscription::start() { RemoteSubscriptions::instance()._client.request(buildSubscribeCommand()); }

inline void detail::RemoteSourceSubscription::disconnect() {
    auto&                    instance = RemoteSubscriptions::instance();
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Unsubscribe;
    command.topic    = opencmw::URI<>(subscribedUri);
    command.callback = [](const opencmw::mdp::Message&) {};
    instance._client.request(command);
}

inline void RemoteSubscriptionHandle::reconnect() noexcept {
    if (!*this) {
        return;
    }
    const auto guard = std::shared_lock{RemoteSubscriptions::instance()._mutex};
    _subscription->reconnect();
}

inline void RemoteSubscriptionHandle::reset() noexcept {
    if (!*this) {
        return;
    }
    // prevent subscribing during unsubscribe
    auto&      instance = RemoteSubscriptions::instance();
    const auto guard    = std::unique_lock{instance._mutex};

    _subscription->removeCallback(_id);
    if (_subscription->userCallbacks.empty()) {
        instance._subscriptions.erase(_subscription->subscribedUri);
    }
    _subscription = nullptr;
    _id           = 0;
}

struct RemoteSources {
    std::mutex                                                                    _mutex;
    std::unordered_map<std::string, RemoteSourceInfo>                             _knownRemoteSources;
    std::unordered_map<std::string, std::function<void(const RemoteSourceInfo&)>> _addingSourcesCallbacks;

    RemoteSources() = default;

    RemoteSources(const RemoteSources&)            = delete;
    RemoteSources& operator=(const RemoteSources&) = delete;

    static RemoteSources& instance() {
        static RemoteSources sources;
        return sources;
    }

    template<typename TBlock>
    void registerRemoteSource(TBlock* block) {
        std::lock_guard lock(_mutex);
        _knownRemoteSources[block->unique_name] = {.uniqueName = block->unique_name, .typeName = std::string(gr::meta::type_name<TBlock>()), .block = block};
    }

    template<typename TBlock>
    void unregisterRemoteSource(TBlock* block) {
        std::lock_guard lock(_mutex);
        _knownRemoteSources.erase(block->unique_name);
    }

    void setRemoteSourceAddedCallback(std::string remoteUri, std::function<void(const RemoteSourceInfo&)> callback) {
        std::lock_guard lock(_mutex);
        _addingSourcesCallbacks[std::move(remoteUri)] = std::move(callback);
    }

    void notifyOfRemoteSource(const std::string& remoteUri, const void* remoteSource) {
        std::lock_guard lock(_mutex);
        auto            callback = _addingSourcesCallbacks.find(remoteUri);
        if (callback == _addingSourcesCallbacks.end()) {
            return;
        }
        auto source = std::ranges::find_if(_knownRemoteSources, [remoteSource](const auto& known) { return known.second.block == remoteSource; });
        if (source != _knownRemoteSources.end()) {
            callback->second(source->second);
            _addingSourcesCallbacks.erase(callback);
        }
    }
};

template<typename Derived, typename QueueContents>
void RemoteSourceCommon<Derived, QueueContents>::startSubscription() {
    if (_subscription) {
        return;
    }
    auto*       derivedSelf = static_cast<Derived*>(this);
    const auto* baseSelf    = static_cast<RemoteSourceBase*>(derivedSelf);
    std::print("<<RemoteSource.hpp>> startSubscription {}\n", baseSelf->remote_uri);
    std::weak_ptr maybeQueue = _queue;
    _subscription            = RemoteSubscriptions::subscribe(*baseSelf, [maybeQueue, derivedSelf](const opencmw::mdp::Message& rep) -> RemoteSubscriptionHandle { //
        return derivedSelf->copyRestResponseDataIntoQueue(rep, maybeQueue);
    });
}

template<typename Derived, typename QueueContents>
void RemoteSourceCommon<Derived, QueueContents>::settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& newSettings) {
    static_assert(std::is_base_of_v<RemoteSourceBase, Derived>);
    static_assert(std::is_base_of_v<RemoteSourceCommon, Derived>);
    // GR doesn't set the state for a block added after the scheduler started
    auto* self = static_cast<RemoteSourceBase*>(static_cast<Derived*>(this));
    if ((newSettings.contains("host") || newSettings.contains("remote_uri")) && !self->host.empty() && !self->remote_uri.empty()) {
        RemoteSources::instance().notifyOfRemoteSource(self->remote_uri, static_cast<Derived*>(this));
        stopSubscription();
        startSubscription();
    }
}

template<typename Derived, typename QueueContents>
void RemoteSourceCommon<Derived, QueueContents>::start() {
    const auto* self = static_cast<RemoteSourceBase*>(static_cast<Derived*>(this));
    if (!self->remote_uri.empty() && !self->host.empty()) {
        startSubscription();
    }
}

struct RemoteStreamSourceData {
    opendigitizer::acq::Acquisition acq;
    std::size_t                     read = 0;
};

template<typename T>
requires std::is_floating_point_v<T> || gr::UncertainValueLike<T>
struct RemoteStreamSource : RemoteSourceBase, RemoteSourceCommon<RemoteStreamSource<T>, RemoteStreamSourceData>, gr::Block<RemoteStreamSource<T>> {
    using Parent = gr::Block<RemoteStreamSource<T>>;
    using Queue  = RemoteSourceCommon<RemoteStreamSource, RemoteStreamSourceData>::Queue;

    gr::PortOut<T> out;

    gr::Annotated<std::pmr::string, "signal name", gr::Visible, gr::Doc<"Identifier for the signal">>                                                                         signal_name;
    gr::Annotated<std::pmr::string, "signal quantity", gr::Visible, gr::Doc<"Physical quantity represented by the signal">>                                                   signal_quantity;
    gr::Annotated<std::pmr::string, "signal unit", gr::Visible, gr::Doc<"Unit of measurement for the signal values">>                                                         signal_unit;
    gr::Annotated<float, "signal min", gr::Doc<"Minimum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min        = std::numeric_limits<float>::lowest();
    gr::Annotated<float, "signal max", gr::Doc<"Maximum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max        = std::numeric_limits<float>::max();
    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">>                                                                                                          verbose_console   = false;
    gr::Annotated<float, "reconnect timeout", gr::Doc<"reconnect timeout in sec">>                                                                                            reconnect_timeout = 5.f;

    GR_MAKE_REFLECTABLE(RemoteStreamSource, out, remote_uri, signal_name, signal_unit, signal_quantity, signal_min, signal_max, host, verbose_console, reconnect_timeout);

    explicit RemoteStreamSource(gr::property_map props) : Parent(props) {
        RemoteSources::instance().registerRemoteSource(this);
        this->disconnect_on_done = false;
    }
    ~RemoteStreamSource() { RemoteSources::instance().unregisterRemoteSource(this); }

    void updateSettingsFromAcquisition(const opendigitizer::acq::Acquisition& acq) {
        if (acq.channelNames.size() != 1 || acq.channelUnits.size() != 1 || acq.channelQuantities.size() != 1 || acq.channelRangeMin.size() != 1 || acq.channelRangeMax.size() != 1) {
            this->emitErrorMessage("updateSettingsFromAcquisition(..)", gr::Error(std::format("Expected exactly one channel, but got {} names, {} units, {} quantities, {} range-min values, and {} range-max values.", //
                                                                            acq.channelNames.size(), acq.channelUnits.size(), acq.channelQuantities.size(), acq.channelRangeMin.size(), acq.channelRangeMax.size())));
        } else {
            if (signal_name != acq.channelNames[0] || signal_unit != acq.channelUnits[0] || signal_quantity != acq.channelQuantities[0] || signal_min != acq.channelRangeMin[0] || signal_max != acq.channelRangeMax[0]) {
                if (!acq.channelNames[0].empty()) {
                    if (const gr::property_map failed = this->settings().set({{"signal_name", acq.channelNames[0]}}); !failed.empty()) {
                        this->emitErrorMessage("updateSettingsFromAcquisition(..)", gr::Error(std::format("settings could not be applied: {}", gr::join(failed))));
                    }
                }

                if (!acq.channelUnits[0].empty()) {
                    if (const gr::property_map failed = this->settings().set({{"signal_unit", acq.channelUnits[0]}}); !failed.empty()) {
                        this->emitErrorMessage("updateSettingsFromAcquisition(..)", gr::Error(std::format("settings could not be applied: {}", gr::join(failed))));
                    }
                }

                if (!acq.channelQuantities[0].empty()) {
                    if (const gr::property_map failed = this->settings().set({{"signal_quantity", acq.channelQuantities[0]}}); !failed.empty()) {
                        this->emitErrorMessage("updateSettingsFromAcquisition(..)", gr::Error(std::format("settings could not be applied: {}", gr::join(failed))));
                    }
                }

                if (const gr::property_map failed = this->settings().set({{"signal_min", acq.channelRangeMin[0]}, {"signal_max", acq.channelRangeMax[0]}}); !failed.empty()) {
                    this->emitErrorMessage("updateSettingsFromAcquisition(..)", gr::Error(std::format("settings could not be applied: {}", gr::join(failed))));
                }
            }
        }
    }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        this->maybeReconnect();

        std::size_t     written = 0;
        std::lock_guard lock(this->_queue->mutex);
        while (written < output.size() && !this->_queue->data.empty()) {
            auto& d = this->_queue->data.front();

            const auto nSignals = static_cast<std::size_t>(d.acq.channelValues.n(0));
            const auto nSamples = static_cast<std::size_t>(d.acq.channelValues.n(1));
            if (nSignals == 0 || nSamples == 0) {
                this->_queue->data.pop_front();
                continue;
            }

            if (nSignals != 1) {
                this->emitErrorMessage("processBulk(..)", gr::Error(std::format("Expected exactly one channel, but got {} channelValues", nSignals)));
                this->_queue->data.pop_front();
                continue;
            }

            const auto valueCount = d.acq.channelValues.elements().size();
            if (valueCount != nSamples || d.read >= nSamples) {
                this->emitErrorMessage("processBulk(..)", gr::Error(std::format("Inconsistent acquisition: {} values, {} samples, read offset {}", valueCount, nSamples, d.read)));
                this->_queue->data.pop_front();
                continue;
            }

            updateSettingsFromAcquisition(d.acq);
            // only one signal is stored
            const auto nSamplesToCopy = std::min(output.size() - written, nSamples - d.read);
            auto       inValues       = std::span{d.acq.channelValues.elements()}.subspan(d.read, nSamplesToCopy);
            auto       outIt          = output.begin() + cast_to_signed(written);
            if constexpr (std::is_same_v<T, float>) {
                std::ranges::copy(inValues, outIt);
            } else if constexpr (gr::UncertainValueLike<T>) {
                if (d.acq.channelValues.elements().size() != d.acq.channelErrors.elements().size()) {
                    this->emitErrorMessage("subscriptionCallback(..)",                                                                                       //
                        gr::Error(std::format("Inconsistent data from '{}': Sample type is UncertainValue but channelValues size ({}) != signalErrors ({})", //
                            remote_uri, d.acq.channelValues.elements().size(), d.acq.channelErrors.elements().size())));                                     //
                    std::ranges::transform(inValues, outIt, [](const auto& v) { return gr::UncertainValue{v, typename T::value_type(0)}; });
                } else {
                    auto inErrors = std::span{d.acq.channelErrors.elements()}.subspan(d.read, inValues.size());
                    std::ranges::transform(std::views::zip(inValues, inErrors), outIt, [](const auto& ve) {
                        const auto& [v, e] = ve;
                        return gr::UncertainValue{v, e};
                    });
                }
            } else {
                std::ranges::transform(inValues, outIt, [](float v) { return static_cast<T>(v); });
            }

            for (const auto& [idx, trigger, timestamp, offset, yaml] : std::views::zip(d.acq.triggerIndices.value(), d.acq.triggerEventNames.value(), d.acq.triggerTimestamps.value(), d.acq.triggerOffsets.value(), d.acq.triggerYamlPropertyMaps.value())) {
                // this tag was already handled in a previous call OR it will be published in the next call
                if (idx < cast_to_signed(d.read) || static_cast<std::size_t>(idx - cast_to_signed(d.read)) >= nSamplesToCopy) {
                    continue;
                }
                auto map = gr::property_map{
                    {gr::tag::kTriggerName, {trigger}},
                    {gr::tag::kTriggerOffset, {offset}},
                };
                if (timestamp != 0) {
                    map.insert({gr::tag::kTriggerTime, {static_cast<std::uint64_t>(timestamp)}});
                    const auto now     = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
                    auto       latency = now - std::chrono::nanoseconds(timestamp);
                    map.insert({"REMOTE_SOURCE_LATENCY", static_cast<int64_t>(latency.count())});
                }
                const auto yamlMap = gr::pmt::yaml::deserialize(yaml);
                if (yamlMap) {
                    const gr::property_map& rootMap = yamlMap.value();
                    map.insert(rootMap.begin(), rootMap.end()); // duplicates ignored, not overwritten
                }
                if (verbose_console) {
                    const auto tagTime = std::chrono::system_clock::time_point() + std::chrono::nanoseconds(timestamp);
                    std::print("RemoteStreamSource: {} publish tag (tag-time: {}, systemtime: {}): {}\n", this->name.value, gr::time::getIsoTime(tagTime), gr::time::getIsoTime(), gr::join(map));
                }
                const auto tagIndex = written + static_cast<std::size_t>(idx - cast_to_signed(d.read));
                output.publishTag(map, tagIndex);
            }

            written += nSamplesToCopy;
            d.read += nSamplesToCopy;
            if (d.read == nSamples) {
                this->_queue->data.pop_front();
            }
        }
        output.publish(written);
        return written == 0UZ ? gr::work::Status::INSUFFICIENT_INPUT_ITEMS : gr::work::Status::OK;
    }

    std::optional<gr::Message> propertyCallbackLifecycleState(std::string_view propertyName, gr::Message message) { return Parent::propertyCallbackLifecycleState(propertyName, std::move(message)); }

    RemoteSubscriptionHandle copyRestResponseDataIntoQueue(const opencmw::mdp::Message& rep, const std::weak_ptr<Queue>& maybeQueue) {
        long           skippedUpdates     = 0;
        constexpr auto kSkipWarningPrefix = "Warning: skipped ";
        if (rep.error.starts_with(kSkipWarningPrefix)) {
            const std::string_view count = std::string_view(rep.error).substr(std::string_view(kSkipWarningPrefix).size());
            std::from_chars(count.data(), count.data() + count.size(), skippedUpdates);
        } else if (!rep.error.empty()) {
            gr::emit<gr::message::Command::Notify>(this->msgOut, gr::Error(std::format("Error in subscription:{}. Re-subscribing {}", rep.error, remote_uri)), {.endpoint = "subscription", .serviceName = this->unique_name});
            uint64_t nowTimeoutNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) //
                                    + static_cast<std::uint64_t>(reconnect_timeout * 1.e9f);
            this->_reconnect.store(nowTimeoutNs, std::memory_order_seq_cst);

            auto queue = maybeQueue.lock();
            if (!queue) {
                return std::move(this->_subscription); // release/unsubscribe
            }
            opendigitizer::acq::Acquisition acq;
            acq.channelValues                      = opencmw::MultiArray<float, 2>({0.f}, std::array<uint32_t, 2>{1U, 1U});
            acq.channelErrors                      = opencmw::MultiArray<float, 2>({0.f}, std::array<uint32_t, 2>{1U, 1U});
            acq.triggerEventNames                  = {"SubscriptionInterrupted"s};
            acq.triggerIndices                     = {std::int64_t(0)};
            acq.triggerTimestamps.value()          = {0};
            acq.triggerOffsets                     = {0.f};
            const gr::property_map yamlPropertyMap = {{"subscription-error", rep.error}};
            acq.triggerYamlPropertyMaps            = {gr::pmt::yaml::serialize(yamlPropertyMap)};
            std::lock_guard lock(queue->mutex);
            queue->data.push_back({std::move(acq), 0});
            return std::move(this->_subscription); // release/unsubscribe
        }
        if (rep.data.empty()) {
            return {};
        }
        try {
            auto queue = maybeQueue.lock();
            if (!queue) {
                return {};
            }
            opendigitizer::acq::Acquisition acq;
            auto                            buf = rep.data;
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);
            if (skippedUpdates != 0) {
                acq.triggerIndices.insert(acq.triggerIndices.begin(), 0L);
                acq.triggerTimestamps.insert(acq.triggerTimestamps.begin(), 0);
                acq.triggerEventNames.insert(acq.triggerEventNames.begin(), "WARNING_SAMPLES_DROPPED");
                acq.triggerOffsets.insert(acq.triggerOffsets.begin(), 0.0f);
            }
            std::lock_guard lock(queue->mutex);
            queue->data.push_back({std::move(acq), 0});
        } catch (opencmw::ProtocolException& e) {
            gr::emit<gr::message::Command::Notify>(this->msgOut, gr::Error(std::format("failed to deserialise update from {}: {}\n", remote_uri, e.what())), {.endpoint = "subscription", .serviceName = this->unique_name});
            return {};
        }
        this->progress->incrementAndGet();
        this->progress->notify_all();
        return {};
    }
};

template<typename T>
requires std::is_floating_point_v<T> || gr::UncertainValueLike<T>
struct RemoteDataSetSource : RemoteSourceBase, RemoteSourceCommon<RemoteDataSetSource<T>, gr::DataSet<T>>, gr::Block<RemoteDataSetSource<T>> {
    using Parent = gr::Block<RemoteDataSetSource<T>>;
    using Queue  = RemoteSourceCommon<RemoteDataSetSource, gr::DataSet<T>>::Queue;

    constexpr static auto floatToDatasetTypeConvert = [](float v) constexpr {
        if constexpr (gr::UncertainValueLike<T>) {
            return static_cast<T::value_type>(v);
        } else {
            return static_cast<T>(v);
        }
    };

    gr::PortOut<gr::DataSet<T>> out;

    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">>               verbose_console   = false;
    gr::Annotated<float, "reconnect timeout", gr::Doc<"reconnect timeout in sec">> reconnect_timeout = 5.f;

    GR_MAKE_REFLECTABLE(RemoteDataSetSource, out, remote_uri, host, verbose_console, reconnect_timeout);

    explicit RemoteDataSetSource(gr::property_map props) : Parent(std::move(props)) {
        RemoteSources::instance().registerRemoteSource(this);
        this->disconnect_on_done = false;
    }
    ~RemoteDataSetSource() { RemoteSources::instance().unregisterRemoteSource(this); }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        this->maybeReconnect();
        std::lock_guard           lock(this->_queue->mutex);
        const auto                n       = std::min(this->_queue->data.size(), output.size());
        std::span<gr::DataSet<T>> outSpan = output;
        for (auto i = 0UZ; i < n; ++i) {
            outSpan[i] = std::move(this->_queue->data.front());
            this->_queue->data.pop_front();
        }
        output.publish(n);
        return n == 0UZ ? gr::work::Status::INSUFFICIENT_INPUT_ITEMS : gr::work::Status::OK;
    }

    std::optional<gr::Message> propertyCallbackLifecycleState(std::string_view propertyName, gr::Message message) { return Parent::propertyCallbackLifecycleState(propertyName, std::move(message)); }

    RemoteSubscriptionHandle copyRestResponseDataIntoQueue(const opencmw::mdp::Message& rep, const std::weak_ptr<Queue>& maybeQueue) {
        long           skippedSamples     = 0;
        constexpr auto kSkipWarningPrefix = "Warning: skipped ";
        if (rep.error.starts_with(kSkipWarningPrefix)) {
            const std::string_view count = std::string_view(rep.error).substr(std::string_view(kSkipWarningPrefix).size());
            std::from_chars(count.data(), count.data() + count.size(), skippedSamples);
        } else if (!rep.error.empty()) {
            emit<gr::message::Command::Notify>(this->msgOut, gr::Error(std::format("Error in subscription: {}. Re-subscribing {}\n", rep.error, remote_uri)), {.endpoint = "subscription", .serviceName = this->unique_name});
            uint64_t nowTimeoutNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) //
                                    + static_cast<std::uint64_t>(reconnect_timeout * 1.e9f);
            this->_reconnect.store(nowTimeoutNs, std::memory_order_seq_cst);
            return std::move(this->_subscription); // release/unsubscribe
        }
        if (rep.data.empty()) {
            return {};
        }
        try {
            auto queue = maybeQueue.lock();
            if (!queue) {
                return {};
            }
            auto                            buf = rep.data;
            opendigitizer::acq::Acquisition acq;
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);

            const auto nSignals = static_cast<std::size_t>(acq.channelValues.n(0));
            const auto nSamples = static_cast<std::size_t>(acq.channelValues.n(1));
            if (nSignals == 0 || nSamples == 0) {
                return {};
            }

            gr::DataSet<T> ds = toDataSet(acq, static_cast<std::uint64_t>(skippedSamples));
            if constexpr (gr::UncertainValueLike<T>) {
                if (acq.channelValues.elements().size() != acq.channelErrors.elements().size()) {
                    this->emitErrorMessage("subscriptionCallback(..)",                                                                                       //
                        gr::Error(std::format("Inconsistent data from '{}': Sample type is UncertainValue but channelValues size ({}) != signalErrors ({})", //
                            remote_uri, acq.channelValues.elements().size(), acq.channelErrors.elements().size())));                                         //
                }
            }
            if (const auto consistent = gr::dataset::checkConsistency(ds, remote_uri); !consistent) {
                this->emitErrorMessage("subscriptionCallback(..)", consistent.error());
            }

            std::lock_guard lock(queue->mutex);
            queue->data.push_back(std::move(ds));
        } catch (opencmw::ProtocolException& e) {
            gr::emit<gr::message::Command::Notify>(this->msgOut, gr::Error(std::format("failed to deserialise update from {}: {}\n", remote_uri, e.what())), {.endpoint = "subscription", .serviceName = this->unique_name});
            return {};
        }
        this->progress->incrementAndGet();
        this->progress->notify_all();
        return {};
    }

    /// one DataSet of every channel of `acq`; fields whose sizes do not fit the channels stay empty (checkConsistency names them), and missing uncertainties are 0.
    /// Each trigger is a timing event of every signal.
    [[nodiscard]] static gr::DataSet<T> toDataSet(const opendigitizer::acq::Acquisition& acq, std::uint64_t skippedSamples) {
        const auto     nSignals = static_cast<std::size_t>(acq.channelValues.n(0));
        const auto     nSamples = static_cast<std::size_t>(acq.channelValues.n(1));
        gr::DataSet<T> ds;
        ds.timestamp  = acq.acqLocalTimeStamp.value(); // UTC timestamp [ns]
        ds.extents    = {static_cast<int32_t>(nSamples)};
        ds.layout     = gr::LayoutRight{};
        ds.axis_names = {"x-axis"};
        ds.axis_units = {"a.u."};
        if (nSamples == acq.channelTimeSinceRefTrigger.size()) {
            ds.axis_values.resize(1UZ);
            ds.axis_values[0UZ].resize(nSamples);
            std::ranges::copy(acq.channelTimeSinceRefTrigger | std::views::transform(floatToDatasetTypeConvert), std::ranges::begin(ds.axisValues(0UZ)));
        }
        if (nSignals == acq.channelNames.size() && nSignals == acq.channelQuantities.size() && nSignals == acq.channelUnits.size()) {
            ds.signal_names.assign(acq.channelNames.begin(), acq.channelNames.end());
            ds.signal_units.assign(acq.channelUnits.begin(), acq.channelUnits.end());
            ds.signal_quantities.assign(acq.channelQuantities.begin(), acq.channelQuantities.end());
        }
        if (nSignals == acq.channelRangeMin.size() && nSignals == acq.channelRangeMax.size()) {
            ds.signal_ranges.resize(nSignals);
            for (std::size_t i = 0; i < nSignals; i++) {
                ds.signal_ranges[i] = {floatToDatasetTypeConvert(acq.channelRangeMin[i]), floatToDatasetTypeConvert(acq.channelRangeMax[i])};
            }
        }
        copySignalValues(ds, acq, nSignals * nSamples);
        ds.meta_information.assign(nSignals, gr::property_map{{"subscription-updates-skipped", skippedSamples}});
        ds.timing_events.resize(nSignals);
        for (const auto& [idx, yaml] : std::views::zip(acq.triggerIndices.value(), acq.triggerYamlPropertyMaps.value())) {
            if (const auto tag = gr::pmt::yaml::deserialize(yaml)) {
                for (auto& events : ds.timing_events) {
                    events.emplace_back(static_cast<std::ptrdiff_t>(idx), tag.value());
                }
            }
        }
        return ds;
    }

    /// written directly: a DataSet without signal names has no signalValues(i)
    static void copySignalValues(gr::DataSet<T>& ds, const opendigitizer::acq::Acquisition& acq, std::size_t nValues) {
        const auto& elements = acq.channelValues.elements();
        const auto  values   = std::span(elements).first(std::min(nValues, elements.size()));
        ds.signal_values.resize(nValues);
        if constexpr (gr::UncertainValueLike<T>) {
            using V             = typename T::value_type;
            const auto& errors  = acq.channelErrors.elements();
            const bool  withErr = errors.size() == elements.size();
            if (withErr) {
                std::ranges::transform(values, errors, ds.signal_values.begin(), [](float v, float e) { return T{static_cast<V>(v), static_cast<V>(e)}; });
            } else {
                std::ranges::transform(values, ds.signal_values.begin(), [](float v) { return T{static_cast<V>(v), V(0)}; });
            }
        } else {
            std::ranges::transform(values, ds.signal_values.begin(), floatToDatasetTypeConvert);
        }
    }
};

} // namespace gr::od

#endif
