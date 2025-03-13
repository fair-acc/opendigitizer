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
        return std::unexpected(fmt::format("Inconvertible type for tag '{}', received type {} not convertible to  {}", key, std::visit<>([]<typename V>(V& value) { return gr::meta::type_name<V>(); }, it->second), gr::meta::type_name<T>()));
    }
}

inline std::string findTriggerName(const std::vector<std::pair<std::ptrdiff_t, gr::property_map>>& tags) {
    for (const auto& [diff, map] : tags) {
        if (auto triggerNameIt = map.find(std::string(gr::tag::TRIGGER_NAME.shortKey())); triggerNameIt != map.end()) {
            const auto name = std::get<std::string>(triggerNameIt->second);

            if (auto triggerMetaInfoIt = map.find(std::string(gr::tag::TRIGGER_META_INFO.shortKey())); triggerMetaInfoIt != map.end()) {
                const auto meta = std::get<gr::property_map>(triggerMetaInfoIt->second);
                if (auto contextIt = meta.find(std::string(gr::tag::CONTEXT.shortKey())); contextIt != meta.end()) {
                    const auto context = std::get<std::string>(contextIt->second);
                    if (!context.empty()) {
                        return name + "/" + context;
                    }
                }
            }

            if (auto contextIt = map.find(std::string(gr::tag::CONTEXT.shortKey())); contextIt != map.end()) {
                const auto context = std::get<std::string>(contextIt->second);
                if (!context.empty()) {
                    return name + "/" + context;
                }
            }

            return name;
        }
    }

    return {};
}

template<typename T>
inline std::optional<T> getSetting(const gr::BlockModel& block, const std::string& key) {
    try {
        const auto setting = block.settings().get(key);
        if (!setting) {
            return {};
        }
        return std::get<T>(*setting);
    } catch (const std::exception& e) {
        fmt::println(std::cerr, "Unexpected type for '{}' property", key);
        return {};
    }
}

} // namespace detail

using namespace gr;
using namespace gr::message;
using enum gr::message::Command;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;

enum class AcquisitionMode { Continuous, Triggered, Multiplexed, Snapshot, DataSet };

