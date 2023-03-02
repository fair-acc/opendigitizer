#ifndef DAQ_API_HPP
#define DAQ_API_HPP

#include <MultiArray.hpp> // todo: replace by mdspan
#include <opencmw.hpp>
#include <string>
#include <units/isq/si/frequency.h>
#include <units/isq/si/time.h>
#include <vector>
#include <mdspan.hpp>
#include <variant>
#include <IoSerialiserYaS.hpp>
#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>

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
template<std::floating_point T>
class AcquisitionExtended {
    template <typename ElementType>
    struct data_handle {
        std::shared_ptr<std::vector<ElementType>> ptr;
        std::size_t offset;
        ElementType& operator[](std::size_t i) {return (*ptr)[offset + i];}
        data_handle operator+(const std::size_t addend) {return data_handle{.ptr=ptr, .offset = offset + addend};}
        data_handle(std::vector<ElementType> data) : ptr(std::make_shared<std::vector<ElementType>>(std::move(data))), offset(0) {};
        data_handle() = default;
    };
    /**
     * An accessor which stores a std::vector inside of the mdspan instead of a pointer.
     * TODO: allow conversion to non-owning view
     */
    template <typename ElementType>
    struct owning_accessor {
        std::variant<std::vector<ElementType>, std::reference_wrapper<std::vector<ElementType>>> data;

        using element_type = ElementType;
        using reference = ElementType&;
        using offset_policy = owning_accessor<ElementType>;
        using data_handle_type = data_handle<ElementType>;
        using pointer = data_handle_type;

        constexpr explicit owning_accessor() noexcept {};

        [[nodiscard]] constexpr reference access(pointer p, size_t i) const noexcept {
            return p[i];
        }

        [[nodiscard]] constexpr data_handle_type offset(pointer p, size_t i) const noexcept {
            return p + i;
        }
    };
    static_assert(std::copyable<owning_accessor<float>>);
    static_assert(std::is_nothrow_move_constructible_v<owning_accessor<float>>);
    static_assert(std::is_nothrow_move_assignable_v<owning_accessor<float>>);
    static_assert(std::is_nothrow_swappable_v<owning_accessor<float>>);
public :
    static constexpr std::size_t MAX_RANK = 8;
    using mdtype = stdx::mdspan<T, stdx::dextents<int, MAX_RANK>, stdx::layout_right, owning_accessor<T>>;
    Annotated<std::string,                 opencmw::NoUnit,      "copy of AcquisitionFilter.acqMode">                acqMode;
    Annotated<int64_t,                     si::time<nanosecond>, "UTC timestamp on which the timing event occurred"> timestamp = 0;
    Annotated<double,                      si::time<second>,     "relative time w.r.t. last beam-in trigger">        beamInTimeStamp = 0;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "base si-unit for axis">                            axisUnits;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "e.g. time, frequency, voltage, current">           axisNames;
    //Annotated<std::vector<std::vector<T>>, opencmw::NoUnit,      "explicit axis values, not necessarily equidistant">axisValues; // and min/max acq. range (ADC clamping, THD, ...) // no support for nested serialiser
    Annotated<std::vector<T>,              opencmw::NoUnit,      "explicit axis values, not necessarily equidistant">axisValues; // and min/max acq. range (ADC clamping, THD, ...) // flattened because of serialiser limitations
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "name of the signal">                               signalNames;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "base si-units">                                    signalUnits;
    //Annotated<std::vector<std::array<T,2>>,opencmw::NoUnit,      "min/max value of signal">                          signalRanges; // no support for nested lists in serialiser
    Annotated<std::vector<T>,              opencmw::NoUnit,      "min/max value of signal">                          signalRanges; // flattened because of serialiser limitations
    Annotated<mdtype,                      opencmw::NoUnit,      "values">                                           signalValues;
    Annotated<mdtype,                      opencmw::NoUnit,      "rms errors">                                       signalErrors;
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "status messages for signal">                       signalStatus; // todo type of map to be discussed, japc compatibility, opencmwlight serialiser does not support vec<map<>>
    Annotated<std::vector<std::string>,    opencmw::NoUnit,      "raw timing events occurred in the acq window">     timingEvents; // now only maps timestamp to event id, because more complex containers trip up the serialisers
};
using AcquisitionExtended_double = AcquisitionExtended<double>;
using AcquisitionExtended_float = AcquisitionExtended<float>;
}

ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionFilter, signalNames, acqMode, contextSelector, contentType)
ENABLE_REFLECTION_FOR(opendigitizer::acq::Acquisition, acqMode, timestamp, beamInTimeStamp, signalTimeBase, signalNames, signalUnits, signalRangeMin, signalRangeMax, signalValues, signalErrors, signalStatus, timingEvents)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionExtended_double, acqMode, timestamp, beamInTimeStamp, signalNames, axisUnits, axisNames, axisValues, signalValues, signalErrors, signalStatus, timingEvents)
ENABLE_REFLECTION_FOR(opendigitizer::acq::AcquisitionExtended_float, acqMode, timestamp, beamInTimeStamp, signalNames, axisUnits, axisNames, axisValues, signalValues, signalErrors, signalStatus, timingEvents)
// clang-format: ON

