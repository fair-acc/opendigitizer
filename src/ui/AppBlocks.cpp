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
void registerAppBlocks(gr::BlockRegistry& registry) {
#if !defined(__EMSCRIPTEN__) || defined(OD_WASM_STATIC_BLOCKLIBS)
    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrElectricalBlocks(registry);
    gr::blocklib::initGrFileIoBlocks(registry);
    gr::blocklib::initGrFilterBlocks(registry);
    gr::blocklib::initGrFourierBlocks(registry);
    gr::blocklib::initGrHttpBlocks(registry);
    gr::blocklib::initGrMathBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    gr::blocklib::initGrAnalogBlocks(registry);
    gr::blocklib::initGrAudioBlocks(registry);
    gr::blocklib::initGrDigitalBlocks(registry);
    charts::registerChart<charts::RDSView>(registry);
    registerWithIdPrefix<ImControlRTL2832Connect>(registry, kDigitizerUiIdPrefix);
    gr::registerBlock<gr::blocks::sdr::RTL2832Source<std::complex<float>>, "">(registry);
#endif
#if defined(OD_ACOUSTIC_CAMERA)
    gr::blocklib::initGrLocalisationBlocks(registry);
#endif

    // decimating BasicFilterProto variants: the ratio is the Resampling<in,out> template integers, and the gr::filter blocklib registers only the <1,1> identity (used by PulsedPowerDemo.grc)
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<100UZ, 1UZ, false>>, "">(registry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<40UZ, 1UZ, false>>, "">(registry);
    gr::registerBlock<gr::filter::BasicFilterProto<float, gr::Resampling<1UZ, 1UZ, false>>, "gr::filter::BasicFilterProto<float32, gr::Resampling<1, 1, false>>">(registry);

#ifndef __EMSCRIPTEN__
    gr::registerBlock<fair::picoscope::Picoscope<float, fair::picoscope::Picoscope4000a>, "">(registry);
    gr::registerBlock<fair::picoscope::Picoscope<gr::DataSet<float>, fair::picoscope::Picoscope4000a>, "">(registry);
#endif
}

} // namespace gr::od
