#ifndef OPENDIGITIZER_SERVICE_GNURADIOACQUISITIONWORKER_H
#define OPENDIGITIZER_SERVICE_GNURADIOACQUISITIONWORKER_H

#include "gnuradio-4.0/Message.hpp"
#include <daq_api.hpp>

#include <majordomo/Worker.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/DataSink.hpp>

#include <chrono>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>

#include <conversion.hpp>

namespace opendigitizer::gnuradio {

using namespace opendigitizer::acq;
using namespace std::string_literals;

namespace detail {
template<typename T>
inline std::expected<T, std::string> get(const gr::property_map& m, const std::string_view& key) {
    const auto it = m.find(std::string(key));
    if (it == m.end()) {
        return {};
    }

    auto res = pmtv::convert_safely<T>(it->second);
    if (res) {
        return res.value();
    } else {
        return std::unexpected(std::format("Inconvertible type for tag '{}', received type {} not convertible to  {}", key, std::visit<>([]<typename V>(V& /*value*/) { return gr::meta::type_name<V>(); }, it->second), gr::meta::type_name<T>()));
    }
}

inline auto findTrigger(const std::vector<std::pair<std::ptrdiff_t, gr::property_map>>& tags) {
    struct {
        std::string   name;
        std::uint64_t time = 0ULL;
    } result;

    for (const auto& [diff, map] : tags) {
        if (auto triggerNameIt = map.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); triggerNameIt != map.end()) {
            std::string name = std::get<std::string>(triggerNameIt->second);

            if (auto contextIt = map.find(std::string(gr::tag::CONTEXT.shortKey())); contextIt != map.end()) {
                const auto context = std::get<std::string>(contextIt->second);
                if (!context.empty()) {
                    name = std::format("{}/{}", name, context);
                }
            }

            if (auto timeIt = map.find(std::string(gr::tag::TRIGGER_TIME.shortKey())); timeIt != map.end()) {
                result = {name, std::get<std::uint64_t>(timeIt->second)};
            } else {
                result = {name, 0ULL};
            }
            break;
        }
    }

    return result;
}

template<typename T>
inline std::optional<T> getSetting(const std::shared_ptr<gr::BlockModel>& block, const std::string& key) {
    try {
        const auto setting = block->settings().get(key);
        if (!setting) {
            return {};
        }
        return std::get<T>(*setting);
    } catch (const std::exception& e) {
        std::println(std::cerr, "Unexpected type for '{}' property", key);
        return {};
    }
}

template<typename TEnum>
[[nodiscard]] inline constexpr TEnum convertToEnum(std::string_view strEnum) {
    auto enumType = magic_enum::enum_cast<TEnum>(strEnum, magic_enum::case_insensitive);
    if (!enumType.has_value()) {
        throw std::invalid_argument(std::format("Unknown value. Cannot convert string '{}' to enum '{}'", strEnum, gr::meta::type_name<TEnum>()));
    }
    return enumType.value();
}

} // namespace detail

using namespace gr;
using namespace gr::message;
using enum gr::message::Command;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;

enum class AcquisitionMode { Continuous, Triggered, Multiplexed, Snapshot, DataSet };

struct PollerKey {
    AcquisitionMode          mode;
    std::string              signal_name;
    std::size_t              pre_samples         = 0;   // Trigger
    std::size_t              post_samples        = 0;   // Trigger
    std::size_t              maximum_window_size = 0;   // Multiplexed
    std::chrono::nanoseconds snapshot_delay      = 0ns; // Snapshot

    auto operator<=>(const PollerKey&) const noexcept = default;
};

struct StreamingPollerEntry {
    using SampleType                                               = float;
    bool                                                    in_use = true;
    std::shared_ptr<gr::basic::StreamingPoller<SampleType>> poller;
    std::optional<std::string>                              signal_name;
    std::optional<std::string>                              signal_unit;
    std::optional<std::string>                              signal_quantity;
    std::optional<float>                                    signal_min;
    std::optional<float>                                    signal_max;

