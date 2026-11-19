#include "Setup.hpp"

#include "blocks/Arithmetic.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/StridedWindow.hpp"
#include "blocks/TagToSample.hpp"
#include "blocks/TestSpectrumGenerator.hpp"

namespace DigitizerUi {

void registerSignalBlocks(gr::BlockRegistry& registry) {
    std::ignore = gr::registerBlock<opendigitizer::Arithmetic, float, double>(registry);
    std::ignore = gr::registerBlock<opendigitizer::TestSpectrumGenerator, float, double>(registry);
    std::ignore = gr::registerBlock<opendigitizer::TagToSample, float>(registry);
    std::ignore = gr::registerBlock<opendigitizer::StridedWindow<float>, "">(registry); // strided windowing in front of the FFT (the gr4 FFT has no Stride<>), PulsedPowerDemo.grc
    std::ignore = gr::registerBlock<opendigitizer::RemoteStreamSource, float, gr::UncertainValue<float>>(registry);
    std::ignore = gr::registerBlock<opendigitizer::RemoteDataSetSource, float, gr::UncertainValue<float>>(registry);
}

} // namespace DigitizerUi
