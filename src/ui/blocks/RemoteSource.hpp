#ifndef OPENDIGITIZER_REMOTESOURCE_HPP
#define OPENDIGITIZER_REMOTESOURCE_HPP

#include <format>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

#include <daq_api.hpp>

#include <opencmw.hpp>

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>

#include "conversion.hpp"
#include "settings.hpp"

namespace opendigitizer {

inline opencmw::URI<> resolveRelativeTopic(const std::string& remote, const std::string& base) {
    auto pathUrl = opencmw::URI<>(remote);
    if (!pathUrl.hostName().has_value() || pathUrl.hostName()->empty()) {
        auto baseUrl = opencmw::URI<>(base.empty() ? "https://localhost:8080" : base);
        auto result  = opencmw::URI<>::UriFactory().scheme(baseUrl.scheme().value()).authority(baseUrl.authority().value()).path(pathUrl.path().value_or("")).queryParam(pathUrl.queryParam().value_or("")).fragment(pathUrl.fragment().value_or("")).build();
        return result;
    }
    return pathUrl;
}

struct RemoteSourceModel {
    virtual ~RemoteSourceModel() {}
    virtual std::string uniqueName() const = 0;
    virtual std::string remoteUri() const  = 0;
    virtual std::string typeName() const   = 0;
    virtual void*       raw() const        = 0;
};

class RemoteSourceManager {
private:
    RemoteSourceManager() = default;

    RemoteSourceManager(const RemoteSourceManager&)            = delete;
    RemoteSourceManager& operator=(const RemoteSourceManager&) = delete;

    std::unordered_map<std::string, std::unique_ptr<RemoteSourceModel>>      _knownRemoteSources;
    std::unordered_map<std::string, std::function<void(RemoteSourceModel&)>> _addingSourcesCallbacks;

    template<typename TRemoteSource>
    struct RemoteSourceWrapper : RemoteSourceModel {
        RemoteSourceWrapper(TRemoteSource* _block) : block(_block) {}
        ~RemoteSourceWrapper() override {}

        std::string uniqueName() const override { return block->unique_name; }
        std::string remoteUri() const override { return block->remote_uri; }
        std::string typeName() const override { return gr::meta::type_name<TRemoteSource>(); }

        void* raw() const override { return block; }

        TRemoteSource* block = nullptr;
    };

public:
    static RemoteSourceManager& instance() {
        static RemoteSourceManager s_instance;
        return s_instance;
    }

    template<typename TBlock>
    void registerRemoteSource(TBlock* block) {
        _knownRemoteSources[block->unique_name] = std::make_unique<RemoteSourceWrapper<TBlock>>(block);
    }

    template<typename TBlock>
    void unregisterRemoteSource(TBlock* block) {
        _knownRemoteSources.erase(block->unique_name);
    }

    void setRemoteSourceAddedCallback(std::string remoteUri, std::function<void(RemoteSourceModel&)> callback) { _addingSourcesCallbacks[std::move(remoteUri)] = callback; }

    void notifyOfRemoteSource(const std::string& remoteUri, void* remoteSourceRaw) {
        auto it = _addingSourcesCallbacks.find(remoteUri);
        if (it == _addingSourcesCallbacks.end()) {
            return;
        }

        auto& callback = it->second;
        for (const auto& [_, sourceModel] : _knownRemoteSources) {
            if (sourceModel->raw() == remoteSourceRaw) {
                callback(*sourceModel);
            }
        }
        _addingSourcesCallbacks.erase(it);
    }
};

struct RemoteSourceBase {
    std::string remote_uri;
    std::string host = "ADDA";
};

template<typename T>
requires std::is_floating_point_v<T> || gr::UncertainValueLike<T>
struct RemoteStreamSource : RemoteSourceBase, gr::Block<RemoteStreamSource<T>> {
    using Parent = gr::Block<RemoteStreamSource<T>>;
    gr::PortOut<T> out;

    gr::Annotated<std::string, "signal name", gr::Visible, gr::Doc<"Identifier for the signal">>                                                                              signal_name;
    gr::Annotated<std::string, "signal quantity", gr::Visible, gr::Doc<"Physical quantity represented by the signal">>                                                        signal_quantity;
    gr::Annotated<std::string, "signal unit", gr::Visible, gr::Doc<"Unit of measurement for the signal values">>                                                              signal_unit;
    gr::Annotated<float, "signal min", gr::Doc<"Minimum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_min        = std::numeric_limits<float>::lowest();
    gr::Annotated<float, "signal max", gr::Doc<"Maximum expected value for the signal">, gr::Limits<std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max()>> signal_max        = std::numeric_limits<float>::max();
    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">>                                                                                                          verbose_console   = false;
    gr::Annotated<float, "reconnect timeout", gr::Doc<"reconnect timeout in sec">>                                                                                            reconnect_timeout = 5.f;

