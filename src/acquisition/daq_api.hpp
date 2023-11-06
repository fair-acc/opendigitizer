#ifndef OPENDIGITIZER_ACQUISITION_DAQ_API_H
#define OPENDIGITIZER_ACQUISITION_DAQ_API_H

#include <MultiArray.hpp>
#include <opencmw.hpp>
#include <string>
#include <units/isq/si/frequency.h>
#include <units/isq/si/time.h>
#include <vector>

namespace opendigitizer::flowgraph {

struct FilterContext {
    opencmw::MIME::MimeType contentType = opencmw::MIME::JSON;
};

struct Flowgraph {
    std::string flowgraph;
    std::string layout;
};

} // namespace opendigitizer::flowgraph

ENABLE_REFLECTION_FOR(opendigitizer::flowgraph::FilterContext, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::flowgraph::Flowgraph, flowgraph, layout)

namespace opendigitizer::acq {

using opencmw::Annotated;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

/**
 * Generic time domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.1
 */
// clang-format: OFF
struct Acquisition {
    Annotated<std::string, opencmw::NoUnit, "property filter for sel. channel mode and name">    selectedFilter;                        // specified as Enum + String
    Annotated<std::string, opencmw::NoUnit, "trigger name, e.g. STREAMING or INJECTION1">        acqTriggerName      = { "STREAMING" }; // specified as ENUM
    Annotated<int64_t, si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> acqTriggerTimeStamp = 0;               // specified as type WR timestamp
    Annotated<int64_t, si::time<nanosecond>, "time-stamp w.r.t. beam-in trigger">                acqLocalTimeStamp   = 0;
    Annotated<std::vector<::int32_t>, si::time<second>, "time scale">                            channelTimeBase; // todo either nanosecond or float
    Annotated<float, si::time<second>, "user-defined delay">                                     channelUserDelay   = 0.0f;
    Annotated<float, si::time<second>, "actual trigger delay">                                   channelActualDelay = 0.0f;
    Annotated<std::string, opencmw::NoUnit, "name of the channel/signal">                        channelName;
    Annotated<std::vector<float>, opencmw::NoUnit, "value of the channel/signal">                channelValue;
    Annotated<std::vector<float>, opencmw::NoUnit, "r.m.s. error of of the channel/signal">      channelError;
    Annotated<std::string, opencmw::NoUnit, "S.I. unit of post-processed signal">                channelUnit;
    Annotated<int64_t, opencmw::NoUnit, "status bit-mask bits for this channel/signal">          status;
    Annotated<float, opencmw::NoUnit, "minimum expected value for channel/signal">               channelRangeMin;
    Annotated<float, opencmw::NoUnit, "minimum expected value for channel/signal">               channelRangeMax;
    Annotated<float, opencmw::NoUnit, "temperature of the measurement device">                   temperature;
};

/**
 * Generic frequency domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.2
 */
// clang-format: OFF
struct AcquisitionSpectra {
    Annotated<std::string, opencmw::NoUnit, "property filter for sel. channel mode and name">    selectedFilter;                        // specified as Enum + String
    Annotated<std::string, opencmw::NoUnit, "trigger name, e.g. STREAMING or INJECTION1">        acqTriggerName      = { "STREAMING" }; // specified as ENUM
    Annotated<int64_t, si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> acqTriggerTimeStamp = 0;               // specified as type WR timestamp
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
// clang-format: ON

struct TimeDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

struct FreqDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

} // namespace opendigitizer::acq

ENABLE_REFLECTION_FOR(opendigitizer::acq::Acquisition, selectedFilter, acqTriggerName, acqTriggerTimeStamp, acqLocalTimeStamp, channelTimeBase, channelUserDelay, channelActualDelay, channelName, channelValue, channelError, channelUnit, status, channelRangeMin, channelRangeMax, temperature)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionSpectra, selectedFilter, acqTriggerName, acqTriggerTimeStamp, acqLocalTimeStamp, channelName, channelMagnitude, channelMagnitude_dimensions, channelMagnitude_labels, channelMagnitude_dim1_labels, channelMagnitude_dim2_labels, channelPhase, channelPhase_labels, channelPhase_dim1_labels, channelPhase_dim2_labels)
ENABLE_REFLECTION_FOR(opendigitizer::acq::TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::acq::FreqDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)

#endif
