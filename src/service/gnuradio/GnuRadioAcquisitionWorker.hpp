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

namespace opendigitizer::gnuradio {

using namespace opendigitizer::acq;

namespace detail {
template<typename T>
inline std::optional<T> get(const gr::property_map& m, const std::string_view& key) {
    const auto it = m.find(std::string(key));
    if (it == m.end()) {
        return {};
    }

    try {
        return std::get<T>(it->second);
    } catch (const std::exception& e) {
        fmt::println(std::cerr, "Unexpected type for tag '{}'", key);
        return {};
    }
}
inline float doubleToFloat(double v) { return static_cast<float>(v); }

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
    std::size_t              pre_samples         = 0;                           // Trigger
    std::size_t              post_samples        = 0;                           // Trigger
    std::size_t              maximum_window_size = 0;                           // Multiplexed
    std::chrono::nanoseconds snapshot_delay      = std::chrono::nanoseconds(0); // Snapshot

    auto operator<=>(const PollerKey&) const noexcept = default;
};

struct StreamingPollerEntry {
    using SampleType                                               = double;
    bool                                                    in_use = true;
    std::shared_ptr<gr::basic::StreamingPoller<SampleType>> poller;
    std::optional<std::string>                              signal_name;
    std::optional<std::string>                              signal_unit;
    std::optional<float>                                    signal_min;
    std::optional<float>                                    signal_max;

    explicit StreamingPollerEntry(std::shared_ptr<basic::StreamingPoller<SampleType>> p) : poller{p} {}

    void populateFromTags(std::span<const gr::Tag>& tags) {
        for (const auto& tag : tags) {
            if (const auto name = detail::get<std::string>(tag.map, tag::SIGNAL_NAME.shortKey())) {
                signal_name = name;
            }
            if (const auto unit = detail::get<std::string>(tag.map, tag::SIGNAL_UNIT.shortKey())) {
                signal_unit = unit;
            }
            if (const auto min = detail::get<float>(tag.map, tag::SIGNAL_MIN.shortKey())) {
                signal_min = min;
            }
            if (const auto max = detail::get<float>(tag.map, tag::SIGNAL_MAX.shortKey())) {
                signal_max = max;
            }
        }
    }
};

enum class SignalType {
    DataSet, ///< DataSet stream, only allows acquisition mode "dataset"
    Plain    ///< Plain data, allows all acquisition modes other than "dataset"
};

struct SignalEntry {
    std::string name;
    std::string unit;
    float       sample_rate;
    SignalType  type;

