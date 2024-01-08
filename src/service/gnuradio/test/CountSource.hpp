#ifndef COUNTSOURCE_HPP
#define COUNTSOURCE_HPP

#include <deque>

template<typename T>
struct CountSource : public gr::Block<CountSource<T>> {
    using clock = std::chrono::system_clock;
    gr::PortOut<T>                   out;

    uint32_t                         n_samples   = 0;
    uint32_t                         delay_ms    = 0;
    std::string                      signal_name = "test signal";
    std::string                      signal_unit = "test unit";
    double                           sample_rate = 0.;
    std::string                      direction   = "up";
    std::vector<std::string>         timing_tags;
    std::size_t                      _produced = 0;
    std::deque<gr::Tag>              _pending_tags;
    std::optional<clock::time_point> _first_request;
    bool                             _waiting = true;

    void
    settingsChanged(const gr::property_map & /*old_settings*/, const gr::property_map & /*new_settings*/) {
        _produced = 0;
        _pending_tags.clear();

        gr::property_map channelInfo = gr::property_map{ { std::string(gr::tag::SAMPLE_RATE.key()), static_cast<float>(sample_rate) } };
        if (!signal_name.empty())
            channelInfo.emplace(std::string(gr::tag::SIGNAL_NAME.key()), signal_name);
        if (!signal_unit.empty())
            channelInfo.emplace(std::string(gr::tag::SIGNAL_UNIT.key()), signal_unit);
        _pending_tags.emplace_back(0, channelInfo);

        for (const auto &tagStr : timing_tags) {
            auto       view = tagStr | std::ranges::views::split(',');
            const auto segs = std::vector(view.begin(), view.end());
            if (segs.size() != 2) {
                fmt::println(std::cerr, "Invalid tag: '{}'", tagStr);
                continue;
            }
            const auto                 indexStr = std::string_view(segs[0].begin(), segs[0].end());
            gr::Tag::signed_index_type index    = 0;
            if (const auto &[_, ec] = std::from_chars(indexStr.begin(), indexStr.end(), index); ec != std::errc{}) {
                fmt::println(std::cerr, "Invalid tag index '{}'", segs[0]);
                continue;
            }
            _pending_tags.emplace_back(index, gr::property_map{ { std::string{ gr::tag::TRIGGER_NAME.key() }, std::string{ segs[1].begin(), segs[1].end() } } });
        }
    }

    gr::work::Status
    processBulk(gr::PublishableSpan auto &output) noexcept {
        // From the first processBulk() call, wait some time to give the test clients time to subscribe
        using enum gr::work::Status;
        if (!_first_request) {
            _first_request = clock::now();
            output.publish(0);
            return OK;
        }
        if (_waiting) {
            const auto now = clock::now();
            if (now - *_first_request < std::chrono::milliseconds(delay_ms)) {
                output.publish(0);
                return OK;
            }
            _waiting = false;
        }
        auto n = std::min(output.size(), static_cast<std::size_t>(n_samples) - _produced);
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
            this->output_tags()[0] = { 0, _pending_tags[0].map };
            _pending_tags.pop_front();
            this->forwardTags();
        }

        const auto subspan = std::span(output.begin(), output.end()).first(n);
        if (direction == "up") {
            std::iota(subspan.begin(), subspan.end(), static_cast<T>(_produced));
        } else {
            std::iota(subspan.begin(), subspan.end(), static_cast<T>(static_cast<std::size_t>(n_samples) - _produced - n));
            std::reverse(subspan.begin(), subspan.end());
        }
        output.publish(n);
        _produced += n;
        return n > 0 ? OK : DONE;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE(CountSource, out, n_samples, delay_ms, signal_name, signal_unit, sample_rate, direction, timing_tags)

#endif
