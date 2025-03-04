#ifndef OPENDIGITIZER_SIMPLETAGSOURCE_HPP
#define OPENDIGITIZER_SIMPLETAGSOURCE_HPP

#include <gnuradio-4.0/Block.hpp>

namespace opendigitizer {

template<typename T>
struct SimpleTagSource : public gr::Block<SimpleTagSource<T>> {

    gr::PortOut<T> out;

    gr::Size_t               n_samples_max{0};
    std::vector<gr::Size_t>  tag_indices;
    std::vector<std::string> tag_keys;
    std::vector<std::string> tag_values;
    float                    sample_rate = 100.0f;
    bool                     repeat_tags = true; // only for infinite mode

    GR_MAKE_REFLECTABLE(SimpleTagSource, out, n_samples_max, sample_rate, tag_indices, tag_keys, tag_values, repeat_tags);

    std::size_t _tagIndex{0};
    gr::Size_t  _nSamplesProduced{0UL};

    void start() {
        _nSamplesProduced = 0U;
        _tagIndex         = 0U;
        if (tag_indices.size() > 1) {
            bool isAscending = std::ranges::is_sorted(tag_indices, [](const auto& lhs, const auto& rhs) { return lhs < rhs; });
            if (!isAscending) {
                throw gr::exception("The input tag indices should be in ascending order.");
            }
        }
    }

    T processOne() noexcept {
        auto sleepDuration = std::chrono::microseconds(static_cast<int>(1e6f / sample_rate));
        std::this_thread::sleep_for(sleepDuration);
        if (_tagIndex < tag_indices.size() && tag_indices[_tagIndex] <= _nSamplesProduced % (tag_indices.back() + 1)) {
            gr::property_map map = {{tag_keys[_tagIndex], tag_values[_tagIndex]}};
            this->publishTag(map, 0UZ);
            this->_outputTagsChanged = true;
            _tagIndex++;
            if (repeat_tags && _tagIndex == tag_indices.size()) {
                _tagIndex = 0;
            }
        }

        _nSamplesProduced++;
        if (!isInfinite() && _nSamplesProduced >= n_samples_max) {
            this->requestStop();
        }

        return T(_nSamplesProduced);
    }

    [[nodiscard]] bool isInfinite() const { return n_samples_max == 0U; }
};

} // namespace opendigitizer

#endif
