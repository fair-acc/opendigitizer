#ifndef OPENDIGITIZER_TAGTOSAMPLE_HPP
#define OPENDIGITIZER_TAGTOSAMPLE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/PmtTypeHelpers.hpp>

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
            if (const auto it = tagMap.find(key_filter); it != tagMap.end()) {
                const gr::pmt::Value& value  = it->second;
                const auto            parsed = value.is_string() ? gr::pmt::convert_safely<T>(std::string(value.value_or(std::string_view{}))) : gr::pmt::convert_safely<T>(value);
                _currentValue                = parsed.value_or(_currentValue);
            }
        }
        return _currentValue;
    }
};

} // namespace opendigitizer

auto registerTagToSample = gr::registerBlock<opendigitizer::TagToSample, float>(gr::globalBlockRegistry());

#endif
