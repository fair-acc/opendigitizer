#include "Setup.hpp"

#include <opendigitizer/localisation/DataSetToTensor.hpp>
#include <opendigitizer/localisation/DelayLocator.hpp>
#include <opendigitizer/localisation/PairDelayArrivals.hpp>
#include <opendigitizer/localisation/PointSourceArrivals.hpp>
#include <opendigitizer/localisation/PointSourceAudio.hpp>

#include <gnuradio-4.0/filter/time_domain_filter.hpp>

#if defined(OD_TRIGGER_BLOCKS)
#include <gnuradio-4.0/trigger/Coincidence.hpp>
#include <gnuradio-4.0/trigger/MultiChannelRecorder.hpp>
#include <gnuradio-4.0/trigger/SchmittTrigger.hpp>
#include <gnuradio-4.0/trigger/TagBridge.hpp>
#include <gnuradio-4.0/trigger/TagToEvent.hpp>
#include <gnuradio-4.0/trigger/ValueTrigger.hpp>
#endif

namespace DigitizerUi {

void registerLocalisationBlocks(gr::BlockRegistry& registry) {
    std::ignore = gr::registerBlock<opendigitizer::localisation::DelayLocator>(registry);
    std::ignore = gr::registerBlock<opendigitizer::localisation::PointSourceArrivals>(registry);
    std::ignore = gr::registerBlock<opendigitizer::localisation::PairDelayArrivals, float, double>(registry);
    std::ignore = gr::registerBlock<opendigitizer::localisation::DataSetToTensor, float, double>(registry);
    std::ignore = gr::registerBlock<gr::filter::BasicFilter<float>, "gr::filter::BasicFilter<float>">(registry); // the localisation dashboards' high-pass, under its alias
    std::ignore = gr::registerBlock<opendigitizer::localisation::PointSourceAudio>(registry);
#if defined(OD_TRIGGER_BLOCKS) // float only, under GR4's names: the whole trigger blocklib adds 35 % to index.wasm
    std::ignore = gr::registerBlock<gr::blocks::trigger::ValueTrigger<float, gr::blocks::trigger::ValueCondition::level>, "gr::blocks::trigger::LevelTrigger<float, gr::blocks::trigger::ValueCondition::level>">(registry);
    std::ignore = gr::registerBlock<gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::LINEAR_INTERPOLATION>, "gr::blocks::trigger::SchmittTrigger<float, gr::trigger::InterpolationMethod::LINEAR_INTERPOLATION>">(registry);
    std::ignore = gr::registerBlock<gr::blocks::trigger::Coincidence>(registry);
    std::ignore = gr::registerBlock<gr::blocks::trigger::EventToTag<float>>(registry);
    std::ignore = gr::registerBlock<gr::blocks::trigger::MultiChannelRecorder<float>>(registry);
    std::ignore = gr::registerBlock<gr::blocks::trigger::TagToEvent<float>>(registry);
#endif
}

} // namespace DigitizerUi