    opencmw::client::RestClient _client = opencmw::client::RestClient(opencmw::client::VerifyServerCertificates(Digitizer::Settings::instance().checkCertificates));
    std::string                 _subscribedUri;
    std::atomic<std::uint64_t>  _reconnect{0ULL}; // 0 == disabled, otherwise time since epoch in ns

    GR_MAKE_REFLECTABLE(RemoteStreamSource, out, remote_uri, signal_name, signal_unit, signal_quantity, signal_min, signal_max, host, verbose_console, reconnect_timeout);

    struct Data {
        opendigitizer::acq::Acquisition acq;
        std::size_t                     read = 0;
    };

    struct Queue {
        std::deque<Data> data;
        std::mutex       mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    RemoteStreamSource(gr::property_map props) : Parent(props) { RemoteSourceManager::instance().registerRemoteSource(this); }
    ~RemoteStreamSource() { RemoteSourceManager::instance().unregisterRemoteSource(this); }

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
        std::uint64_t       reconnectNs = _reconnect.load(std::memory_order_acquire);
        const std::uint64_t nowNs       = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        if (reconnectNs != 0ULL && reconnectNs < nowNs && !host.empty() && !remote_uri.empty()) {
            if (_reconnect.compare_exchange_strong(reconnectNs, 0ULL, std::memory_order_acq_rel)) {
                startSubscription(remote_uri);
            }
        }

        std::size_t     written = 0;
        std::lock_guard lock(_queue->mutex);
        while (written < output.size() && !_queue->data.empty()) {
            auto& d = _queue->data.front();
            updateSettingsFromAcquisition(d.acq);

            const auto nSignals = static_cast<std::size_t>(d.acq.channelValues.n(0));
            const auto nSamples = static_cast<std::size_t>(d.acq.channelValues.n(1));
            if (nSignals == 0 || nSamples == 0) {
                continue;
            }

            if (nSignals != 1) {
                this->emitErrorMessage("processBulk(..)", gr::Error(std::format("Expected exactly one channel, but got {} channelValues", nSignals)));
                continue;
            }
            // Only one signal is stored
            const auto nSamplesToCopy = std::min(output.size() - written, d.acq.channelValues.elements().size() - d.read);
            auto       inValues       = std::span{d.acq.channelValues.elements()}.subspan(d.read, nSamplesToCopy);
            auto       outIt          = output.begin() + cast_to_signed(written);
            if constexpr (std::is_same_v<T, float>) {
                std::ranges::copy(inValues, outIt);
            } else if constexpr (gr::UncertainValueLike<T>) { // TODO: still needs to be tested when we get full support of gr::UncertainValue
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

            // publish trigger info
            for (const auto& [idx, trigger, timestamp, offset, yaml] : std::views::zip(d.acq.triggerIndices.value(), d.acq.triggerEventNames.value(), d.acq.triggerTimestamps.value(), d.acq.triggerOffsets.value(), d.acq.triggerYamlPropertyMaps.value())) {
                // this tag was already handled in a previous call OR it will be published in the next call
                if (idx < cast_to_signed(d.read) || static_cast<std::size_t>(idx - cast_to_signed(d.read)) >= nSamplesToCopy) {
                    continue;
                }
                auto map = gr::property_map{
                    {gr::tag::TRIGGER_NAME.shortKey(), {trigger}},
                    {gr::tag::TRIGGER_OFFSET.shortKey(), {offset}},
                };
                if (timestamp != 0) {
                    map.insert({gr::tag::TRIGGER_TIME.shortKey(), {static_cast<std::uint64_t>(timestamp)}});
                    const auto now     = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
                    auto       latency = now - std::chrono::nanoseconds(timestamp);
                    map.insert({"REMOTE_SOURCE_LATENCY", {latency.count()}}); // compares the current system time with the time inside the tag
                }
                const auto yamlMap = gr::pmt::yaml::deserialize(yaml);
                if (yamlMap) {
                    const gr::property_map& rootMap = yamlMap.value();
                    map.insert(rootMap.begin(), rootMap.end()); // Ignore duplicates (do not overwrite)
                } else {
                    // throw gr::exception(std::format("Could not parse yaml for Tag property_map: {}:{}\n{}", yamlMap.error().message, yamlMap.error().line, yaml));
                }
                if (verbose_console) {
                    const auto tag_time = std::chrono::system_clock::time_point() + std::chrono::nanoseconds(timestamp);
                    std::print("RemoteStreamSource: {} publish tag (tag-time: {}, systemtime: {}): {}\n", this->name.value, gr::time::getIsoTime(tag_time), gr::time::getIsoTime(), gr::join(map));
                }
                const auto tagIndex = written + static_cast<std::size_t>(idx - cast_to_signed(d.read));
                output.publishTag(map, tagIndex);
            }

            written += nSamplesToCopy;
            d.read += nSamplesToCopy;
            if (d.read == nSamples) {
                _queue->data.pop_front();
            }
        }
        output.publish(written);
        return written == 0UZ ? gr::work::Status::INSUFFICIENT_INPUT_ITEMS : gr::work::Status::OK;
    }

