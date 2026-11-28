#include "App.hpp"

#include <complex>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/basic/ConverterBlocks.hpp>
#include <gnuradio-4.0/basic/SignalGenerator.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>

#ifndef __EMSCRIPTEN__
// gr-digitizers is a native-only run-time dependency (PicoScope SDK, USB); unavailable under emscripten.
#include <fair/picoscope/Picoscope.hpp>
#include <fair/picoscope/Picoscope4000a.hpp>
#endif

#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
#include <gnuradio-4.0/GrAnalogBlocks.hpp>
#include <gnuradio-4.0/GrAudioBlocks.hpp>
#if defined(OD_ACOUSTIC_CAMERA)
#include <gnuradio-4.0/GrLocalisationBlocks.hpp>
#endif
#include <gnuradio-4.0/GrBasicBlocks.hpp>
#include <gnuradio-4.0/GrDigitalBlocks.hpp>
#include <gnuradio-4.0/GrElectricalBlocks.hpp>
#include <gnuradio-4.0/GrFileIoBlocks.hpp>
#include <gnuradio-4.0/GrFilterBlocks.hpp>
#include <gnuradio-4.0/GrFourierBlocks.hpp>
#include <gnuradio-4.0/GrHttpBlocks.hpp>
#include <gnuradio-4.0/GrMathBlocks.hpp>
#include <gnuradio-4.0/GrTestingBlocks.hpp>
#include <gnuradio-4.0/sdr/RTL2832Source.hpp>

#include "blocks/ImControlRTL2832Connect.hpp"
#include "charts/RDSView.hpp"
#endif

namespace gr::od {

// on WASM dynload the Gr* blocklibs arrive as SIDE_MODULE plugins; natively and WASM-static they are linked in and registered here
void registerAppBlocks(gr::BlockRegistry& blockRegistry) {
#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
    gr::blocklib::initGrBasicBlocks(blockRegistry);
    gr::blocklib::initGrElectricalBlocks(blockRegistry);
    gr::blocklib::initGrFileIoBlocks(blockRegistry);
    gr::blocklib::initGrFilterBlocks(blockRegistry);
    gr::blocklib::initGrFourierBlocks(blockRegistry);
    gr::blocklib::initGrHttpBlocks(blockRegistry);
    gr::blocklib::initGrMathBlocks(blockRegistry);
    gr::blocklib::initGrTestingBlocks(blockRegistry);
    gr::blocklib::initGrAnalogBlocks(blockRegistry);
    gr::blocklib::initGrAudioBlocks(blockRegistry);
    gr::blocklib::initGrDigitalBlocks(blockRegistry);
    charts::registerChart<charts::RDSView>(blockRegistry);
    registerWithIdPrefix<ImControlRTL2832Connect>(blockRegistry, kDigitizerUiIdPrefix);
    gr::registerBlock<gr::blocks::sdr::RTL2832Source<std::complex<float>>, "">(blockRegistry);
#endif
#if defined(OD_ACOUSTIC_CAMERA)
    gr::blocklib::initGrLocalisationBlocks(blockRegistry);
#endif

    // decimating BasicFilterProto variants: the ratio is the Resampling<in,out> template integers, and the gr::filter blocklib registers only the <1,1> identity (used by PulsedPowerDemo.grc)
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<100UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<float32, gr::Resampling<100, 1, false>>">(blockRegistry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<40UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<float32, gr::Resampling<40, 1, false>>">(blockRegistry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<1UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<float32, gr::Resampling<1, 1, false>>">(blockRegistry);
    gr::registerBlock<gr::filter::BasicFilterProto<gr::UncertainValue<float>, gr::Resampling<1UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<gr::UncertainValue<float32>, gr::Resampling<1, 1, false>>">(blockRegistry);

#ifndef __EMSCRIPTEN__
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope4000a>, "">(blockRegistry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope4000a>, "">(blockRegistry);
#endif
}

} // namespace gr::od