constexpr AcquisitionMode parseAcquisitionMode(std::string_view v) {
    using enum AcquisitionMode;
    if (v == "continuous") {
        return Continuous;
    }
    if (v == "triggered") {
        return Triggered;
    }
    if (v == "multiplexed") {
        return Multiplexed;
    }
    if (v == "snapshot") {
        return Snapshot;
    }
    if (v == "dataset") {
        return DataSet;
    }
    throw std::invalid_argument(fmt::format("Invalid acquisition mode '{}'", v));
}

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
        const auto  maybeName = tag.get(std::string(gr::tag::TRIGGER_NAME.shortKey()));
        const auto  name      = maybeName ? std::get<std::string>(maybeName->get()) : "<unset>"s;
        const auto  maybeMeta = tag.get(std::string(gr::tag::TRIGGER_META_INFO.shortKey()));
        std::string context   = "<undefined>";
        if (maybeMeta) {
            const auto meta = std::get<gr::property_map>(maybeMeta->get());
            auto       it   = meta.find(gr::tag::CONTEXT.shortKey());
            if (it != meta.end()) {
                context = std::get<std::string>(it->second);
            }
        }

        if (context.empty()) {
            const auto maybeContext = tag.get(std::string(gr::tag::CONTEXT.shortKey()));
            context                 = maybeContext ? std::get<std::string>(maybeContext->get()) : "<undefined>";
        }

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
    std::shared_ptr<thread_pool::BasicThreadPool>                                 _threadPool = std::make_shared<thread_pool::BasicThreadPool>("scheduler_Pool", gr::thread_pool::CPU_BOUND, 2, 2);

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
                    pendingFlowGraph->forEachBlock([&signalEntryBySink](const auto& block) {
                        if (block.typeName().starts_with("gr::basic::DataSink")) {
                            SignalEntry& entry = signalEntryBySink[std::string(block.uniqueName())];
                            entry.type         = SignalType::Plain;
                            entry.name         = detail::getSetting<std::string>(block, "signal_name").value_or("");
                            entry.quantity     = detail::getSetting<std::string>(block, "signal_quantity").value_or("");
                            entry.unit         = detail::getSetting<std::string>(block, "signal_unit").value_or("");
                            entry.sample_rate  = detail::getSetting<float>(block, "sample_rate").value_or(1.f);
                        } else if (block.typeName().starts_with("gr::basic::DataSetSink")) {
                            SignalEntry& entry = signalEntryBySink[std::string(block.uniqueName())];
                            entry.type         = SignalType::DataSet;
                            entry.name         = detail::getSetting<std::string>(block, "signal_name").value_or("");
                            entry.sample_rate  = detail::getSetting<float>(block, "sample_rate").value_or(1.f);
                        }
                    });
                    if (_updateSignalEntriesCallback) {
                        auto entries = signalEntryBySink | std::views::values;
                        _updateSignalEntriesCallback(std::vector(entries.begin(), entries.end()));
                    }
                    fmt::print("There is a pendingFlowGraph -- new sched\n");
                    _scheduler             = std::make_unique<scheduler::Simple<scheduler::ExecutionPolicy::multiThreaded>>(std::move(*pendingFlowGraph), _threadPool);
                    _messagesToScheduler   = std::make_unique<MsgPortOut>();
                    _messagesFromScheduler = std::make_unique<MsgPortIn>();
                    std::ignore            = _messagesToScheduler->connect(_scheduler->msgIn);
                    std::ignore            = _scheduler->msgOut.connect(*_messagesFromScheduler);
                    schedulerUniqueName    = _scheduler->unique_name;
                    sendMessage<Subscribe>(*_messagesToScheduler, schedulerUniqueName, block::property::kLifeCycleState, {}, "GnuRadioWorker");
                    sendMessage<Subscribe>(*_messagesToScheduler, "", block::property::kSetting, {}, "GnuRadioWorker");

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
                const auto acquisitionMode = parseAcquisitionMode(filterIn.acquisitionModeFilter);
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
                fmt::println(std::cerr, "Could not handle subscription {}: {}", subscription.toZmqTopic(), e.what());
            }
        }
        return pollersFinished;
    }

    auto getStreamingPoller(std::map<PollerKey, StreamingPollerEntry>& pollers, std::string_view signalName, std::size_t minRequiredSamples = 40, std::size_t maxRequiredSamples = std::numeric_limits<std::size_t>::max()) {
        const auto key = PollerKey{.mode = AcquisitionMode::Continuous, .signal_name = std::string(signalName)};

        auto pollerIt = pollers.find(key);
        if (pollerIt == pollers.end()) {
            const auto query = basic::DataSinkQuery::signalName(signalName);
            pollerIt         = pollers.emplace(key, basic::DataSinkRegistry::instance().getStreamingPoller<StreamingPollerEntry::SampleType>(query, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples})).first;
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
            reply.acqTriggerName            = "STREAMING";
            reply.channelName               = pollerEntry.signal_name.value_or(std::string(signalName));
            reply.channelUnit               = pollerEntry.signal_unit.value_or("N/A");
            // work around fix the Annotated::operator= ambiguity here (move vs. copy assignment) when creating a temporary unit here
            // Should be fixed in Annotated (templated forwarding assignment operator=?)/or go for gnuradio4's Annotated?
            const typename decltype(reply.channelRangeMin)::R rangeMin = pollerEntry.signal_min ? static_cast<float>(*pollerEntry.signal_min) : std::numeric_limits<float>::lowest();
            const typename decltype(reply.channelRangeMax)::R rangeMax = pollerEntry.signal_max ? static_cast<float>(*pollerEntry.signal_max) : std::numeric_limits<float>::max();
            reply.channelRangeMin                                      = rangeMin;
            reply.channelRangeMax                                      = rangeMax;
            reply.channelValue.resize(data.size());
            reply.channelError.resize(data.size());
            reply.channelTimeBase.resize(data.size());
            std::ranges::copy(data, reply.channelValue.begin());
            std::fill(reply.channelError.begin(), reply.channelError.end(), 0.f);     // TODO
            std::fill(reply.channelTimeBase.begin(), reply.channelTimeBase.end(), 0); // TODO
            // preallocate trigger vectors to number of tags
            reply.triggerIndices.reserve(tags.size());
            reply.triggerEventNames.reserve(tags.size());
            reply.triggerTimestamps.reserve(tags.size());
            reply.triggerOffsets.reserve(tags.size());
            reply.triggerYamlPropertyMaps.reserve(tags.size());
            for (auto& [idx, tagMap] : tags) {
                if (tagMap.contains(gr::tag::TRIGGER_NAME.shortKey()) && tagMap.contains(gr::tag::TRIGGER_TIME.shortKey())) {
                    if (reply.acqLocalTimeStamp == 0) { // just take the value of the first tag. probably should correct for the tag index times samplerate
                        reply.acqLocalTimeStamp = std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME.shortKey()));
                    }
                    reply.triggerIndices.push_back(static_cast<int64_t>(idx));
                    reply.triggerEventNames.push_back(std::get<std::string>(tagMap.at(gr::tag::TRIGGER_NAME.shortKey())));
                    reply.triggerTimestamps.push_back(static_cast<int64_t>(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME.shortKey()))));
                    if (tagMap.contains(gr::tag::TRIGGER_OFFSET.shortKey())) {
                        reply.triggerOffsets.push_back(std::get<float>(tagMap.at(gr::tag::TRIGGER_OFFSET.shortKey())));
                    } else {
                        reply.triggerOffsets.push_back(0.0f);
                    }
                    reply.triggerYamlPropertyMaps.push_back(pmtv::yaml::serialize(tagMap));
                } else {
                    reply.triggerIndices.push_back(cast_to_signed(idx));
                    reply.triggerEventNames.push_back("");
                    reply.triggerTimestamps.push_back(0ULL);
                    reply.triggerOffsets.push_back(0.f);
                    reply.triggerYamlPropertyMaps.push_back(pmtv::yaml::serialize(tagMap));
                }
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
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getTriggerPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, //
                                                    {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .preSamples = key.pre_samples, .postSamples = key.post_samples, })).first; //
                // clang-format on
            } else if (mode == AcquisitionMode::Snapshot) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getSnapshotPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .delay = key.snapshot_delay})).first;
            } else if (mode == AcquisitionMode::Multiplexed) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getMultiplexedPoller<SampleType>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples, .maximumWindowSize = key.maximum_window_size})).first;
            } else if (mode == AcquisitionMode::DataSet) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getDataSetPoller<SampleType>(query, {.minRequiredSamples = minRequiredSamples, .maxRequiredSamples = maxRequiredSamples})).first;
            }
        }
        return pollerIt;
    }

    // TODO: this is a workaround method to parse signal names for a DataSet: `signalName::index`
    std::pair<std::string, std::size_t> parseSignalNameForDataSet(std::string_view input) {
        const std::string delimiter = "::";
        std::size_t       pos       = input.find(delimiter);
        if (pos == std::string_view::npos) {
            return {std::string(input), 0UZ};
        }

        std::string_view namePart  = input.substr(0, pos);
        std::string_view indexPart = input.substr(pos + delimiter.size());

        if (indexPart.empty()) {
            return {std::string(namePart), 0UZ};
        }

        std::size_t index{};
        if (auto [_, ec] = std::from_chars(indexPart.data(), indexPart.data() + indexPart.size(), index); ec != std::errc()) {
            return {std::string(namePart), 0UZ};
        }
        return {std::string(namePart), index};
    }

    bool handleDataSetSubscription(std::map<PollerKey, DataSetPollerEntry>& pollers, const TimeDomainContext& context, AcquisitionMode mode, std::string_view signalName_) {
        std::string signalName(signalName_);
        std::size_t signalIndex = 0UZ;

        if (mode == AcquisitionMode::DataSet) {
            auto parseRes = parseSignalNameForDataSet(signalName_);
            signalName    = parseRes.first;
            signalIndex   = parseRes.second;
        }
        auto pollerIt = getDataSetPoller(pollers, context, mode, signalName);
        if (pollerIt == pollers.end()) { // flushing, do not create new pollers
            return true;
        }

        const auto& key         = pollerIt->first;
        auto&       pollerEntry = pollerIt->second;

        if (pollerEntry.poller == nullptr) {
            return true;
        }
        Acquisition reply;
        auto        processData = [&reply, &key, signalName, &pollerEntry, &signalIndex](std::span<const gr::DataSet<DataSetPollerEntry::SampleType>> dataSets) {
            const auto& dataSet = dataSets[0];

            if (!dataSet.timing_events.empty()) {
                reply.acqTriggerName = detail::findTriggerName(dataSet.timing_events[0]);
                fmt::print("Setting acqTriggerName to {}\n", reply.acqTriggerName);
            }
            reply.channelName     = std::string(signalName);
            reply.channelQuantity = dataSet.signal_quantities.size() > signalIndex ? dataSet.signal_quantities[signalIndex] : "";
            reply.channelUnit     = dataSet.signal_units.size() > signalIndex ? dataSet.signal_units[signalIndex] : "N/A";

            if (dataSet.signal_ranges.size() > signalIndex) {
                // Workaround for Annotated, see above
                const typename decltype(reply.channelRangeMin)::R rangeMin = dataSet.signal_ranges[signalIndex].min;
                const typename decltype(reply.channelRangeMax)::R rangeMax = dataSet.signal_ranges[signalIndex].max;
                reply.channelRangeMin                                      = rangeMin;
                reply.channelRangeMax                                      = rangeMax;
            }
            auto values = std::span(dataSet.signal_values);

            if (key.mode == AcquisitionMode::DataSet) {
                const auto samplesPerSignal = static_cast<std::size_t>(dataSet.extents[1]);
                const auto offset           = signalIndex * samplesPerSignal;
                const auto nValues          = offset + samplesPerSignal <= values.size() ? samplesPerSignal : 0;
                values                      = values.subspan(offset, nValues);
            }

            reply.channelValue.resize(values.size());
            std::ranges::copy(values, reply.channelValue.begin());
            reply.channelError.resize(0);
            reply.channelTimeBase.resize(values.size());
            std::fill(reply.channelTimeBase.begin(), reply.channelTimeBase.end(), 0); // TODO
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
