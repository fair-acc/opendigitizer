#ifndef OPENDIGITIZER_REMOTESOURCE_HPP
#define OPENDIGITIZER_REMOTESOURCE_HPP

#include <format>
#include <type_traits>
#include <unordered_map>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

#include <daq_api.hpp>

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <opencmw.hpp>

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
requires std::is_floating_point_v<T>
struct RemoteStreamSource : RemoteSourceBase, gr::Block<RemoteStreamSource<T>> {
    using Parent = gr::Block<RemoteStreamSource<T>>;
    gr::PortOut<T> out;

    std::string signal_name;
    std::string signal_unit;
    float       signal_min;
    float       signal_max;

    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">> verbose_console = false;

    opencmw::client::RestClient _client = opencmw::client::RestClient(opencmw::client::VerifyServerCertificates(Digitizer::Settings::instance().checkCertificates));
    std::string                 _subscribedUri;

    std::optional<std::chrono::system_clock::time_point> _reconnect{};

    GR_MAKE_REFLECTABLE(RemoteStreamSource, out, remote_uri, signal_name, signal_unit, signal_min, signal_max, host, verbose_console);

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
        if (signal_name != acq.channelName.value() || signal_unit != acq.channelUnit.value() || signal_min != acq.channelRangeMin.value() || signal_max != acq.channelRangeMax.value()) {
            if (!acq.channelName.value().empty()) {
                this->settings().set({{"signal_name", acq.channelName.value()}});
            }

            if (!acq.channelUnit.value().empty()) {
                this->settings().set({{"signal_unit", acq.channelUnit.value()}});
            }

            this->settings().set({{"signal_min", acq.channelRangeMin.value()}, {"signal_max", acq.channelRangeMax.value()}});
        }
    }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        if (_reconnect.has_value() && _reconnect.value() < std::chrono::system_clock::now() && !host.empty() && !remote_uri.empty()) {
            startSubscription(remote_uri);
            _reconnect.reset();
        }

        std::size_t     written = 0;
        std::lock_guard lock(_queue->mutex);
        while (written < output.size() && !_queue->data.empty()) {
            auto& d = _queue->data.front();
            updateSettingsFromAcquisition(d.acq);
            auto in = std::span<const float>(d.acq.channelValue.begin(), d.acq.channelValue.end());
            in      = in.subspan(d.read, std::min(output.size() - written, in.size() - d.read));

            if constexpr (std::is_same_v<T, float>) {
                std::ranges::copy(in, output.begin() + cast_to_signed(written));
            } else {
                std::ranges::transform(in, output.begin() + cast_to_signed(written), [](float v) { return static_cast<T>(v); });
            }

            for (const auto& [idx, trigger, timestamp, offset, yaml] : std::views::zip(d.acq.triggerIndices.value(), d.acq.triggerEventNames.value(), d.acq.triggerTimestamps.value(), d.acq.triggerOffsets.value(), d.acq.triggerYamlPropertyMaps.value())) {
                if (idx < cast_to_signed(d.read)) { // this tag was already handled in a previous call
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
                const auto yamlMap = pmtv::yaml::deserialize(yaml);
                if (yamlMap) {
                    const gr::property_map& rootMap = yamlMap.value();
                    map.insert(rootMap.begin(), rootMap.end()); // Ignore duplicates (do not overwrite)
                } else {
                    // throw gr::exception(std::format("Could not parse yaml for Tag property_map: {}:{}\n{}", yamlMap.error().message, yamlMap.error().line, yaml));
                }
                if (verbose_console) {
                    auto tag_time = std::chrono::system_clock::time_point() + std::chrono::nanoseconds(timestamp);
                    std::print("RemoteStreamSource: {} publish tag (tag-time: {}, systemtime: {}): {}\n", this->name, tag_time, std::chrono::system_clock::now(), map);
                }
                output.publishTag(map, cast_to_unsigned(idx - cast_to_signed(d.read)));
            }
            written += in.size();
            d.read += in.size();
            if (d.read == d.acq.channelValue.size()) {
                _queue->data.pop_front();
            }
        }
        output.publish(written);
        return gr::work::Status::OK;
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
                _reconnect = std::chrono::system_clock::now() + 5s;
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                opendigitizer::acq::Acquisition acq;
                acq.channelValue.value()            = {0};
                acq.triggerEventNames.value()       = {"SubscriptionInterrupted"};
                acq.triggerIndices.value()          = {0};
                acq.triggerTimestamps.value()       = {0}; // {std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count()};
                acq.triggerOffsets.value()          = {0.f};
                acq.triggerYamlPropertyMaps.value() = {{"subscription-error", rep.error}};
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
        };
        _client.request(command);
    }

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
        // GR doesn't set the state for a block added after the scheduler started
        // if (Parent::state() != gr::lifecycle::State::RUNNING) {
        //     std::print("<<RemoteSource.hpp>> We didn't get a running lifetime from GR\n");
        //     return; // early return, only apply settings for the running flowgraph
        // }
        const auto oldHost  = old_settings.find("host");
        const auto oldValue = old_settings.find("remote_uri");
        if ((oldValue != old_settings.end() || oldHost == old_settings.end()) && !host.empty() && !remote_uri.empty()) {
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
}; // namespace opendigitizer