    explicit StreamingPollerEntry(std::shared_ptr<basic::StreamingPoller<SampleType>> p) : poller{p} {}

    std::vector<std::string> populateFromTags(std::span<const gr::Tag>& tags) {
        std::vector<std::string> errors;
        for (const auto& tag : tags) {
            if (const auto name = detail::get<std::string>(tag.map, tag::SIGNAL_NAME.shortKey())) {
                signal_name = name.value();
            } else {
                errors.push_back(name.error());
            }
            if (const auto unit = detail::get<std::string>(tag.map, tag::SIGNAL_UNIT.shortKey())) {
                signal_unit = unit.value();
            } else {
                errors.push_back(unit.error());
            }
            if (const auto quantity = detail::get<std::string>(tag.map, tag::SIGNAL_QUANTITY.shortKey())) {
                signal_quantity = quantity.value();
            } else {
                errors.push_back(quantity.error());
            }
            if (const auto min = detail::get<float>(tag.map, tag::SIGNAL_MIN.shortKey())) {
                signal_min = min.value();
            } else {
                errors.push_back(min.error());
            }
            if (const auto max = detail::get<float>(tag.map, tag::SIGNAL_MAX.shortKey())) {
                signal_max = max.value();
            } else {
                errors.push_back(max.error());
            }
        }
        return errors;
    }
};

enum class SignalType {
    DataSet, ///< DataSet stream, only allows acquisition mode "dataset"
    Plain    ///< Plain data, allows all acquisition modes other than "dataset"
};

struct SignalEntry {
    std::string name;
    std::string quantity;
    std::string unit;
    float       sample_rate;
    SignalType  type;

    auto operator<=>(const SignalEntry&) const noexcept = default;
};

namespace detail {

struct Matcher {
    std::string          filterDefinition;
    trigger::MatchResult operator()(std::string_view, const Tag& tag, property_map& filterState) {
        const auto maybeName    = tag.get(std::string(gr::tag::TRIGGER_NAME.shortKey()));
        const auto name         = maybeName ? std::get<std::string>(maybeName->get()) : "<unset>"s;
        const auto maybeContext = tag.get(std::string(gr::tag::CONTEXT.shortKey()));
        const auto context      = maybeContext ? std::get<std::string>(maybeContext->get()) : ""s;

        return trigger::BasicTriggerNameCtxMatcher::filter(filterDefinition, tag, filterState);
    }
};

} // namespace detail

struct DataSetPollerEntry {
    using SampleType = float;
    std::shared_ptr<gr::basic::DataSetPoller<SampleType>> poller;
};

template<units::basic_fixed_string serviceName, typename... Meta>
class GnuRadioAcquisitionWorker : public Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...> {
    gr::PluginLoader*                             _pluginLoader;
    std::jthread                                  _notifyThread;
    std::function<void(std::vector<SignalEntry>)> _updateSignalEntriesCallback;
    std::unique_ptr<MsgPortOut>                   _messagesToScheduler;
    std::unique_ptr<MsgPortIn>                    _messagesFromScheduler;

    std::unique_ptr<gr::Graph>                                                    _pendingFlowGraph;
    std::unique_ptr<scheduler::Simple<scheduler::ExecutionPolicy::multiThreaded>> _scheduler;
    std::mutex                                                                    _graphChangeMutex;

public:
    using super_t = Worker<serviceName, TimeDomainContext, Empty, Acquisition, Meta...>;

    explicit GnuRadioAcquisitionWorker(opencmw::URI<opencmw::STRICT> brokerAddress, const opencmw::zmq::Context& context, gr::PluginLoader* pluginLoader, std::chrono::milliseconds rate, Settings settings = {}) : super_t(std::move(brokerAddress), {}, context, std::move(settings)), _pluginLoader(pluginLoader) {
        // TODO would be useful if one can check if the external broker knows TimeDomainContext and throw an error if not
        init(rate);
    }

