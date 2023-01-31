#pragma once

#include <units/isq/si/time.h>
#include <units/isq/si/frequency.h>
#include <MultiArray.hpp>
#include <opencmw.hpp>
#include <string>
#include <vector>

using opencmw::Annotated;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

/**
 * Generic time domain data object.
 * Specified in: https://git.gsi.de/acc/specs/generic-daq#time-domain-fec-acquisition-properties-user-interface
 */
// clang-format: OFF
struct Acquisition {
    Annotated<std::string,                   opencmw::NoUnit,      "name of timing event used to align the data (default: 'NO_REF_TRIGGER')"> refTriggerName     = { "NO_REF_TRIGGER" };
    Annotated<int64_t,                       si::time<nanosecond>, "UTC timestamp on which the timing event occurred"                       > refTriggerStamp    = 0;
    Annotated<std::vector<float>,            si::time<second>,     "relative time between the reference trigger and each sample"            > channelTimeSinceRefTrigger;
    Annotated<float,                         si::time<second>,     "user-defined delay"                                                     > channelUserDelay   = 0.0f;
    Annotated<float,                         si::time<second>,     "actual trigger delay"                                                   > channelActualDelay = 0.0f;
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "name of the channel/signal"                                             > channelNames;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit,      "value of the channel/signal"                                            > channelValues;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit,      "r.m.s. error of of the channel/signal"                                  > channelErrors;
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "S.I. unit of post-processed signal"                                     > channelUnits;
    Annotated<std::vector<int64_t>,          opencmw::NoUnit,      "status bit-mask bits for this channel/signal"                           > status;
    Annotated<std::vector<float>,            opencmw::NoUnit,      "minimum expected value for channel/signal"                              > channelRangeMin;
    Annotated<std::vector<float>,            opencmw::NoUnit,      "minimum expected value for channel/signal"                              > channelRangeMax;
    Annotated<std::vector<float>,            opencmw::NoUnit,      "temperature of the measurement device"                                  > temperature;
};
ENABLE_REFLECTION_FOR(Acquisition, refTriggerName, refTriggerStamp, channelTimeSinceRefTrigger, channelUserDelay, channelActualDelay, channelNames, channelValues, channelErrors, channelUnits, status, channelRangeMin, channelRangeMax, temperature)

/**
 * Generic frequency domain data object.
 * Specified in: https://git.gsi.de/acc/specs/generic-daq#frequency-domain-fec-spectrum-acquisition-property-user-interface
 * We do not follow the specification here, because the last changes to the spec where only performed for the time-domain
 * and the frequency domain is expected to follow at some time.
 */
// clang-format: OFF
struct AcquisitionSpectra {
    Annotated<std::string,        opencmw::NoUnit,      "trigger name"                                                 > refTriggerName  = { "NO_REF_TRIGGER" };
    Annotated<int64_t,            si::time<nanosecond>, "UTC trigger time-stamp"                                       > refTriggerStamp = 0;
    Annotated<std::string,        opencmw::NoUnit,      "name of digitizer input or post-processed signals"            > channelName;
    Annotated<std::vector<float>, opencmw::NoUnit,      "magnitude specta of digitizer input or post-processed signals"> channelMagnitudeValues;
    Annotated<std::vector<float>, si::frequency<hertz>, "frequency scale"                                              > channelFrequencyValues;
    Annotated<std::string,        opencmw::NoUnit,      "S.I. unit of post-processed signal"                           > channelUnit;
};
ENABLE_REFLECTION_FOR(AcquisitionSpectra, refTriggerName, refTriggerStamp, channelName, channelMagnitudeValues, channelFrequencyValues, channelUnit)
// clang-format: ON

struct TimeDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(TimeDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)

struct FreqDomainContext {
    std::string             channelNameFilter;
    int32_t                 acquisitionModeFilter = 0; // STREAMING
    std::string             triggerNameFilter;
    int32_t                 maxClientUpdateFrequencyFilter = 25;
    opencmw::MIME::MimeType contentType                    = opencmw::MIME::JSON;
};

ENABLE_REFLECTION_FOR(FreqDomainContext, channelNameFilter, acquisitionModeFilter, triggerNameFilter, maxClientUpdateFrequencyFilter, contentType)