    auto operator<=>(const SignalEntry&) const noexcept = default;
};

namespace detail {
std::vector<SignalEntry> entriesFromSettings(const std::optional<std::vector<std::string>>& names, const std::optional<std::vector<std::string>>& units) {
    if (!names || !units) {
        return {};
    }
    const auto               n = std::min(names->size(), units->size());
    std::vector<SignalEntry> entries{n};
    for (auto i = 0UZ; i < n; ++i) {
        entries[i].type        = SignalType::DataSet;
        entries[i].name        = names.value()[i];
        entries[i].unit        = units.value()[i];
        entries[i].sample_rate = 1.f; // no sample rate information available for data sets
    }
    return entries;
}

struct Matcher {
    std::string          filterDefinition;
    trigger::MatchResult operator()(std::string_view, const Tag& tag, property_map& filterState) {
        const auto  maybeName = tag.get(std::string(gr::tag::TRIGGER_NAME.shortKey()));
        const auto  name      = maybeName ? std::get<std::string>(maybeName->get()) : "<unset>"s;
        const auto  maybeMeta = tag.get(std::string(gr::tag::TRIGGER_META_INFO.shortKey()));
        std::string context   = "<undefined>";
        if (maybeMeta) {
            const auto m  = std::get<gr::property_map>(maybeMeta->get());
            auto       it = m.find(gr::tag::CONTEXT.shortKey());
            if (it != m.end()) {
                context = std::get<std::string>(it->second);
            }
        }
        fmt::println("Matching {} against '{}' / '{}'", filterDefinition, name, context);
        return trigger::BasicTriggerNameCtxMatcher::filter(filterDefinition, tag, filterState);
    }
};

} // namespace detail

struct DataSetPollerEntry {
    using SampleType = double;
    std::shared_ptr<gr::basic::DataSetPoller<SampleType>> poller;
    bool                                                  in_use = false;
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
            auto update = std::chrono::system_clock::now();
            // TODO: current load_grc creates Foo<double> types no matter what the original type was
            // when supporting more types, we need some type erasure here
            std::map<PollerKey, StreamingPollerEntry>       streamingPollers;
            std::map<PollerKey, DataSetPollerEntry>         dataSetPollers;
            std::jthread                                    schedulerThread;
            std::string                                     schedulerUniqueName;
            std::map<std::string, std::vector<SignalEntry>> signalEntriesBySink;
            std::unique_ptr<MsgPortOut>                     toScheduler;
            std::unique_ptr<MsgPortIn>                      fromScheduler;

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
                            if (state == magic_enum::enum_name(lifecycle::State::STOPPED)) {
                                schedulerFinished = true;
                                continue;
                            }
                        } else if (message.endpoint == block::property::kSetting) {
                            auto sinkIt = signalEntriesBySink.find(message.serviceName);
                            if (sinkIt == signalEntriesBySink.end()) {
                                continue;
                            }
                            const auto& settings = message.data;
                            if (!settings) {
                                continue;
                            }
                            auto& entries = sinkIt->second;

                            const auto signalNames = detail::get<std::vector<std::string>>(*settings, "signal_names");
                            const auto signalUnits = detail::get<std::vector<std::string>>(*settings, "signal_units");
                            if (signalNames && signalUnits) {
                                // assume DataSetSink
                                auto newEntries = detail::entriesFromSettings(signalNames, signalUnits);
                                if (entries != newEntries) {
                                    entries           = std::move(newEntries);
                                    signalInfoChanged = true;
                                }
                            } else {
                                // Assume DataSink
                                entries.resize(1);
                                auto& entry            = entries[0];
                                entry.type             = SignalType::Plain;
                                const auto signal_name = detail::get<std::string>(*settings, "signal_name");
                                const auto signal_unit = detail::get<std::string>(*settings, "signal_unit");
                                const auto sample_rate = detail::get<float>(*settings, "sample_rate");
                                if (signal_name && signal_name != entry.name) {
                                    entry.name        = *signal_name;
                                    signalInfoChanged = true;
                                }
                                if (signal_unit && signal_unit != entry.unit) {
                                    entry.unit        = *signal_unit;
                                    signalInfoChanged = true;
                                }
                                if (sample_rate && sample_rate != entry.sample_rate) {
                                    entry.sample_rate = *sample_rate;
                                    signalInfoChanged = true;
                                }
                            }
                        }
                    }

                    std::ignore = messages.consume(messages.size());

                    if (signalInfoChanged && _updateSignalEntriesCallback) {
                        auto flattened = signalEntriesBySink | std::views::values | std::views::join;
                        _updateSignalEntriesCallback(std::vector(flattened.begin(), flattened.end()));
                    }

                    bool pollersFinished = true;
                    do {
                        pollersFinished = true;
                        for (auto& [_, pollerEntry] : streamingPollers) {
                            pollerEntry.in_use = false;
                        }
                        for (auto& [_, pollerEntry] : dataSetPollers) {
                            pollerEntry.in_use = false;
                        }
                        pollersFinished = handleSubscriptions(streamingPollers, dataSetPollers);
                        // drop pollers of old subscriptions to avoid the sinks from blocking
                        std::erase_if(streamingPollers, [](const auto& item) { return !item.second.in_use; });
                        std::erase_if(dataSetPollers, [](const auto& item) { return !item.second.in_use; });
                    } while (stopScheduler && !pollersFinished);
                }

                if (stopScheduler || schedulerFinished) {
                    if (_updateSignalEntriesCallback) {
                        _updateSignalEntriesCallback({});
                    }
                    signalEntriesBySink.clear();
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
                    pendingFlowGraph->forEachBlock([&signalEntriesBySink](const auto& block) {
                        if (block.typeName().starts_with("gr::basic::DataSink")) {
                            auto& entries = signalEntriesBySink[std::string(block.uniqueName())];
                            entries.resize(1);
                            auto& entry       = entries[0];
                            entry.type        = SignalType::Plain;
                            entry.name        = detail::getSetting<std::string>(block, "signal_name").value_or("");
                            entry.unit        = detail::getSetting<std::string>(block, "signal_unit").value_or("");
                            entry.sample_rate = detail::getSetting<float>(block, "sample_rate").value_or(1.f);
                        } else if (block.typeName().starts_with("gr::basic::DataSetSink")) {
                            auto&      entries = signalEntriesBySink[std::string(block.uniqueName())];
                            const auto names   = detail::getSetting<std::vector<std::string>>(block, "signal_names");
                            const auto units   = detail::getSetting<std::vector<std::string>>(block, "signal_units");
                            entries            = detail::entriesFromSettings(names, units);
                        }
                    });
                    if (_updateSignalEntriesCallback) {
                        auto flattened = signalEntriesBySink | std::views::values | std::views::join;
                        _updateSignalEntriesCallback(std::vector(flattened.begin(), flattened.end()));
                    }
                    _scheduler             = std::make_unique<scheduler::Simple<scheduler::ExecutionPolicy::multiThreaded>>(std::move(*pendingFlowGraph));
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

    auto getStreamingPoller(std::map<PollerKey, StreamingPollerEntry>& pollers, std::string_view signalName) {
        const auto key = PollerKey{.mode = AcquisitionMode::Continuous, .signal_name = std::string(signalName)};

        auto pollerIt = pollers.find(key);
        if (pollerIt == pollers.end()) {
            const auto query = basic::DataSinkQuery::signalName(signalName);
            pollerIt         = pollers.emplace(key, basic::DataSinkRegistry::instance().getStreamingPoller<double>(query)).first;
        }
        return pollerIt;
    }

    bool handleStreamingSubscription(std::map<PollerKey, StreamingPollerEntry>& pollers, const TimeDomainContext& context, std::string_view signalName) {
        auto pollerIt = getStreamingPoller(pollers, signalName);
        if (pollerIt == pollers.end()) { // flushing, do not create new pollers
            return true;
        }

        auto& pollerEntry = pollerIt->second;

        if (!pollerEntry.poller) {
            return true;
        }
        Acquisition reply;

        auto key         = pollerIt->first;
        auto processData = [&reply, signalName, &pollerEntry, &key](std::span<const double> data, std::span<const gr::Tag> tags) {
            pollerEntry.populateFromTags(tags);
            reply.acqTriggerName = "STREAMING";
            reply.channelName    = pollerEntry.signal_name.value_or(std::string(signalName));
            reply.channelUnit    = pollerEntry.signal_unit.value_or("N/A");
            // work around fix the Annotated::operator= ambiguity here (move vs. copy assignment) when creating a temporary unit here
            // Should be fixed in Annotated (templated forwarding assignment operator=?)/or go for gnuradio4's Annotated?
            const typename decltype(reply.channelRangeMin)::R rangeMin = pollerEntry.signal_min ? static_cast<float>(*pollerEntry.signal_min) : std::numeric_limits<float>::lowest();
            const typename decltype(reply.channelRangeMax)::R rangeMax = pollerEntry.signal_max ? static_cast<float>(*pollerEntry.signal_max) : std::numeric_limits<float>::max();
            reply.channelRangeMin                                      = rangeMin;
            reply.channelRangeMax                                      = rangeMax;
            reply.channelValue.resize(data.size());
            reply.channelError.resize(data.size());
            reply.channelTimeBase.resize(data.size());
            std::ranges::transform(data, reply.channelValue.begin(), detail::doubleToFloat);
            std::ranges::copy(data, reply.channelValue.begin());
            std::fill(reply.channelError.begin(), reply.channelError.end(), 0.f);     // TODO
            std::fill(reply.channelTimeBase.begin(), reply.channelTimeBase.end(), 0); // TODO
            // preallocate trigger vectors to number of tags
            reply.triggerIndices.reserve(tags.size());
            reply.triggerEventNames.reserve(tags.size());
            reply.triggerTimestamps.reserve(tags.size());
            for (auto& [idx, tagMap] : tags) {
                if (tagMap.contains(gr::tag::TRIGGER_NAME) && tagMap.contains(gr::tag::TRIGGER_TIME)) {
                    if (std::get<std::string>(tagMap.at(gr::tag::TRIGGER_NAME)) == "systemtime") {
                        reply.acqLocalTimeStamp  = std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME));
                        auto       dataTimestamp = std::chrono::nanoseconds(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME)));
                        const auto now           = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::high_resolution_clock::now().time_since_epoch());
                        auto       latency       = (dataTimestamp.count() == 0) ? 0ns : now - dataTimestamp;
                    }
                    reply.triggerIndices.push_back(idx);
                    reply.triggerEventNames.push_back(std::get<std::string>(tagMap.at(gr::tag::TRIGGER_NAME)));
                    reply.triggerTimestamps.push_back(static_cast<int64_t>(std::get<uint64_t>(tagMap.at(gr::tag::TRIGGER_TIME))));
                }
            }
            reply.triggerIndices.shrink_to_fit();
            reply.triggerEventNames.shrink_to_fit();
            reply.triggerTimestamps.shrink_to_fit();
        };
        pollerEntry.in_use = true;

        const auto wasFinished = pollerEntry.poller->finished.load();
        if (pollerEntry.poller->process(processData)) {
            super_t::notify(context, reply);
        }
        return wasFinished;
    }

    auto getDataSetPoller(std::map<PollerKey, DataSetPollerEntry>& pollers, const TimeDomainContext& context, AcquisitionMode mode, std::string_view signalName) {
        const auto key      = PollerKey{.mode = mode, .signal_name = std::string(signalName), .pre_samples = static_cast<std::size_t>(context.preSamples), .post_samples = static_cast<std::size_t>(context.postSamples), .maximum_window_size = static_cast<std::size_t>(context.maximumWindowSize), .snapshot_delay = std::chrono::nanoseconds(context.snapshotDelay)};
        auto       pollerIt = pollers.find(key);
        if (pollerIt == pollers.end()) {
            const auto query = basic::DataSinkQuery::signalName(signalName);
            // TODO for triggered/multiplexed subscriptions that only differ in preSamples/postSamples/maximumWindowSize, we could use a single poller for the encompassing range
            // and send snippets from their datasets to the individual subscribers
            if (mode == AcquisitionMode::Triggered) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getTriggerPoller<double>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, key.pre_samples, key.post_samples)).first;
            } else if (mode == AcquisitionMode::Snapshot) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getSnapshotPoller<double>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, key.snapshot_delay)).first;
            } else if (mode == AcquisitionMode::Multiplexed) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getMultiplexedPoller<double>(query, detail::Matcher{.filterDefinition = context.triggerNameFilter}, key.maximum_window_size)).first;
            } else if (mode == AcquisitionMode::DataSet) {
                pollerIt = pollers.emplace(key, basic::DataSinkRegistry::instance().getDataSetPoller<double>(query)).first;
            }
        }
        return pollerIt;
    }

    bool handleDataSetSubscription(std::map<PollerKey, DataSetPollerEntry>& pollers, const TimeDomainContext& context, AcquisitionMode mode, std::string_view signalName) {
        auto pollerIt = getDataSetPoller(pollers, context, mode, signalName);
        if (pollerIt == pollers.end()) { // flushing, do not create new pollers
            return true;
        }

        const auto& key         = pollerIt->first;
        auto&       pollerEntry = pollerIt->second;

        if (!pollerEntry.poller) {
            return true;
        }
        Acquisition reply;
        auto        processData = [&reply, &key, signalName, &pollerEntry](std::span<const gr::DataSet<double>> dataSets) {
            const auto& dataSet  = dataSets[0];
            const auto  signalIt = std::ranges::find(dataSet.signal_names, signalName);
            if (key.mode == AcquisitionMode::DataSet && signalIt == dataSet.signal_names.end()) {
                return;
            }
            const auto signalIndex = signalIt == dataSet.signal_names.end() ? 0UZ : static_cast<std::size_t>(std::distance(dataSet.signal_names.begin(), signalIt));
            if (!dataSet.timing_events.empty()) {
                reply.acqTriggerName = detail::findTriggerName(dataSet.timing_events[signalIndex]);
            }
            reply.channelName = (dataSet.signal_names.size() <= signalIndex) ? std::string(signalName) : dataSet.signal_names[signalIndex];
            reply.channelUnit = (dataSet.signal_units.size() <= signalIndex) ? "N/A" : dataSet.signal_units[signalIndex];
            if ((dataSet.signal_ranges.size() > signalIndex) && dataSet.signal_ranges[signalIndex].size() == 2) {
                // Workaround for Annotated, see above
                const typename decltype(reply.channelRangeMin)::R rangeMin = dataSet.signal_ranges[signalIndex][0];
                const typename decltype(reply.channelRangeMax)::R rangeMax = dataSet.signal_ranges[signalIndex][1];
                reply.channelRangeMin                                      = rangeMin;
                reply.channelRangeMax                                      = rangeMax;
            }
            auto values = std::span(dataSet.signal_values);
            auto errors = std::span(dataSet.signal_errors);

            if (key.mode == AcquisitionMode::DataSet) {
                const auto samples = static_cast<std::size_t>(dataSet.extents[1]);
                const auto offset  = signalIndex * samples;
                const auto nValues = offset + samples <= values.size() ? samples : 0;
                const auto nErrors = offset + samples <= errors.size() ? samples : 0;
                values             = values.subspan(offset, nValues);
                errors             = errors.subspan(offset, nErrors);
            }

            reply.channelValue.resize(values.size());
            std::ranges::transform(values, reply.channelValue.begin(), detail::doubleToFloat);
            reply.channelError.resize(errors.size());
            std::ranges::transform(errors, reply.channelError.begin(), detail::doubleToFloat);
            reply.channelTimeBase.resize(values.size());
            std::fill(reply.channelTimeBase.begin(), reply.channelTimeBase.end(), 0); // TODO
        };
        pollerEntry.in_use = true;

        const auto wasFinished = pollerEntry.poller->finished.load();
        while (pollerEntry.poller->process(processData, 1)) {
            fmt::println("Pushing a data set with {} samples to {}", reply.channelValue.size(), context.triggerNameFilter);
            super_t::notify(context, reply);
        }

        return wasFinished;
    }
};

} // namespace opendigitizer::gnuradio

#endif // OPENDIGITIZER_SERVICE_GNURADIOWORKER_H