    template<typename BrokerType>
    explicit GnuRadioAcquisitionWorker(BrokerType& broker, gr::PluginLoader* pluginLoader, std::chrono::milliseconds rate) : super_t(broker, {}), _pluginLoader(pluginLoader) {
        // this makes sure the subscriptions are filtered correctly
        opencmw::query::registerTypes(TimeDomainContext(), broker);
        init(rate);
    }

    ~GnuRadioAcquisitionWorker() {
        _notifyThread.request_stop();
        _notifyThread.join();
    }

    void scheduleGraphChange(std::unique_ptr<gr::Graph> fg) {
        std::lock_guard lg{_graphChangeMutex};
        _pendingFlowGraph = std::move(fg);
    }

    gr::MsgPortOut& messagesToScheduler() { return *_messagesToScheduler; }
    gr::MsgPortIn&  messagesFromScheduler() { return *_messagesFromScheduler; }

    void setUpdateSignalEntriesCallback(std::function<void(std::vector<SignalEntry>)> callback) { _updateSignalEntriesCallback = std::move(callback); }

    template<typename Fn, typename Ret = std::invoke_result_t<Fn, gr::Graph&>>
    std::optional<Ret> withGraph(Fn fn) {
        std::lock_guard lg{_graphChangeMutex};
        if (!_scheduler) {
            return {};
        } else {
            return {fn(_scheduler->graph())};
        }
    }

private:
    void init(std::chrono::milliseconds rate) {
        // TODO instead of a notify thread with polling, we could also use callbacks. This would require
        // the ability to unregister callbacks though (RAII callback "handles" using shared_ptr/weak_ptr like it works for pollers??)
        _notifyThread = std::jthread([this, rate](const std::stop_token& stoken) {
            auto                                      update = std::chrono::system_clock::now();
            std::map<PollerKey, StreamingPollerEntry> streamingPollers;
            std::map<PollerKey, DataSetPollerEntry>   dataSetPollers;
            std::jthread                              schedulerThread;
            std::string                               schedulerUniqueName;
            std::map<std::string, SignalEntry>        signalEntryBySink;
            std::unique_ptr<MsgPortOut>               toScheduler;
            std::unique_ptr<MsgPortIn>                fromScheduler;

            bool finished = false;

            while (!finished) {
                const auto aboutToFinish    = stoken.stop_requested();
                auto       pendingFlowGraph = [this]() {
                    std::lock_guard lg{_graphChangeMutex};
                    return std::exchange(_pendingFlowGraph, {});
                }();
                const auto hasScheduler      = schedulerThread.joinable();
                const bool stopScheduler     = hasScheduler && (aboutToFinish || pendingFlowGraph);
                bool       schedulerFinished = false;

                if (stopScheduler) {
                    sendMessage<Set>(*_messagesToScheduler, schedulerUniqueName, block::property::kLifeCycleState, {{"state", std::string(magic_enum::enum_name(lifecycle::State::REQUESTED_STOP))}}, "");
                }

                if (hasScheduler) {
                    bool signalInfoChanged = false;
                    auto messages          = _messagesFromScheduler->streamReader().get(_messagesFromScheduler->streamReader().available());
                    for (const auto& message : messages) {
                        if (message.endpoint == block::property::kLifeCycleState) {
                            if (!message.data) {
                                continue;
                            }
                            const auto state = detail::get<std::string>(*message.data, "state");
                            if (state) {
                                if (state.value() == magic_enum::enum_name(lifecycle::State::STOPPED)) {
                                    schedulerFinished = true;
                                    continue;
                                }
                            } else {
                                // TODO
                            }
                        } else if (message.endpoint == block::property::kSetting) {
                            auto sinkIt = signalEntryBySink.find(message.serviceName);
                            if (sinkIt == signalEntryBySink.end()) {
                                continue;
                            }
                            const auto& settings = message.data;
                            if (!settings) {
                                continue;
                            }
                            SignalEntry& entry = sinkIt->second;

                            const auto signalName = detail::get<std::string>(*settings, "signal_name");
                            const auto sampleRate = detail::get<float>(*settings, "sample_rate");
                            if (signalName && signalName.value() != entry.name) {
                                entry.name        = *signalName;
                                signalInfoChanged = true;
                            }
                            if (sampleRate && sampleRate.value() != entry.sample_rate) {
                                entry.sample_rate = *sampleRate;
                                signalInfoChanged = true;
                            }
                            if (entry.type != SignalType::DataSet) {
                                const auto signalUnit = detail::get<std::string>(*settings, "signal_unit");
                                if (signalUnit && signalUnit.value() != entry.unit) {
                                    entry.unit        = *signalUnit;
                                    signalInfoChanged = true;
                                }
                                const auto signalQuantity = detail::get<std::string>(*settings, "signal_quantity");
                                if (signalQuantity && signalQuantity.value() != entry.quantity) {
                                    entry.quantity    = *signalQuantity;
                                    signalInfoChanged = true;
                                }
                            }
                        }
                    }

                    std::ignore = messages.consume(messages.size());

                    if (signalInfoChanged && _updateSignalEntriesCallback) {
                        auto entries = signalEntryBySink | std::views::values;
                        _updateSignalEntriesCallback(std::vector(entries.begin(), entries.end()));
                    }

                    bool pollersFinished = true;
                    do {
                        pollersFinished = handleSubscriptions(streamingPollers, dataSetPollers);
                    } while (stopScheduler && !pollersFinished);
                }

                if (stopScheduler || schedulerFinished) {
                    if (_updateSignalEntriesCallback) {
                        _updateSignalEntriesCallback({});
                    }
                    signalEntryBySink.clear();
                    streamingPollers.clear();
                    dataSetPollers.clear();
                    _messagesFromScheduler.reset();
                    _messagesToScheduler.reset();
                    schedulerUniqueName.clear();
                    schedulerThread.join();
                }

                if (aboutToFinish) {
                    finished = true;
                    continue;
                }

                if (pendingFlowGraph) {
                    gr::graph::forEachBlock<gr::block::Category::NormalBlock>(*pendingFlowGraph, [&signalEntryBySink](const auto& block) {
                        if (block->typeName().starts_with("gr::basic::DataSink")) {
                            SignalEntry& entry = signalEntryBySink[std::string(block->uniqueName())];
                            entry.type         = SignalType::Plain;
                            entry.name         = detail::getSetting<std::string>(block, "signal_name").value_or("");
                            entry.quantity     = detail::getSetting<std::string>(block, "signal_quantity").value_or("");
                            entry.unit         = detail::getSetting<std::string>(block, "signal_unit").value_or("");
                            entry.sample_rate  = detail::getSetting<float>(block, "sample_rate").value_or(1.f);
                        } else if (block->typeName().starts_with("gr::basic::DataSetSink")) {
                            SignalEntry& entry = signalEntryBySink[std::string(block->uniqueName())];
                            entry.type         = SignalType::DataSet;
                            entry.name         = detail::getSetting<std::string>(block, "signal_name").value_or("");
                            entry.sample_rate  = detail::getSetting<float>(block, "sample_rate").value_or(1.f);
                        }
                    });
                    if (_updateSignalEntriesCallback) {
                        auto entries = signalEntryBySink | std::views::values;
                        _updateSignalEntriesCallback(std::vector(entries.begin(), entries.end()));
                    }
                    _scheduler             = std::make_unique<scheduler::Simple<scheduler::ExecutionPolicy::multiThreaded>>(std::move(*pendingFlowGraph));
                    _messagesToScheduler   = std::make_unique<MsgPortOut>();
                    _messagesFromScheduler = std::make_unique<MsgPortIn>();
                    std::ignore            = _messagesToScheduler->connect(_scheduler->msgIn);
                    std::ignore            = _scheduler->msgOut.connect(*_messagesFromScheduler);
                    schedulerUniqueName    = _scheduler->unique_name;
                    sendMessage<Subscribe>(*_messagesToScheduler, schedulerUniqueName, block::property::kLifeCycleState, {}, "GnuRadioWorker");
                    sendMessage<Subscribe>(*_messagesToScheduler, "", block::property::kSetting, {}, "GnuRadioWorker");
                    // make sure that we once get all the updated sink settings
                    // this covers the case that the settings subscriptions are only set up after signal metadata has been propagated through the graph
                    // since the request is sent as a message, it is ensured that the subscription was set up beforehand
                    for (auto& sinkBlockName : signalEntryBySink | std::views::keys) {
                        sendMessage<Get>(*_messagesToScheduler, sinkBlockName, block::property::kSetting, {});
                    }

                    {
                        std::lock_guard lg{_graphChangeMutex};
                        schedulerThread = std::jthread([scheduler = _scheduler.get()] { scheduler->runAndWait(); });
                    }
                }

                const auto next_update = update + rate;
                const auto now         = std::chrono::system_clock::now();
                if (now < next_update) {
                    std::this_thread::sleep_for(next_update - now);
                }
                update = next_update;
            }
        });
    }

