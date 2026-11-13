#ifndef OPENDIGITIZER_TAGSINK_HPP
#define OPENDIGITIZER_TAGSINK_HPP

#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendigitizer {

template<typename T>
struct TagSink : gr::Block<TagSink<T>> {
    using Description = gr::Doc<R""(@brief Keeps the tags of a stream with their arrival time, as the signal of a chart that shows tags, e.g. an RDSView after an RDSDecoder.

A tag is kept when it contains at least one of the `tag_keys`; an empty list keeps every tag.
Only the newest `n_tag_history` tags are kept.)"">;

    struct ReceivedTag {
        double           utcSeconds;
        gr::property_map map;
    };

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<T> in;

    gr::SignalName                                                                                                                           signal_name;
    A<std::vector<std::string>, "tag keys", gr::Visible, gr::Doc<"a tag is kept when it contains one of these keys; empty keeps every tag">> tag_keys;
    A<gr::Size_t, "tag history length", gr::Doc<"number of newest tags kept">>                                                               n_tag_history = 256U;

    GR_MAKE_REFLECTABLE(TagSink, in, signal_name, tag_keys, n_tag_history);

    std::shared_ptr<std::mutex> _dataMutex = std::make_shared<std::mutex>();
    std::deque<ReceivedTag>     _tags;
    std::size_t                 _sampleCount = 0UZ;
    std::shared_ptr<SignalSink> _sinkAdapter;

    explicit TagSink(gr::property_map initParameters = {}) : gr::Block<TagSink<T>>(std::move(initParameters)) {}

    ~TagSink() {
        if (_sinkAdapter) {
            static_cast<SinkAdapter<TagSink>*>(_sinkAdapter.get())->invalidate();
            try {
                charts::SinkRegistry::instance().unregisterSink(this->unique_name);
            } catch (...) {
            }
        }
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        const double utcSeconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        {
            std::lock_guard lock(*_dataMutex);
            for (const auto& [relIndex, tagMap] : input.tags(input.size())) {
                if (isKept(tagMap.get())) {
                    _tags.push_back({utcSeconds, tagMap.get()});
                }
            }
            dropOldTags();
            _sampleCount += input.size();
        }
        input.consumeTags(input.size());
        return gr::work::Status::OK;
    }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& newSettings) {
        if (newSettings.contains("n_tag_history")) {
            std::lock_guard lock(*_dataMutex);
            dropOldTags();
        }
        if (!_sinkAdapter) {
            _sinkAdapter = std::make_shared<SinkAdapter<TagSink>>(*this);
            charts::SinkRegistry::instance().registerSink(_sinkAdapter);
        }
    }

    [[nodiscard]] bool isKept(const gr::property_map& tag) const {
        return tag_keys.value.empty() || std::ranges::any_of(tag_keys.value, [&tag](const std::string& key) { return tag.contains(key); });
    }

    void dropOldTags() {
        while (_tags.size() > static_cast<std::size_t>(n_tag_history.value)) {
            _tags.pop_front();
        }
    }

    [[nodiscard]] std::string_view            signalName() const noexcept { return signal_name.value.empty() ? std::string_view(this->name.value) : std::string_view(signal_name.value); }
    [[nodiscard]] SignalKind                  signalKind() const noexcept { return SignalKind::Streaming; }
    [[nodiscard]] std::shared_ptr<std::mutex> sharedDataMutex() const { return _dataMutex; }
    [[nodiscard]] std::size_t                 totalSampleCount() const noexcept { return _sampleCount; }
    [[nodiscard]] bool                        hasStreamingTags() const noexcept { return !_tags.empty(); }

    void forEachTag(std::function<void(double, const gr::property_map&)> callback) const {
        for (const ReceivedTag& tag : _tags) {
            callback(tag.utcSeconds, tag.map);
        }
    }
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_TAGSINK_HPP