    void stopSubscription() {
        if (_subscribedUri.empty()) {
            return;
        }
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.topic    = opencmw::URI<>(_subscribedUri);
        command.callback = [](const opencmw::mdp::Message&) {};
        _client.request(command);
        _subscribedUri = "";
    }

    std::optional<gr::Message> propertyCallbackLifecycleState(std::string_view propertyName, gr::Message message) { return Parent::propertyCallbackLifecycleState(propertyName, std::move(message)); }

    void startSubscription(const std::string& uri) {
        if (!_subscribedUri.empty()) {
            return;
        }
        std::print("<<RemoteSource.hpp>> RemoteStreamSource::startSubscription {}\n", uri);
        opencmw::client::Command command;
        command.command          = opencmw::mdp::Command::Subscribe;
        command.topic            = resolveRelativeTopic(uri, host);
        _subscribedUri           = command.topic.str();
        std::weak_ptr maybeQueue = _queue;
        command.callback         = [maybeQueue, uri, this](const opencmw::mdp::Message& rep) {
            long           skipped_updates     = 0;
            constexpr auto skip_warning_prefix = "Warning: skipped ";
            if (rep.error.starts_with(skip_warning_prefix)) {
                skipped_updates = std::stol(rep.error.substr(std::string_view(skip_warning_prefix).size()));
            } else if (!rep.error.empty()) {
                stopSubscription();
                gr::sendMessage<gr::message::Command::Notify>(this->msgOut, this->unique_name /* serviceName */, "subscription", //
                            gr::Error(std::format("Error in subscription:{}. Re-subscribing {}", rep.error, remote_uri)));
                uint64_t nowTimeoutNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) //
                                        + static_cast<std::uint64_t>(reconnect_timeout * 1.e9f);
                _reconnect.store(nowTimeoutNs, std::memory_order_release);

                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                opendigitizer::acq::Acquisition acq;
                acq.channelValues                      = opencmw::MultiArray<float, 2>({0.f}, std::array<uint32_t, 2>{1U, 1U});
                acq.channelErrors                      = opencmw::MultiArray<float, 2>({0.f}, std::array<uint32_t, 2>{1U, 1U});
                acq.triggerEventNames                  = {"SubscriptionInterrupted"s};
                acq.triggerIndices                     = {std::int64_t(0)};
                acq.triggerTimestamps.value()          = {0}; // {std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()};
                acq.triggerOffsets                     = {0.f};
                const gr::property_map yamlPropertyMap = {{"subscription-error", rep.error}};
                acq.triggerYamlPropertyMaps            = {gr::pmt::yaml::serialize(yamlPropertyMap)};
                std::lock_guard lock(queue->mutex);
                queue->data.push_back({std::move(acq), 0});
                return;
            }
            if (rep.data.empty()) {
                return;
            }
            try {
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                opendigitizer::acq::Acquisition acq;
                auto                            buf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);
                if (skipped_updates != 0) {
                    acq.triggerIndices.insert(acq.triggerIndices.begin(), 0L);
                    acq.triggerTimestamps.insert(acq.triggerTimestamps.begin(), 0);
                    acq.triggerEventNames.insert(acq.triggerEventNames.begin(), "WARNING_SAMPLES_DROPPED");
                    acq.triggerOffsets.insert(acq.triggerOffsets.begin(), 0.0f);
                }
                std::lock_guard lock(queue->mutex);
                queue->data.push_back({std::move(acq), 0});
            } catch (opencmw::ProtocolException& e) {
                gr::sendMessage<gr::message::Command::Notify>(this->msgOut, this->unique_name /* serviceName */, "subscription", //
                            gr::Error(std::format("failed to deserialise update from {}: {}\n", remote_uri, e.what())));
                return;
            }
            this->progress->incrementAndGet();
            this->progress->notify_all();
        };
        _client.request(command);
    }

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& new_settings) {
        // GR doesn't set the state for a block added after the scheduler started
        // if (Parent::state() != gr::lifecycle::State::RUNNING) {
        //     std::print("<<RemoteSource.hpp>> We didn't get a running lifetime from GR\n");
        //     return; // early return, only apply settings for the running flowgraph
        // }
        if ((new_settings.contains("host") || new_settings.contains("remote_uri")) && !host.empty() && !remote_uri.empty()) {
            RemoteSourceManager::instance().notifyOfRemoteSource(remote_uri, this);
            stopSubscription();
            startSubscription(remote_uri);
        }
    }

    void start() {
        if (!remote_uri.empty() && !host.empty()) {
            startSubscription(remote_uri);
        }
    }

    void stop() { stopSubscription(); }
}; // RemoteStreamSource

