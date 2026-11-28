#ifndef OPENDIGITIZER_TAGSINK_HPP
#define OPENDIGITIZER_TAGSINK_HPP

#include "../charts/SignalSink.hpp"
#include "../charts/SinkRegistry.hpp"

#include <gnuradio-4.0/Block.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gr::od {

template<typename T>
struct TagSink : gr::Block<TagSink<T>> {
    using Description = gr::Doc<R""(@brief Keeps the tags of a stream with their arrival time, as the signal of a chart that shows tags, e.g. an RDSView after an RDSDecoder.

A tag is kept when it contains at least one of the `tag_keys`; an empty list keeps every tag.
Only the newest `n_tag_history` tags are kept.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<T> in;

    gr::SignalName                                                                                                                           signal_name;
    A<std::vector<std::string>, "tag keys", gr::Visible, gr::Doc<"a tag is kept when it contains one of these keys; empty keeps every tag">> tag_keys;
    A<gr::Size_t, "tag history length", gr::Doc<"number of newest tags kept">>                                                               n_tag_history = 256U;

    GR_MAKE_REFLECTABLE(TagSink, in, signal_name, tag_keys, n_tag_history);

    std::shared_ptr<SignalSink> _sink       = std::make_shared<SignalSink>(SignalSink::Config{.kind = SignalKind::Events, .uniqueName = std::string(this->unique_name), .name = std::string(this->name.value), .capacity = static_cast<std::size_t>(n_tag_history.value), .capacityFloor = 0UZ, .doubleSamples = false, .withErrors = false});
    bool                        _registered = false;

    explicit TagSink(gr::property_map initParameters = {}) : gr::Block<TagSink<T>>(std::move(initParameters)) {}

    ~TagSink() {
        if (_registered) {
            try {
                charts::SinkRegistry::instance().unregisterSink(this->unique_name);
            } catch (...) {
            }
        }
    }

    [[nodiscard]] const std::shared_ptr<SignalSink>& signalSink() const noexcept { return _sink; }

    gr::work::Status processBulk(gr::InputSpanLike auto& input) noexcept {
        const double utcSeconds = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        {
            const DataGuard guard = _sink->dataGuard();
            for (const auto& [relIndex, tagMap] : input.tags(input.size())) {
                if (isKept(tagMap.get())) {
                    _sink->pushEvent(utcSeconds, tagMap.get());
                }
            }
            _sink->keepNewestEvents(static_cast<std::size_t>(n_tag_history.value)); // exactly, though a chart asks for more
            _sink->countSamples(input.size());
        }
        input.consumeTags(input.size());
        return gr::work::Status::OK;
    }

    void settingsChanged(const gr::property_map& /*oldSettings*/, const gr::property_map& /*newSettings*/) {
        {
            const DataGuard guard      = _sink->dataGuard();
            _sink->metadata.signalName = std::string(signal_name.value);
            _sink->setConfiguredCapacity(static_cast<std::size_t>(n_tag_history.value));
            _sink->keepNewestEvents(static_cast<std::size_t>(n_tag_history.value));
        }
        if (!_registered) {
            _sink->_name = this->name.value;
            _registered  = charts::SinkRegistry::instance().registerSink(_sink);
        }
    }

    [[nodiscard]] bool isKept(const gr::property_map& tag) const {
        return tag_keys.value.empty() || std::ranges::any_of(tag_keys.value, [&tag](const std::string& key) { return tag.contains(key); });
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_TAGSINK_HPP
