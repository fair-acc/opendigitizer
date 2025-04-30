#ifndef OPENDIGITIZER_ACQUISITION_DAQ_API_H
#define OPENDIGITIZER_ACQUISITION_DAQ_API_H

#include <string>
#include <vector>

#include <MIME.hpp>
#include <MultiArray.hpp>
#include <opencmw.hpp>

#include <units/isq/si/frequency.h>
#include <units/isq/si/time.h>

#include "gnuradio-4.0/Message.hpp"

#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

using namespace std::string_literals;

namespace opendigitizer::flowgraph {

struct FilterContext {
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};

struct Flowgraph {
    std::string serialisedFlowgraph;
    std::string serialisedUiLayout;
};

struct SerialisedFlowgraphMessage {
    std::string data;
};

inline void storeFlowgraphToMessage(const flowgraph::Flowgraph& outFlowgraph, gr::Message& message) {
    message.data = gr::property_map{
        {"serialisedFlowgraph"s, outFlowgraph.serialisedFlowgraph}, //
        {"serialisedUiLayout"s, outFlowgraph.serialisedUiLayout}    //
    };
}

inline std::expected<flowgraph::Flowgraph, std::string> getFlowgraphFromMessage(const gr::Message& message) {
    const auto& dataMap = *message.data;
    auto        it      = dataMap.find("serialisedFlowgraph"s);
    if (it == dataMap.end()) {
        return std::unexpected("serialisedFlowgraph field not specified"s);
    }

    const auto* source = std::get_if<std::string>(&it->second);
    if (!source) {
        return std::unexpected("serialisedFlowgraph field is not a string"s);
    }

    std::string serialisedUiLayout;
    it = dataMap.find("serialisedUiLayout"s);
    if (it != dataMap.end()) {
        if (const auto* uiLayout = std::get_if<std::string>(&it->second); uiLayout) {
            serialisedUiLayout = *uiLayout;
        }
    }

    return flowgraph::Flowgraph{.serialisedFlowgraph = *source, .serialisedUiLayout = serialisedUiLayout};
}

} // namespace opendigitizer::flowgraph

ENABLE_REFLECTION_FOR(opendigitizer::flowgraph::FilterContext, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::flowgraph::Flowgraph, serialisedFlowgraph, serialisedUiLayout)
ENABLE_REFLECTION_FOR(opendigitizer::flowgraph::SerialisedFlowgraphMessage, data);

namespace opendigitizer::gnuradio {
// Yaml to gr::message and back, TODO should be moved to GR
inline std::string serialiseMessage(const gr::Message& message) {
    using namespace gr;
    property_map yaml;
    yaml["cmd"]             = std::string(magic_enum::enum_name(message.cmd));
    yaml["protocol"]        = message.protocol;
    yaml["serviceName"]     = message.serviceName;
    yaml["clientRequestID"] = message.clientRequestID;
    yaml["endpoint"]        = message.endpoint;
    yaml["rbac"]            = message.rbac;

    if (message.data) {
        yaml["data"] = message.data.value();
    } else {
        yaml["dataError"] = message.data.error().message;
    }

    return pmtv::yaml::serialize(yaml);
}

inline gr::Message deserialiseMessage(const std::string& messageYaml) {
    using namespace gr;
    const auto yaml = pmtv::yaml::deserialize(messageYaml);
    if (!yaml) {
        throw gr::exception(std::format("Could not parse yaml: {}:{}\n{}", yaml.error().message, yaml.error().line, messageYaml));
    }

    const property_map& rootMap = yaml.value();

    gr::Message message;
    auto        optionalCmd = magic_enum::enum_cast<gr::message::Command>(std::get<std::string>(rootMap.at("cmd")));
    if (optionalCmd) {
        message.cmd = *optionalCmd;
    }

    message.protocol        = std::get<std::string>(rootMap.at("protocol"));
    message.serviceName     = std::get<std::string>(rootMap.at("serviceName"));
    message.clientRequestID = std::get<std::string>(rootMap.at("clientRequestID"));
    message.endpoint        = std::get<std::string>(rootMap.at("endpoint"));
    message.rbac            = std::get<std::string>(rootMap.at("rbac"));

    if (rootMap.contains("data") && std::holds_alternative<property_map>(rootMap.at("data"))) {
        message.data = std::get<property_map>(rootMap.at("data"));
    } else if (rootMap.contains("dataError")) {
        message.data = std::unexpected(gr::Error(std::get<std::string>(rootMap.at("dataError"))));
    }

    return message;
}
} // namespace opendigitizer::gnuradio

