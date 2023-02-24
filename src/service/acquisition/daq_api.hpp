#ifndef DAQ_API_HPP
#define DAQ_API_HPP

#include <MultiArray.hpp> // todo: replace by mdspan
#include <opencmw.hpp>
#include <string>
#include <units/isq/si/frequency.h>
#include <units/isq/si/time.h>
#include <vector>
#include <mdspan.hpp>

using opencmw::Annotated;
using namespace units::isq;
using namespace units::isq::si;
using namespace std::literals;

namespace stdx = std::experimental;

namespace opendigitizer::acq {

/**
 * Top level Acquisition modes
 */
class AcquisitionMode {
public:
    enum Mode : uint32_t { STREAMING = 0, FULL_CYCLE = 1, SNAPSHOT = 2, POST_MORTEM = 3, TRIGGERED = 4 };

    explicit(false) AcquisitionMode(Mode mode_) : mode(mode_) {}

    explicit AcquisitionMode(const std::string& string) {
        if (string == "STREAMING" || string.ends_with("Hz")) { // "1Hz", "1 Hz", "25Hz", "25 Hz"
            mode = STREAMING;
        } else if (string == "FULL_CYCLE") {
            mode = FULL_CYCLE;
        } else if (string == "SNAPSHOT" || string.ends_with("s")) { // "20ms", "5 s", ...
            mode = SNAPSHOT;
        } else if (string == "POST_MORTEM" || string.starts_with("CMD_POST_MORTEM_")) { // "CMD_POST_MORTEM_1"
            mode = POST_MORTEM;
        } else { // Interpret string as event name or EVT#<Nr>
            mode = TRIGGERED;
        }
    }

    explicit(false) operator Mode() { return mode; }

    std::string toString() {
        switch(mode) {
            case STREAMING:
                return "STREAMING";
            case FULL_CYCLE:
                return "FULL_CYCLE";
            case SNAPSHOT:
                return "SNAPSHOT";
            case POST_MORTEM:
                return "POST_MORTEM";
            case TRIGGERED:
            default:
                return "TRIGGERED";
        }
    }

private:
    Mode mode;
};

/**
 * possible query parameters for requests to the (time-domain) acquisition property
 */
// clang-format: OFF
struct AcquisitionFilter {
    Annotated<std::string,        opencmw::NoUnit, "comma separated list of signals">                          signalNames; // exception if list contains incompatible signals contains sample-rate
    Annotated<opencmw::TimingCtx, opencmw::NoUnit, "e.g. FAIR_SELECTOR:C=-1:S=3,7:P=1-5">                      contextSelector = opencmw::TimingCtx(); // todo: OR tbd list of LSA Context names ~C=1.INJECTION~ ?
    Annotated<std::string,        opencmw::NoUnit, "'STREAMING', '20ms'->snapshot, 'CYCLE', '1Hz'->streaming"> acqMode= AcquisitionMode(AcquisitionMode::STREAMING).toString();
    // todo: global list of timing event names and IDs -> property which could be global setting with worker (-> later supplied via lsa)
    opencmw::MIME::MimeType contentType             = opencmw::MIME::JSON;
};

/**
 * Timing Event Payload object with field accessors
 * Format description: https://www-acc.gsi.de/wiki/Timing/TimingSystemEvent
 */
class TimingEvt {
    std::array<std::uint8_t, 32> data;

