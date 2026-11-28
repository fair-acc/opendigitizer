#include "DashboardBlocks.hpp"

#include "blocks/Arithmetic.hpp"
#include "blocks/RemoteSource.hpp"
#include "blocks/StridedWindow.hpp"
#include "blocks/TagToSample.hpp"
#include "blocks/TestSpectrumGenerator.hpp"
#include "blocks/ToUncertainValue.hpp"
#include "charts/BlockTypes.hpp"

namespace gr::od {

void registerSignalBlocks(gr::BlockRegistry& registry) {
    registerWithIdPrefix<Arithmetic, float, double>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<TestSpectrumGenerator, float, double>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<TagToSample, float>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<StridedWindow<float>>(registry, kOpendigitizerIdPrefix); // strided windowing in front of the FFT (the gr4 FFT has no Stride<>), PulsedPowerDemo.grc
    registerWithIdPrefix<RemoteStreamSource, float, gr::UncertainValue<float>>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<RemoteDataSetSource, float, gr::UncertainValue<float>>(registry, kOpendigitizerIdPrefix);
    registerWithIdPrefix<ToUncertainValue, float, double>(registry, kOpendigitizerIdPrefix);
}

} // namespace gr::od
