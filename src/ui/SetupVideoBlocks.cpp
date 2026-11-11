#include "Setup.hpp"

#include <opendigitizer/video/AcousticImage.hpp>
#include <opendigitizer/video/BrowserMicrophonePairs.hpp>
#include <opendigitizer/video/CameraSource.hpp>
#include <opendigitizer/video/ImageTransforms.hpp>

namespace DigitizerUi {

void registerVideoBlocks(gr::BlockRegistry& registry) {
    std::ignore = gr::registerBlock<opendigitizer::video::CameraSource>(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::GreyScale, float, double, std::uint32_t>(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::ImageCrop, float, double, std::uint32_t>(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::ImageResize, float, double, std::uint32_t>(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::ImageDistortion, float, double, std::uint32_t>(registry);
    std::ignore = gr::registerBlock<opendigitizer::video::AcousticImage>(registry);
#if defined(__EMSCRIPTEN__)
    std::ignore = gr::registerBlock<opendigitizer::video::BrowserMicrophonePairs>(registry);
#endif
}

} // namespace DigitizerUi