    std::span<std::uint8_t, 8> eventId()   {return std::span<std::uint8_t , 8>{data.begin(),    8};} // used for indexing and control values
    std::span<std::uint8_t, 8> param()     {return std::span<std::uint8_t , 8>{data.begin()+ 8, 8};} // additional parameter with event specific meaning
    std::span<std::uint8_t, 4> reserved()  {return std::span<std::uint8_t , 4>{data.begin()+16, 4};} // Reserved
    std::span<std::uint8_t, 4> tef()       {return std::span<std::uint8_t , 4>{data.begin()+20, 4};} // Timing Extension Field containing data relevant for internal processing
    std::span<std::uint8_t, 8> timestamp() {return std::span<std::uint8_t , 8>{data.begin()+24, 8};} // Timestamp: nanoseconds since 1 January 1970
    // eventId subfields
    std::uint8_t fid()       {return eventId()[0] & 0b00001111;} // 4bit format id: 0: pre 09/2017, 1: post 10/2017
    int          gid()       {return (int(eventId()[0] & 0b11110000) << 4) + eventId()[1];} // 12bit group id
    int          evtno()     {return (int(eventId()[3] & 0b00001111) << 4) + eventId()[2];} // 12bit event number
    bool         beamin()    {return eventId()[3] & 0b00010000;} // true: beam-in, false: beam-out
    bool         bpc_start() {return eventId()[3] & 0b00100000;} // true: start of new bpc (since 2020)
    bool         reserved1() {return eventId()[3] & 0b01000000;}
    bool         reserved2() {return eventId()[3] & 0b10000000;}
    int          sid()       {return (int(eventId()[5] & 0b00001111) < 4) + (int(eventId()[4]) << 4);} // 12bit sequence id
    int          bpid ()     {return (int(eventId()[5] & 0b11110000) < 6) + (int((eventId()[6]) << 2)) + int(eventId()[7] & 0b00000011);} // 14bit BeamProcess ID
    bool         reserved3() {return eventId()[7] & 0b00000100;}
    bool         reqNoBeam() {return eventId()[7] & 0b00001000;} // request virtual accelerator but without beam from unilac
    std::uint8_t virtAcc()   {return eventId()[0] & 0b11110000;} // number of virtual accelerator requested from unilac
    // param subfields for standard event numbers
    int          bpcid()     {return (int(param()[0]) << 14) + (int(param()[1]) << 6) + (int(param()[2] & 0b00000011));}; // 22bit
    long         bpcts()     {return (long(param()[2] & 0b11000000) << 40) + (long(param()[3]) << 32) + (long(param()[4]) << 24) + (long(param()[4]) << 16) + (long(param()[4]) << 8) + (long(param()[4]));} // 42bit
};

/**
 * Generic time domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.1
 * modified channel -> signal, rationalised timestamps and acqMode, added status map
 */
struct Acquisition {
    Annotated<std::string,                   opencmw::NoUnit,      "copy of AcquisitionFilter.acqMode">                acqMode;
    Annotated<int64_t,                       si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> timestamp = 0;
    Annotated<int64_t,                       si::time<nanosecond>, "relative time w.r.t. last beam-in trigger">        beamInTimeStamp = 0;
    Annotated<std::vector<float>,            si::time<second>,     "time scale relative to timestamp">                 signalTimeBase; // full cycle -> beam injection
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "name of the signal">                               signalNames;
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "S.I. unit of post-processed signal">               signalUnits;
    Annotated<std::vector<float>,            opencmw::NoUnit,      "minimum expected value for signal/signal">         signalRangeMin;
    Annotated<std::vector<float>,            opencmw::NoUnit,      "minimum expected value for signal/signal">         signalRangeMax;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit,      "value of the signal">                              signalValues;
    Annotated<opencmw::MultiArray<float, 2>, opencmw::NoUnit,      "r.m.s. signal error">                              signalErrors;
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "status messages for signal">                       signalStatus; // todo type of map to be discussed, japc compatibility, opencmwlight serialiser does not support vec<map<>>
    Annotated<std::vector<std::string>,      opencmw::NoUnit,      "raw timing events occurred in the acq window">     timingEvents; // now only maps timestamp to event id, because more complex containers trip up the serialisers
};

/**
 * Generic frequency domain data object.
 * Specified in: https://edms.cern.ch/document/1823376/1 EDMS: 1823376 v.1 Section 4.3.2
 * modified channel -> signal, rationalised timestamps and acqMode, added status map
 */
// clang-format: OFF
struct AcquisitionSpectra {
    Annotated<std::string,              opencmw::NoUnit,      "copy of AcquisitionFilter.acqMode">                acqMode;
    Annotated<int64_t,                  si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> timestamp = 0;
    Annotated<int64_t,                  si::time<nanosecond>, "relative time w.r.t. last beam-in trigger">        beamInTimeStamp = 0;
    Annotated<std::string,              opencmw::NoUnit,      "name of the signal/signal">                        signalName;
    Annotated<std::vector<float>,       opencmw::NoUnit,      "magnitude spectra of signals">                     signalMagnitude;
    Annotated<std::vector<::int32_t>,   opencmw::NoUnit,      "{N_meas, N_binning}">                              signalMagnitude_dimensions;
    Annotated<std::vector<std::string>, opencmw::NoUnit,      "{'time', 'frequency'}">                            signalMagnitude_labels;
    Annotated<std::vector<long>,        si::time<si::second>, "timestamps of samples">                            signalMagnitude_dim1_labels;
    Annotated<std::vector<float>,       opencmw::NoUnit,      "frequency scale">                                  signalMagnitude_dim2_labels; // unit: Hz or f_rev
    Annotated<std::vector<float>,       opencmw::NoUnit,      "phase spectra of signals">                         signalPhase;
    Annotated<std::vector<std::string>, opencmw::NoUnit,      "{'time', 'frequency'}">                            signalPhase_labels;
    Annotated<std::vector<long>,        si::time<si::second>, "timestamps of samples">                            signalPhase_dim1_labels;
    Annotated<std::vector<float>,       opencmw::NoUnit,      "frequency scale">                                  signalPhase_dim2_labels; // unit: Hz or f_rev
    Annotated<std::vector<std::string>, opencmw::NoUnit,      "status messages for signal">                       signalStatus; // todo type of map to be discussed, japc compatibility, opencmwlight serialiser does not support vec<map<>>
    Annotated<std::vector<std::string>, opencmw::NoUnit,      "raw timing events occurred in the acq window">     timingEvents; // now only maps timestamp to event id, because more complex containers trip up the serialisers
};

/**
 * n-dimensional tensor
 * corresponds to DataSet layout
 * dense data on mesh
 * todo: implement serialisers in opencmw
 */
//template<std::floating_point T> // todo: enable reflection on templated types
class AcquisitionExtended {
    using T = float;
    // private mdspan backing storage
    std::vector<T> signalValuesData;
    std::vector<T> signalErrorsData;
public :
    static constexpr std::size_t MAX_RANK = 8;
    using mdtype = stdx::mdspan<T, stdx::dextents<int, MAX_RANK>>;
    Annotated<std::string,                 opencmw::NoUnit,      "copy of AcquisitionFilter.acqMode">                acqMode;
    Annotated<int64_t,                     si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> timestamp = 0;
    Annotated<double,                      si::time<second>,     "relative time w.r.t. last beam-in trigger">        beamInTimeStamp = 0;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "base si-unit for axis">                            axisUnits;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "e.g. time, frequency, voltage, current">           axisNames;
    Annotated<std::vector<std::vector<T>>, opencmw::NoUnit,      "explicit axis values, not necessarily equidistant">axisValues; // and min/max acq. range (ADC clamping, THD, ...)
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "name of the signal">                               signalNames;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "base si-units">                                    signalUnits;
    Annotated<std::vector<std::array<T,2>>,opencmw::NoUnit,      "min/max value of signal">                          signalRanges;
    Annotated<mdtype,                      opencmw::NoUnit,      "values">                                           signalValues;
    Annotated<mdtype,                      opencmw::NoUnit,      "rms errors">                                       signalErrors;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "status messages for signal">                       signalStatus; // todo type of map to be discussed, japc compatibility, opencmwlight serialiser does not support vec<map<>>
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "raw timing events occurred in the acq window">     timingEvents; // now only maps timestamp to event id, because more complex containers trip up the serialisers
};
}

ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionFilter, signalNames, acqMode, contextSelector, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::acq::Acquisition, acqMode, timestamp, beamInTimeStamp, signalTimeBase, signalNames, signalUnits, signalRangeMin, signalRangeMax, signalValues, signalErrors, signalStatus, timingEvents)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionSpectra, acqMode, timestamp, beamInTimeStamp, signalName, signalMagnitude, signalMagnitude_dimensions, signalMagnitude_labels, signalMagnitude_dim1_labels, signalMagnitude_dim2_labels, signalPhase, signalPhase_labels, signalPhase_dim1_labels, signalPhase_dim2_labels, signalStatus, timingEvents)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionExtended, acqMode, timestamp, beamInTimeStamp, signalNames, axisUnits, axisNames, axisValues, signalValues, signalErrors, signalStatus, timingEvents)
// ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionExtended<double>, acqMode, timestamp, beamInTimeStamp, signalNames, axisUnits, axisNames, axisValues, signalValues, signalErrors, signalStatus, timingEvents)
// ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionExtended<float>, acqMode, timestamp, beamInTimeStamp, signalNames, axisUnits, axisNames, axisValues, signalValues, signalErrors, signalStatus, timingEvents)
// clang-format: ON

#endif //DAQ_API_HPP
