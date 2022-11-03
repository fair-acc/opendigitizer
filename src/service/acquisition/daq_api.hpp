#pragma once

#include <iostream>
#include <opencmw.hpp>
#include <units/isq/si/time.h>
#include <vector>

/**
 * @brief Time-domain FEC acquisition user-interface
 * specified here: https://git.gsi.de/acc/specs/generic-daq#time-domain-fec-acquisition-properties-user-interface.
 */
struct Acquisition {
    opencmw::Annotated<std::string, opencmw::NoUnit, "Name of timing event used to align the data">                                                     refTriggerName = "NO_REF_TRIGGER";
    opencmw::Annotated<int64_t, units::isq::si::time<units::isq::si::nanosecond>, "UTC timestamp on which the timing event occured">                    refTriggerStamp;
    opencmw::Annotated<std::vector<float>, units::isq::si::time<units::isq::si::second>, "Relative time between the reference trigger and each sample"> channelTimeSinceRefTrigger;
    opencmw::Annotated<float, units::isq::si::time<units::isq::si::second>, "User-defined delay">                                                       channelUserDelay;
    opencmw::Annotated<float, units::isq::si::time<units::isq::si::second>, "Actual trigger delay">                                                     channelActualDelay;
    opencmw::Annotated<std::vector<std::string>, opencmw::NoUnit, "Name(s) of the channel/signal">                                                      channelNames{};
    opencmw::Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "Values of the channel/signal">                                                  channelValues{};
    opencmw::Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit, "R.m.s. error of of the channel/signal">                                         channelErrors{};
    opencmw::Annotated<std::vector<std::string>, opencmw::NoUnit, "S.I. unit of post-processed signal">                                                 channelUnits{};
    opencmw::Annotated<std::vector<int64_t>, opencmw::NoUnit, "Status bit-mask bits for this channel/signal">                                           status{};
    opencmw::Annotated<std::vector<float>, opencmw::NoUnit, "Minimum expected value for channel/signal">                                                channelRangeMin{};
    opencmw::Annotated<std::vector<float>, opencmw::NoUnit, "Maximum expected value for channel/signal">                                                channelRangeMax{};
    // Celsius is currently not supported: https://github.com/mpusz/units/pull/232
    opencmw::Annotated<std::vector<float>, opencmw::NoUnit, "Temperature of the measurement device"> temperature{}; // [°C]
};
ENABLE_REFLECTION_FOR(Acquisition, refTriggerName, refTriggerStamp, channelTimeSinceRefTrigger, channelUserDelay, channelActualDelay, channelNames, channelValues, channelErrors, channelUnits, status, channelRangeMin, channelRangeMax)