template<typename T>
requires std::is_floating_point_v<T> || gr::UncertainValueLike<T>
struct RemoteDataSetSource : RemoteSourceBase, gr::Block<RemoteDataSetSource<T>> {
    using Parent = gr::Block<RemoteDataSetSource<T>>;
    gr::PortOut<gr::DataSet<T>> out;

    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">>               verbose_console   = false;
    gr::Annotated<float, "reconnect timeout", gr::Doc<"reconnect timeout in sec">> reconnect_timeout = 5.f;

    opencmw::client::RestClient _client = opencmw::client::RestClient(opencmw::client::VerifyServerCertificates(Digitizer::Settings::instance().checkCertificates));
    std::string                 _subscribedUri;
    std::atomic<std::uint64_t>  _reconnect{0ULL}; // 0 == disabled, otherwise time since epoch in ns

    GR_MAKE_REFLECTABLE(RemoteDataSetSource, out, remote_uri, host, verbose_console, reconnect_timeout);
    struct Queue {
        std::deque<gr::DataSet<T>> data;
        std::mutex                 mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    RemoteDataSetSource(gr::property_map props) : Parent(std::move(props)) {}

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        std::uint64_t       reconnectNs = _reconnect.load(std::memory_order_acquire);
        const std::uint64_t nowNs       = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count());

        if (reconnectNs != 0ULL && reconnectNs < nowNs && !host.empty() && !remote_uri.empty()) {
            if (_reconnect.compare_exchange_strong(reconnectNs, 0ULL, std::memory_order_acq_rel)) {
                startSubscription(remote_uri);
            }
        }

