#ifndef COUNTSOURCE_HPP
#define COUNTSOURCE_HPP

#include <deque>

template<typename T>
struct CountSource : public gr::Block<CountSource<T>> {
    gr::PortOut<T> out;

    uint32_t                 n_samples     = 0; ///< Number of samples to produce, 0 means infinite
    T                        initial_value = {};
    float                    sample_rate   = 1.;
    std::string              signal_name   = "test signal";
    std::string              signal_unit   = "test unit";
    float                    signal_min    = std::numeric_limits<float>::lowest(); ///< minimum value of the signal
    float                    signal_max    = std::numeric_limits<float>::max();    ///< maximum value of the signal
    std::string              direction     = "up";                                 ///< direction of the count, "up" or "down"
    std::vector<std::string> timing_tags;
    std::size_t              _produced = 0;
    std::deque<gr::Tag>      _pending_tags;

    GR_MAKE_REFLECTABLE(CountSource, out, n_samples, initial_value, sample_rate, signal_name, signal_unit, signal_min, signal_max, direction, timing_tags);

    void settingsChanged(const gr::property_map& /*old_settings*/, const gr::property_map& /*new_settings*/) {
        _produced = 0;
        _pending_tags.clear();

        for (const auto& tagStr : timing_tags) {
            auto       view = tagStr | std::ranges::views::split(',');
            const auto segs = std::vector(view.begin(), view.end());
            if (segs.size() != 2) {
                fmt::println(std::cerr, "Invalid tag: '{}'", tagStr);
                continue;
            }
            const auto                 indexStr = std::string_view(segs[0].begin(), segs[0].end());
            gr::Tag::signed_index_type index    = 0;
            if (const auto& [_, ec] = std::from_chars(indexStr.begin(), indexStr.end(), index); ec != std::errc{}) {
                fmt::println(std::cerr, "Invalid tag index '{}'", segs[0]);
                continue;
            }
            _pending_tags.emplace_back(index, gr::property_map{{std::string{gr::tag::TRIGGER_NAME.key()}, std::string{segs[1].begin(), segs[1].end()}}});
        }
    }

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) noexcept {
        // From the first processBulk() call, wait some time to give the test clients time to subscribe
        auto n = output.size();
        if (n_samples > 0) {
            const auto samplesLeft = static_cast<std::size_t>(n_samples) - _produced;
            if (samplesLeft == 0) {
                this->requestStop();
            }
            n = std::min(n, samplesLeft);
        }
        // chunk data so that there's one tag max, at index 0 in the chunk
        auto tagIt = _pending_tags.begin();
        if (tagIt != _pending_tags.end()) {
            if (static_cast<std::size_t>(tagIt->index) == _produced) {
                tagIt++;
            }
            if (tagIt != _pending_tags.end()) {
                n = std::min(n, static_cast<std::size_t>(tagIt->index) - _produced);
            }
        }
        if (!_pending_tags.empty() && _pending_tags[0].index == static_cast<gr::Tag::signed_index_type>(_produced)) {
            this->publishTag(_pending_tags[0].map, 0);
            _pending_tags.pop_front();
        }

        const auto subspan = std::span(output.begin(), output.end()).first(n);
        if (direction == "up") {
            std::iota(subspan.begin(), subspan.end(), initial_value + static_cast<T>(_produced));
        } else {
            std::iota(subspan.begin(), subspan.end(), initial_value - static_cast<T>(_produced + n - 1));
            std::reverse(subspan.begin(), subspan.end());
        }
        output.publish(n);
        _produced += n;
        return n > 0 ? gr::work::Status::OK : gr::work::Status::DONE;
    }
};

#endif