template<typename T>
requires std::is_floating_point_v<T>
struct RemoteDataSetSource : RemoteSourceBase, gr::Block<RemoteDataSetSource<T>> {
    using Parent = gr::Block<RemoteDataSetSource<T>>;
    gr::PortOut<gr::DataSet<T>> out;

    opencmw::client::RestClient _client = opencmw::client::RestClient(opencmw::client::VerifyServerCertificates(Digitizer::Settings::instance().checkCertificates));
    std::string                 _subscribedUri;

    gr::Annotated<bool, "verbose console", gr::Doc<"For debugging">> verbose_console = false;

    std::optional<std::chrono::system_clock::time_point> _reconnect{};

    GR_MAKE_REFLECTABLE(RemoteDataSetSource, out, remote_uri, host, verbose_console);
    struct Queue {
        std::deque<gr::DataSet<T>> data;
        std::mutex                 mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    RemoteDataSetSource(gr::property_map props) : Parent(std::move(props)) {}

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        if (_reconnect.has_value() && _reconnect.value() < std::chrono::system_clock::now() && !host.empty() && !remote_uri.empty()) {
            startSubscription(remote_uri);
            _reconnect.reset();
        }

        std::lock_guard           lock(_queue->mutex);
        const auto                n       = std::min(_queue->data.size(), output.size());
        std::span<gr::DataSet<T>> outSpan = output;
        for (auto i = 0UZ; i < n; ++i) {
            outSpan[i] = std::move(_queue->data.front());
            _queue->data.pop_front();
        }
        output.publish(n);
        return gr::work::Status::OK;
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
                _reconnect = std::chrono::system_clock::now() + 5s;
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
                gr::DataSet<T> ds;
                auto           convert = [](float v) { return static_cast<T>(v); };

                ds.timestamp = acq.acqLocalTimeStamp.value(); // UTC timestamp [ns]

                // axis layout:
                ds.axis_names = {"x-axis"};
                ds.axis_units = {"a.u."};
                ds.axis_values.resize(1UZ);
                auto axisValues     = acq.channelTimeBase | std::views::transform(convert);
                ds.axis_values[0UZ] = {axisValues.begin(), axisValues.end()};

                // signal data layout:
                ds.extents = {static_cast<int32_t>(acq.channelValue.size())};
                ds.layout  = gr::LayoutRight{};

                // signal data storage:
                ds.signal_names      = {acq.channelName};
                ds.signal_quantities = {acq.channelQuantity};
                ds.signal_units      = {acq.channelUnit};
                auto signalValues    = acq.channelValue | std::views::transform(convert);
                ds.signal_values     = {signalValues.begin(), signalValues.end()};
                // auto errors      = acq.channelError | std::views::transform(convert); // TODO: If type is uncertain value, use values and errors to initialize
                ds.signal_ranges = {{convert(acq.channelRangeMin.value()), convert(acq.channelRangeMax.value())}};

                // meta data
                ds.meta_information.push_back({{"subscription-updates-skipped", static_cast<uint64_t>(skipped_samples)}});
                ds.timing_events.resize(1UZ);

                for (const auto& [idx, yaml] : std::views::zip(acq.triggerIndices.value(), acq.triggerYamlPropertyMaps.value())) {
                    const auto yamlMap = pmtv::yaml::deserialize(yaml);
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
        };
        _client.request(command);
    }

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
        // GR doesn't set the state for a block added after the scheduler started
        // if (Parent::state() != gr::lifecycle::State::RUNNING) {
        //     std::print("<<RemoteSource.hpp>> We didn't get a running lifetime from GR\n");
        //     return; // early return, only apply settings for the running flowgraph
        // }
        const auto old_host = old_settings.find("host");
        const auto oldValue = old_settings.find("remote_uri");
        if ((oldValue != old_settings.end() || old_host == old_settings.end()) && !host.empty() && !remote_uri.empty()) {
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

inline auto registerRemoteStreamSource  = gr::registerBlock<opendigitizer::RemoteStreamSource, float>(gr::globalBlockRegistry());
inline auto registerRemoteDataSetSource = gr::registerBlock<opendigitizer::RemoteDataSetSource, float>(gr::globalBlockRegistry());

#endif