// implement serialisers for mdspan
template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::YaS, stdx::mdspan<T,extents,layout,accessor>> {
    using mdspan_t = stdx::mdspan<T,extents,layout,accessor>;
    forceinline static constexpr uint8_t getDataTypeId() {
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<T>();
    }
    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const mdspan_t &value) noexcept {
        std::array<int32_t, extents::rank()> dims;
        std::size_t n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        buffer.put(dims);
        std::span<T> data(&value[std::array<int, extents::rank()>{}], n);
        buffer.put(std::vector<T>{data.begin(), data.end()}); // todo: account for strides and offsets (possibly use iterators?)
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};

template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::CmwLight, stdx::mdspan<T,extents,layout,accessor>> {
    using mdspan_t = stdx::mdspan<T,extents,layout,accessor>;
    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
        if      constexpr (extents::rank() == 1) { return cmwlight::getTypeIdVector<T>(); }
        else if constexpr (std::is_same_v<bool   , T> && extents::rank() == 2) { return 17; }
        else if constexpr (std::is_same_v<int8_t , T> && extents::rank() == 2) { return 18; }
        else if constexpr (std::is_same_v<int16_t, T> && extents::rank() == 2) { return 19; }
        else if constexpr (std::is_same_v<int32_t, T> && extents::rank() == 2) { return 20; }
        else if constexpr (std::is_same_v<int64_t, T> && extents::rank() == 2) { return 21; }
        else if constexpr (std::is_same_v<float  , T> && extents::rank() == 2) { return 22; }
        else if constexpr (std::is_same_v<double , T> && extents::rank() == 2) { return 23; }
        else if constexpr (std::is_same_v<char   , T> && extents::rank() == 2) { return 203; }
        else if constexpr (opencmw::is_stringlike<T>  && extents::rank() == 2) { return 24; }
        else if constexpr (std::is_same_v<bool   , T>) { return 25; }
        else if constexpr (std::is_same_v<int8_t , T>) { return 26; }
        else if constexpr (std::is_same_v<int16_t, T>) { return 27; }
        else if constexpr (std::is_same_v<int32_t, T>) { return 28; }
        else if constexpr (std::is_same_v<int64_t, T>) { return 29; }
        else if constexpr (std::is_same_v<float  , T>) { return 30; }
        else if constexpr (std::is_same_v<double , T>) { return 31; }
        else if constexpr (std::is_same_v<char   , T>) { return 204; }
        else if constexpr (opencmw::is_stringlike<T> ) { return 32; }
        else { static_assert(opencmw::always_false<T>); }
        // clang-format on
    }
    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const mdspan_t &value) noexcept {
        std::array<int32_t, extents::rank()> dims;
        std::size_t n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        buffer.put(dims);
        std::span<T> data(&value[std::array<int, extents::rank()>{}], n);
        buffer.put(std::vector<T>{data.begin(), data.end()}); // todo: account for strides and offsets (possibly use iterators?)
    }
    constexpr static void deserialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, T &value) noexcept {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};

template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::Json, stdx::mdspan<T,extents,layout,accessor>> {
    using mdspan_t = stdx::mdspan<T,extents,layout,accessor>;
    static std::vector<T> to_vec(mdspan_t data, std::size_t n) {
        std::span<T> spandata(&data[std::array<int, extents::rank()>{0}], n);
        return std::vector<T>{spandata.begin(), spandata.end()};
    }
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, START_MARKER>::getDataTypeId(); } // because the object is serialised as a subobject, we have to emmit START_MARKER
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const &field, const mdspan_t &value) noexcept {
        using namespace std::string_view_literals;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("{\n"sv);
        std::array<int32_t, mdspan_t::rank()> dims;
        std::size_t n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        FieldDescriptionShort memberField{ .headerStart = 0, .dataStartPosition = 0, .dataEndPosition = 0, .subfields = 0, .fieldName = ""sv, .intDataType = IoSerialiser<Json, T>::getDataTypeId(), .hierarchyDepth = static_cast<uint8_t>(field.hierarchyDepth + 1U) };
        memberField.fieldName = "dims"sv;
        FieldHeaderWriter<Json>::template put<IoBuffer::WITHOUT>(buffer, memberField, dims);
        memberField.fieldName = "values"sv;
        FieldHeaderWriter<Json>::template put<IoBuffer::WITHOUT>(buffer, memberField, to_vec(value, n));
        buffer.resize(buffer.size() - 2); // remove trailing comma
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("\n}\n"sv);
    }
    static void deserialise(IoBuffer &buffer, FieldDescription auto const &field, T &value) {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};


template<typename T, typename extents, typename layout, typename accessor>
class opencmw::mustache::mustache_data<stdx::mdspan<T,extents,layout,accessor>> : public mustache_data<std::vector<T>> {
    using mdspan_t = stdx::mdspan<T,extents,layout,accessor>;
    static std::vector<T> to_vec(mdspan_t data) {
        std::size_t n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            n *= static_cast<int32_t>(data.extent(i));
        }
        std::span<T> spandata(&data[std::array<int, extents::rank()>{0}], n);
        return std::vector<T>{spandata.begin(), spandata.end()};
    }
public:
    explicit mustache_data(mdspan_t val) : mustache_data<std::vector<T>>(to_vec(val)) {}
};



#endif //DAQ_API_HPP
