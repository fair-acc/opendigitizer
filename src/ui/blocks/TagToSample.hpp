#ifndef OPENDIGITIZER_TAGTOSAMPLE_HPP
#define OPENDIGITIZER_TAGTOSAMPLE_HPP

#include <gnuradio-4.0/Block.hpp>

namespace opendigitizer {

template<typename T>
struct TagToSample : public gr::Block<TagToSample<T>> {
    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    std::string key_filter = "tag_id";

    T _currentValue = T(0);

    GR_MAKE_REFLECTABLE(TagToSample, in, out, key_filter);

    T processOne(T) noexcept {
        if (this->inputTagsPresent()) {
            const gr::property_map& tagMap = this->mergedInputTag().map;
            if (tagMap.contains(key_filter) && std::holds_alternative<std::string>(tagMap.at(key_filter))) {
                _currentValue = static_cast<T>(std::stod(std::get<std::string>(tagMap.at(key_filter))));
            }
        }
        return _currentValue;
    }
};

} // namespace opendigitizer

auto registerTagToSample = gr::registerBlock<opendigitizer::TagToSample, float, double>(gr::globalBlockRegistry());

#endif