    bool handleSubscriptions(std::map<PollerKey, StreamingPollerEntry>& streamingPollers, std::map<PollerKey, DataSetPollerEntry>& dataSetPollers) {
        bool pollersFinished = true;
        for (const auto& subscription : super_t::activeSubscriptions()) {
            const auto filterIn = opencmw::query::deserialise<TimeDomainContext>(subscription.params());
            try {
                const auto acquisitionMode = detail::convertToEnum<AcquisitionMode>(filterIn.acquisitionModeFilter);
                for (std::string_view signalName : filterIn.channelNameFilter | std::ranges::views::split(',') | std::ranges::views::transform([](const auto&& r) { return std::string_view{&*r.begin(), static_cast<std::size_t>(std::ranges::distance(r))}; })) {
                    if (acquisitionMode == AcquisitionMode::Continuous) {
                        if (!handleStreamingSubscription(streamingPollers, filterIn, signalName)) {
                            pollersFinished = false;
                        }
                    } else {
                        if (!handleDataSetSubscription(dataSetPollers, filterIn, acquisitionMode, signalName)) {
                            pollersFinished = false;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::println(std::cerr, "Could not handle subscription {}: {}", subscription.toZmqTopic(), e.what());
            }
        }
        return pollersFinished;
    }

    auto getStreamingPoller(std::map<PollerKey, StreamingPollerEntry>& pollers, std::string_view signalName, std::size_t minRequiredSamples = 40, std::size_t maxRequiredSamples = std::numeric_limits<std::size_t>::max()) {
        const auto key = PollerKey{.mode = AcquisitionMode::Continuous, .signal_name = std::string(signalName)};

        auto pollerIt = pollers.find(key);
        if (pollerIt == pollers.end()) {
            const auto query = basic::DataSinkQuery::signalName(signalName);
            pollerIt         = pollers.emplace(key, gr::basic::globalDataSinkRegistry().getStreamingPoller<StreamingPollerEntry::SampleType>(query, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples})).first;
        }
        return pollerIt;
    }

    bool handleStreamingSubscription(std::map<PollerKey, StreamingPollerEntry>& pollers, const TimeDomainContext& context, std::string_view signalName) {
        auto pollerIt = getStreamingPoller(pollers, signalName);
        if (pollerIt == pollers.end()) { // flushing, do not create new pollers
            return true;
        }

        auto& pollerEntry = pollerIt->second;

        if (pollerEntry.poller == nullptr) {
            return true;
        }
        Acquisition reply;

        auto key         = pollerIt->first;
        auto processData = [&reply, signalName, &pollerEntry, &key](std::span<const StreamingPollerEntry::SampleType> data, std::span<const gr::Tag> tags) {
            std::vector<std::string> errors = pollerEntry.populateFromTags(tags);
            reply.refTriggerName            = "STREAMING";
            reply.channelNames              = {pollerEntry.signal_name.value_or(std::string(signalName))};
            reply.channelUnits              = {pollerEntry.signal_unit.value_or("N/A")};
            reply.channelQuantities         = {pollerEntry.signal_quantity.value_or("N/A")};
            reply.channelRangeMin           = {pollerEntry.signal_min ? static_cast<float>(*pollerEntry.signal_min) : std::numeric_limits<float>::lowest()};
            reply.channelRangeMax           = {pollerEntry.signal_max ? static_cast<float>(*pollerEntry.signal_max) : std::numeric_limits<float>::max()};

            const auto                    nSamples = static_cast<uint32_t>(data.size());
            const std::array<uint32_t, 2> dims{1U, nSamples}; // 1 signal, N samples
            reply.channelValues = opencmw::MultiArray<float, 2>(std::vector<float>(data.begin(), data.end()), dims);
            reply.channelErrors = opencmw::MultiArray<float, 2>(std::vector<float>(nSamples, 0.f), dims);
            reply.channelTimeSinceRefTrigger.assign(nSamples, 0.f); // TODO real timeline

            // preallocate trigger vectors to number of tags
            reply.triggerIndices.reserve(tags.size());
            reply.triggerEventNames.reserve(tags.size());
            reply.triggerTimestamps.reserve(tags.size());
            reply.triggerOffsets.reserve(tags.size());
            reply.triggerYamlPropertyMaps.reserve(tags.size());
            for (auto& [idx, tagMap] : tags) {
                if (tagMap.contains(gr::tag::TRIGGER_NAME.shortKey()) && tagMap.contains(gr::tag::TRIGGER_TIME.shortKey())) {
                    // float   Ts_ns  = 1'000'000'000.f / entry.sample_rate;
                    float      Ts_ns  = 0.f; // TODO: find where sample_rate is stored
                    const auto offset = static_cast<int64_t>(static_cast<float>(idx) * Ts_ns);
                    if (reply.acqLocalTimeStamp == 0) { // just take the value of the first tag. probably should correct for the tag index times samplerate
                        reply.acqLocalTimeStamp = static_cast<int64_t>(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME.shortKey()))) - offset;
                    }
                    if (reply.refTriggerStamp == 0) { // just take the value of the first tag. probably should correct for the tag index times samplerate
                        reply.refTriggerName  = std::get<std::string>(tagMap.at(gr::tag::TRIGGER_NAME.shortKey()));
                        reply.refTriggerStamp = cast_to_signed(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME.shortKey()))) - offset;
                    }
                }
                reply.triggerIndices.push_back(cast_to_signed(idx));
                reply.triggerEventNames.push_back(tagMap.contains(gr::tag::TRIGGER_NAME.shortKey()) ? std::get<std::string>(tagMap.at(gr::tag::TRIGGER_NAME.shortKey())) : ""s);
                reply.triggerTimestamps.push_back(tagMap.contains(gr::tag::TRIGGER_TIME.shortKey()) ? static_cast<int64_t>(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME.shortKey()))) : 0LL);
                reply.triggerOffsets.push_back(tagMap.contains(gr::tag::TRIGGER_OFFSET.shortKey()) ? std::get<float>(tagMap.at(gr::tag::TRIGGER_OFFSET.shortKey())) : 0.0f);
                reply.triggerYamlPropertyMaps.push_back(pmtv::yaml::serialize(tagMap));
            }

            reply.triggerIndices.shrink_to_fit();
            reply.triggerEventNames.shrink_to_fit();
            reply.triggerTimestamps.shrink_to_fit();
            reply.triggerOffsets.shrink_to_fit();
            reply.triggerYamlPropertyMaps.shrink_to_fit();
            if (!errors.empty()) {
                reply.acqErrors = std::move(errors);
            }
        };

        const auto wasFinished = pollerEntry.poller->finished.load();
        if (pollerEntry.poller->process(processData)) {
            super_t::notify(context, reply);
        }
        return wasFinished;
    }

    auto getDataSetPoller(std::map<PollerKey, DataSetPollerEntry>& pollers, const TimeDomainContext& context, AcquisitionMode mode, std::string_view signalName, std::size_t minRequiredSamples = 1, std::size_t maxRequiredSamples = std::numeric_limits<std::size_t>::max()) {
        const auto key      = PollerKey{.mode = mode, .signal_name = std::string(signalName), .pre_samples = static_cast<std::size_t>(context.preSamples), .post_samples = static_cast<std::size_t>(context.postSamples), .maximum_window_size = static_cast<std::size_t>(context.maximumWindowSize), .snapshot_delay = std::chrono::nanoseconds(context.snapshotDelay)};
        auto       pollerIt = pollers.find(key);
        if (pollerIt == pollers.end()) {
            using SampleType = DataSetPollerEntry::SampleType;
            const auto query = basic::DataSinkQuery::signalName(signalName);
            // TODO for triggered/multiplexed subscriptions that only differ in preSamples/postSamples/maximumWindowSize, we could use a single poller for the encompassing range
            // and send snippets from their datasets to the individual subscribers
            if (mode == AcquisitionMode::Triggered) {
                // clang-format off
                pollerIt = pollers.emplace(key, basic::globalDataSinkRegistry().getTriggerPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, //
                                                    {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .preSamples = key.pre_samples, .postSamples = key.post_samples, })).first; //
                // clang-format on
            } else if (mode == AcquisitionMode::Snapshot) {
                pollerIt = pollers.emplace(key, basic::globalDataSinkRegistry().getSnapshotPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .delay = key.snapshot_delay})).first;
            } else if (mode == AcquisitionMode::Multiplexed) {
                pollerIt = pollers.emplace(key, basic::globalDataSinkRegistry().getMultiplexedPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .maximumWindowSize = key.maximum_window_size})).first;
            } else if (mode == AcquisitionMode::DataSet) {
                pollerIt = pollers.emplace(key, basic::globalDataSinkRegistry().getDataSetPoller<SampleType>(query, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples})).first;
            }
        }
        return pollerIt;
    }

    std::vector<std::string> parseSignalNameList(std::string_view input) {
        namespace views = std::ranges::views;

        auto isNotSpace = [](char ch) { return !std::isspace(static_cast<unsigned char>(ch)); };

        std::vector<std::string> result;
        for (auto subRange : input | views::split(',')) {
            std::string str(subRange.begin(), subRange.end());

            auto lead = std::ranges::find_if(str, isNotSpace);
            if (lead == str.end()) {
                continue;
            }
            auto trailRev = std::ranges::find_if(str | views::reverse, isNotSpace);
            result.emplace_back(lead, trailRev.base());
        }
        return result;
    }

    bool handleDataSetSubscription(std::map<PollerKey, DataSetPollerEntry>& pollers, const TimeDomainContext& context, AcquisitionMode mode, std::string_view signalName_) {
        const std::string signalName(signalName_);
        auto              pollerIt = getDataSetPoller(pollers, context, mode, signalName);
        if (pollerIt == pollers.end()) { // flushing, do not create new pollers
            return true;
        }

        const auto& key         = pollerIt->first;
        auto&       pollerEntry = pollerIt->second;

        if (pollerEntry.poller == nullptr) {
            return true;
        }
        Acquisition reply;
        auto        processData = [&reply, &key, signalName, &pollerEntry](std::span<const gr::DataSet<DataSetPollerEntry::SampleType>> dataSets) {
            const auto& dataSet = dataSets[0];

            if (!dataSet.timing_events.empty()) {
                const auto [triggerName, triggerTime] = detail::findTrigger(dataSet.timing_events[0]);
                reply.refTriggerName                  = triggerName;
                reply.refTriggerStamp                 = static_cast<std::int64_t>(triggerTime);
            }

            const std::size_t nSignals = static_cast<uint32_t>(dataSet.size());
            const std::size_t nSamples = static_cast<uint32_t>(dataSet.axisValues(0).size());

            reply.channelNames.clear();
            reply.channelNames.reserve(nSignals);
            reply.channelQuantities.clear();
            reply.channelQuantities.reserve(nSignals);
            reply.channelUnits.clear();
            reply.channelUnits.reserve(nSignals);
            reply.channelRangeMin.clear();
            reply.channelRangeMin.reserve(nSignals);
            reply.channelRangeMax.clear();
            reply.channelRangeMax.reserve(nSignals);
            reply.status.clear();
            reply.status.reserve(nSignals);
            reply.temperature.clear();
            reply.temperature.reserve(nSignals);

            for (std::size_t i = 0; i < nSignals; i++) {
                reply.channelNames.push_back(std::string(dataSet.signalName(i)));
                reply.channelQuantities.push_back(std::string(dataSet.signalQuantity(i)));
                reply.channelUnits.push_back(std::string(dataSet.signalUnit(i)));

                const auto& range = dataSet.signalRange(i);
                reply.channelRangeMin.push_back(static_cast<float>(range.min));
                reply.channelRangeMax.push_back(static_cast<float>(range.max));
            }
            // MultiArray stores internally elements as stride 1D array: <values_signal_1><values_signal_2><values_signal_3>
            std::vector<float> values;
            values.reserve(nSignals * nSamples);
            for (uint32_t i = 0; i < nSignals; ++i) {
                auto span = dataSet.signalValues(i);
                values.insert(values.end(), span.begin(), span.end());
            }
            reply.channelValues = opencmw::MultiArray<float, 2>(std::move(values), std::array<uint32_t, 2>{static_cast<uint32_t>(nSignals), static_cast<uint32_t>(nSamples)});

            std::vector<float> errors(nSignals * nSamples, 0.f);
            reply.channelErrors = opencmw::MultiArray<float, 2>(std::move(errors), std::array<uint32_t, 2>{static_cast<uint32_t>(nSignals), static_cast<uint32_t>(nSamples)});

            reply.channelTimeSinceRefTrigger = dataSet.axis_values[0];

            // copy event_timing information, TODO: now we copy all data only from timing_events[0]
            if (!dataSet.timing_events.empty()) {
                const auto& tags = dataSet.timing_events[0];
                reply.triggerIndices.reserve(tags.size());
                reply.triggerEventNames.reserve(tags.size());
                reply.triggerTimestamps.reserve(tags.size());
                reply.triggerOffsets.reserve(tags.size());
                reply.triggerYamlPropertyMaps.reserve(tags.size());
                for (auto& [idx, tagMap] : tags) {
                    reply.triggerIndices.push_back(static_cast<int64_t>(idx));
                    reply.triggerEventNames.push_back("");
                    reply.triggerTimestamps.push_back(0ULL);
                    reply.triggerOffsets.push_back(0.f);
                    reply.triggerYamlPropertyMaps.push_back(pmtv::yaml::serialize(tagMap));
                }
                reply.triggerIndices.shrink_to_fit();
                reply.triggerEventNames.shrink_to_fit();
                reply.triggerTimestamps.shrink_to_fit();
                reply.triggerOffsets.shrink_to_fit();
                reply.triggerYamlPropertyMaps.shrink_to_fit();
            }
        };

        const auto wasFinished = pollerEntry.poller->finished.load();
        while (pollerEntry.poller->process(processData, 1)) {
            super_t::notify(context, reply);
        }

        return wasFinished;
    }
};

} // namespace opendigitizer::gnuradio

#endif // OPENDIGITIZER_SERVICE_GNURADIOACQUISITIONWORKER_H