namespace opendigitizer::acq {

using opencmw::Annotated;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

/**
 * Generic time domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.1
 * Updated version at: https://gitlab.gsi.de/acc/specs/generic-daq#user-content-expected-data-acquisition-variables
 */
struct Acquisition {
    Annotated<std::string, opencmw::NoUnit, "name of timing event used to align the data, e.g. STREAMING or INJECTION1"> refTriggerName  = {"NO_REF_TRIGGER"}; // specified as ENUM
    Annotated<int64_t, si::time<nanosecond>, "UTC timestamp on which the timing event occurred">                         refTriggerStamp = 0;                  // specified as type WR timestamp
    Annotated<std::vector<float>, si::time<second>, "relative time between the reference trigger and each sample [s]">   channelTimeSinceRefTrigger;
    Annotated<float, si::time<second>, "user-defined delay">                                                             channelUserDelay   = 0.0f;
    Annotated<float, si::time<second>, "actual trigger delay">                                                           channelActualDelay = 0.0f;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "names of the channel/signal">                                  channelNames;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "values for each channel/signal">                          channelValues;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "r.m.s. errors for each channel/signal">                   channelErrors;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "S.I. units of post-processed signals">                         channelUnits;
    Annotated<std::vector<int64_t>, opencmw::NoUnit, "status bit-mask bits for this channel/signal">                     status;
    Annotated<std::vector<float>, opencmw::NoUnit, "minimum expected value for channel/signal">                          channelRangeMin;
    Annotated<std::vector<float>, opencmw::NoUnit, "maximum expected value for channel/signal">                          channelRangeMax;
    Annotated<std::vector<float>, opencmw::NoUnit, "temperature of the measurement device">                              temperature;

    // Additional fields not mandated by the specification but are present in FESA
    Annotated<std::vector<std::string>, opencmw::NoUnit, "event names">                     acquisitionContextCol_eventName;
    Annotated<std::vector<int>, opencmw::NoUnit, "process indices">                         acquisitionContextCol_processIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "sequence indices">                        acquisitionContextCol_sequenceIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "chain indices">                           acquisitionContextCol_chainIndex;
    Annotated<std::vector<int>, opencmw::NoUnit, "event numbers">                           acquisitionContextCol_eventNumber;
    Annotated<std::vector<int>, opencmw::NoUnit, "timing group ids">                        acquisitionContextCol_timingGroupID;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "event timestamps">          acquisitionContextCol_eventStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "process start timestamps">  acquisitionContextCol_processStartStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "sequence start timestamps"> acquisitionContextCol_sequenceStartStamp;
    Annotated<std::vector<std::int64_t>, si::time<nanosecond>, "chain start timestamps">    acquisitionContextCol_chainStartStamp;
    Annotated<std::vector<std::uint8_t>, opencmw::NoUnit, "event flags">                    acquisitionContextCol_eventFlags;
    Annotated<std::vector<std::int16_t>, opencmw::NoUnit, "reserved">                       acquisitionContextCol_reserved;
    Annotated<std::vector<std::int64_t>, opencmw::NoUnit, "event_id_raw">                   acquisitionContextCol_event_id_raw;
    Annotated<std::vector<std::int64_t>, opencmw::NoUnit, "param_raw">                      acquisitionContextCol_param_raw;
    Annotated<int, opencmw::NoUnit, "process index">                                        processIndex       = 0;
    Annotated<int, opencmw::NoUnit, "sequence index">                                       sequenceIndex      = 0;
    Annotated<int, opencmw::NoUnit, "chain index">                                          chainIndex         = 0;
    Annotated<int, opencmw::NoUnit, "event number">                                         eventNumber        = 0;
    Annotated<int, opencmw::NoUnit, "timing group id">                                      timingGroupID      = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "acquisition timestamp">                  acquisitionStamp   = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "event timestamp">                        eventStamp         = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "process start timestamp">                processStartStamp  = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "sequence start timestamp">               sequenceStartStamp = 0;
    Annotated<std::int64_t, si::time<nanosecond>, "chain start timestamp">                  chainStartStamp    = 0;