        std::lock_guard           lock(_queue->mutex);
        const auto                n       = std::min(_queue->data.size(), output.size());
        std::span<gr::DataSet<T>> outSpan = output;
        for (auto i = 0UZ; i < n; ++i) {
            outSpan[i] = std::move(_queue->data.front());
            _queue->data.pop_front();
        }
        output.publish(n);
        return n == 0UZ ? gr::work::Status::INSUFFICIENT_INPUT_ITEMS : gr::work::Status::OK;
    }

    void stopSubscription() {
        if (_subscribedUri.empty()) {
            return;
        }
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.topic    = opencmw::URI<>(_subscribedUri);
        command.callback = [](const opencmw::mdp::Message&) {};
        _client.request(command);
        _subscribedUri = "";
    }

    std::optional<gr::Message> propertyCallbackLifecycleState(std::string_view propertyName, gr::Message message) {
        //
        return Parent::propertyCallbackLifecycleState(propertyName, std::move(message));
    }

    void startSubscription(const std::string& uri) {
        if (!_subscribedUri.empty()) {
            return;
        }
        std::print("<<RemoteSource.hpp>> RemoteDataSetSource::startSubscription {}\n", uri);
        opencmw::client::Command command;
        command.command          = opencmw::mdp::Command::Subscribe;
        command.topic            = resolveRelativeTopic(uri, host);
        _subscribedUri           = command.topic.str();
        std::weak_ptr maybeQueue = _queue;

        command.callback = [maybeQueue, uri, this](const opencmw::mdp::Message& rep) {
            long           skipped_samples     = 0;
            constexpr auto skip_warning_prefix = "Warning: skipped ";
            if (rep.error.starts_with(skip_warning_prefix)) {
                skipped_samples = std::stol(rep.error.substr(std::string_view(skip_warning_prefix).size()));
            } else if (!rep.error.empty()) {
                stopSubscription();
                sendMessage<gr::message::Command::Notify>(this->msgOut, this->unique_name /* serviceName */, "subscription", //
                    gr::Error(std::format("Error in subscription: {}. Re-subscribing {}\n", rep.error, remote_uri)));
                uint64_t nowTimeoutNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()) //
                                        + static_cast<std::uint64_t>(reconnect_timeout * 1.e9f);
                _reconnect.store(nowTimeoutNs, std::memory_order_release);
                return;
            }
            if (rep.data.empty()) {
                return;
            }
            try {
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                auto                            buf = rep.data;
                opendigitizer::acq::Acquisition acq;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);

                const auto nSignals = static_cast<std::size_t>(acq.channelValues.n(0));
                const auto nSamples = static_cast<std::size_t>(acq.channelValues.n(1));
                if (nSignals == 0 || nSamples == 0) {
                    return;
                }

                gr::DataSet<T> ds;
                auto           convert = [](float v) {
                    if constexpr (gr::UncertainValueLike<T>) {
                        return static_cast<T::value_type>(v);
                    } else {
                        return static_cast<T>(v);
                    }
                };

                ds.timestamp = acq.acqLocalTimeStamp.value(); // UTC timestamp [ns]

                // signal data layout:
                ds.extents = {static_cast<int32_t>(nSamples)};
                ds.layout  = gr::LayoutRight{};

                // axis layout:
                ds.axis_names = {"x-axis"};
                ds.axis_units = {"a.u."};
                if (nSamples == acq.channelTimeSinceRefTrigger.size()) {
                    ds.axis_values.resize(1UZ);
                    ds.axis_values[0UZ].resize(nSamples);
                    std::ranges::copy(acq.channelTimeSinceRefTrigger | std::views::transform(convert), std::ranges::begin(ds.axisValues(0UZ)));
                } else {
                    this->emitErrorMessage("subscriptionCallback(..)",                                                               //
                        gr::Error(std::format("Inconsistent data from '{}': channelTimeSinceRefTrigger size ({}) !=  nSamples ({})", //
                            remote_uri, acq.channelTimeSinceRefTrigger.size(), nSamples)));
                }

                // signal meta info
                if (nSignals == acq.channelNames.size() && nSignals == acq.channelQuantities.size() && nSignals == acq.channelUnits.size()) {
                    ds.signal_names      = acq.channelNames;
                    ds.signal_units      = acq.channelUnits;
                    ds.signal_quantities = acq.channelQuantities;
                } else {
                    this->emitErrorMessage("subscriptionCallback(..)",                                                                                                          //
                        gr::Error(std::format("Inconsistent data from '{}': channelNames size ({}) or channelQuantities size ({}) or channelUnits size ({}) !=  nSignals ({})", //
                            remote_uri, acq.channelNames.size(), acq.channelQuantities.size(), acq.channelUnits.size(), nSignals)));                                            //
                }

                if (nSignals == acq.channelRangeMin.size() && nSignals == acq.channelRangeMax.size()) {
                    ds.signal_ranges.resize(nSignals);
                    for (std::size_t i = 0; i < nSignals; i++) {
                        ds.signal_ranges[i] = {convert(acq.channelRangeMin[i]), convert(acq.channelRangeMax[i])};
                    }
                } else {
                    this->emitErrorMessage("subscriptionCallback(..)",                                                                                 //
                        gr::Error(std::format("Inconsistent data from '{}': channelRangeMin size ({}) or channelRangeMax size ({}) !=  nSignals ({})", //
                            remote_uri, acq.channelRangeMin.size(), acq.channelRangeMax.size(), nSignals)));                                           //
                }

                // copy signal values
                ds.signal_values.resize(nSignals * nSamples);
                // TODO: still needs to be tested when we get full support of gr::UncertainValue
                if constexpr (gr::UncertainValueLike<T>) {
                    const bool dataOk = acq.channelValues.elements().size() == acq.channelErrors.elements().size();
                    if (!dataOk) {
                        this->emitErrorMessage("subscriptionCallback(..)",                                                                                       //
                            gr::Error(std::format("Inconsistent data from '{}': Sample type is UncertainValue but channelValues size ({}) != signalErrors ({})", //
                                remote_uri, acq.channelValues.elements().size(), acq.channelErrors.elements().size())));                                         //

                        for (std::size_t i = 0; i < nSignals; i++) {
                            auto outValues = ds.signalValues(i);
                            auto inValues  = std::span{std::next(acq.channelValues.elements().begin(), static_cast<std::ptrdiff_t>(i * nSamples)), nSamples};
                            std::ranges::transform(inValues, outValues.begin(), [](const auto& v) { return gr::UncertainValue{static_cast<typename T::value_type>(v), typename T::value_type(0)}; });
                        }
                    } else {
                        for (std::size_t i = 0; i < nSignals; i++) {
                            auto outValues = ds.signalValues(i);
                            auto inValues  = std::span{std::next(acq.channelValues.elements().begin(), static_cast<std::ptrdiff_t>(i * nSamples)), nSamples};
                            auto inErrors  = std::span{std::next(acq.channelErrors.elements().begin(), static_cast<std::ptrdiff_t>(i * nSamples)), nSamples};
                            std::ranges::transform(std::views::zip(inValues, inErrors), outValues.begin(), [](const auto& ve) {
                                const auto& [v, e] = ve;
                                return gr::UncertainValue{static_cast<typename T::value_type>(v), static_cast<typename T::value_type>(e)};
                            });
                        }
                    }
                } else {
                    auto signalValues = acq.channelValues.elements() | std::views::transform(convert);
                    for (std::size_t i = 0; i < nSignals; i++) {
                        std::ranges::copy_n(std::next(signalValues.begin(), static_cast<std::ptrdiff_t>(i * nSamples)), static_cast<std::ptrdiff_t>(nSamples), ds.signalValues(i).begin());
                    }
                }

                // meta data
                ds.meta_information.push_back({{"subscription-updates-skipped", static_cast<uint64_t>(skipped_samples)}});
                ds.timing_events.resize(1UZ);

                for (const auto& [idx, yaml] : std::views::zip(acq.triggerIndices.value(), acq.triggerYamlPropertyMaps.value())) {
                    const auto yamlMap = gr::pmt::yaml::deserialize(yaml);
                    if (yamlMap) {
                        const gr::property_map& rootMap = yamlMap.value();
                        ds.timing_events[0].emplace_back(static_cast<std::ptrdiff_t>(idx), rootMap);
                    } else {
                        // throw gr::exception(std::format("Could not parse yaml for Tag property_map: {}:{}\n{}", yamlMap.error().message, yamlMap.error().line, yaml));
                    }
                }

                std::lock_guard lock(queue->mutex);
                queue->data.push_back(std::move(ds));
            } catch (opencmw::ProtocolException& e) {
                gr::sendMessage<gr::message::Command::Notify>(this->msgOut, this->unique_name /* serviceName */, "subscription", gr::Error(std::format("failed to deserialise update from {}: {}\n", remote_uri, e.what())));
                return;
            }
            this->progress->incrementAndGet();
            this->progress->notify_all();
        };
        _client.request(command);
    }

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& new_settings) {
        // GR doesn't set the state for a block added after the scheduler started
        // if (Parent::state() != gr::lifecycle::State::RUNNING) {
        //     std::print("<<RemoteSource.hpp>> We didn't get a running lifetime from GR\n");
        //     return; // early return, only apply settings for the running flowgraph
        // }
        if ((new_settings.contains("host") || new_settings.contains("remote_uri")) && !host.empty() && !remote_uri.empty()) {
            RemoteSourceManager::instance().notifyOfRemoteSource(remote_uri, this);
            stopSubscription();
            startSubscription(remote_uri);
        }
    }

    void start() {
        if (!remote_uri.empty() && !host.empty()) {
            startSubscription(remote_uri);
        }
    }

    void stop() { stopSubscription(); }
};

} // namespace opendigitizer

inline auto registerRemoteStreamSource  = gr::registerBlock<opendigitizer::RemoteStreamSource, float, gr::UncertainValue<float>>(gr::globalBlockRegistry());
inline auto registerRemoteDataSetSource = gr::registerBlock<opendigitizer::RemoteDataSetSource, float, gr::UncertainValue<float>>(gr::globalBlockRegistry());

#endif