    // Optional fields not mandated by the specification but useful/necessary to propagate additional metainformation
    Annotated<std::vector<std::string>, opencmw::NoUnit, "S.I. quantities of post-processed signals"> channelQuantities;
    Annotated<int64_t, si::time<nanosecond>, "time-stamp w.r.t. beam-in trigger">                     acqLocalTimeStamp = 0;
    Annotated<std::vector<int64_t>, opencmw::NoUnit, "indices of trigger tags">                       triggerIndices;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "event names of trigger tags">               triggerEventNames;
    Annotated<std::vector<int64_t>, si::time<nanosecond>, "timestamps of trigger tags">               triggerTimestamps;
    Annotated<std::vector<float>, si::time<second>, "sample delay w.r.t. the trigger">                triggerOffsets;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "yaml of Tag's property_map">                triggerYamlPropertyMaps;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "list of error messages for this update">    acqErrors;
};

/**
 * Generic frequency domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.2
 */
struct AcquisitionSpectra {
    Annotated<std::string, opencmw::NoUnit, "property filter for sel. channel mode and name">    selectedFilter;                      // specified as Enum + String
    Annotated<std::string, opencmw::NoUnit, "trigger name, e.g. STREAMING or INJECTION1">        acqTriggerName      = {"STREAMING"}; // specified as ENUM
    Annotated<int64_t, si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> acqTriggerTimeStamp = 0;             // specified as type WR timestamp
    Annotated<int64_t, si::time<nanosecond>, "time-stamp w.r.t. beam-in trigger">                acqLocalTimeStamp   = 0;
    Annotated<std::string, opencmw::NoUnit, "name of the channel/signal">                        channelName;
    Annotated<std::vector<float>, opencmw::NoUnit, "magnitude spectra of signals">               channelMagnitude;
    Annotated<std::vector<::int32_t>, opencmw::NoUnit, "{N_meas, N_binning}">                    channelMagnitude_dimensions;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "{'time', 'frequency'}">                channelMagnitude_labels;
    Annotated<std::vector<long>, si::time<si::second>, "timestamps of samples">                  channelMagnitude_dim1_labels; // todo: either nanosecond or float
    Annotated<std::vector<float>, opencmw::NoUnit, "freqency scale">                             channelMagnitude_dim2_labels; // unit: Hz or f_rev
    Annotated<std::vector<float>, opencmw::NoUnit, "phase spectra of signals">                   channelPhase;
    Annotated<std::vector<std::string>, opencmw::NoUnit, "{'time', 'frequency'}">                channelPhase_labels;
    Annotated<std::vector<long>, si::time<si::second>, "timestamps of samples">                  channelPhase_dim1_labels; // todo: either nanosecond or float
    Annotated<std::vector<float>, opencmw::NoUnit, "freqency scale">                             channelPhase_dim2_labels; // unit: Hz or f_rev
};

struct TimeDomainContext {
    std::string channelNameFilter;
    std::string acquisitionModeFilter = "continuous"; // one of "continuous", "triggered", "multiplexed", "snapshot"
    std::string triggerNameFilter;
    int32_t     maxClientUpdateFrequencyFilter = 25;
    // TODO should we use sensible defaults for the following properties?
    // TODO make the following unsigned? (add unsigned support to query serialiser)
    int32_t                 preSamples        = 0;                     // Trigger mode
    int32_t                 postSamples       = 0;                     // Trigger mode
    int32_t                 maximumWindowSize = 65535;                 // Multiplexed mode
    int64_t                 snapshotDelay     = 0;                     // nanoseconds, Snapshot mode
    opencmw::MIME::MimeType contentType       = opencmw::MIME::BINARY; // YaS
};

struct FreqDomainContext {
    std::string             channelNameFilter;
    std::string             acquisitionModeFilter = "continuous"; // one of "continuous", "triggered", "multiplexed", "snapshot"
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::BINARY; // YaS
};

} // namespace opendigitizer::acq
ENABLE_REFLECTION_FOR(opendigitizer::acq::Acquisition, refTriggerName, refTriggerStamp, channelTimeSinceRefTrigger, channelUserDelay, channelActualDelay, channelNames, channelValues, channelErrors, channelQuantities, channelUnits, status, channelRangeMin, channelRangeMax, temperature, acqLocalTimeStamp, triggerIndices, triggerEventNames, triggerTimestamps, triggerOffsets, triggerYamlPropertyMaps, acqErrors)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionSpectra, selectedFilter, acqTriggerName, acqTriggerTimeStamp, acqLocalTimeStamp, channelName, channelMagnitude, channelMagnitude_dimensions, channelMagnitude_labels, channelMagnitude_dim1_labels, channelMagnitude_dim2_labels, channelPhase, channelPhase_labels, channelPhase_dim1_labels, channelPhase_dim2_labels)
ENABLE_REFLECTION_FOR(opendigitizer::acq::TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, preSamples, postSamples, maximumWindowSize, snapshotDelay, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::acq::FreqDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)

#endif
