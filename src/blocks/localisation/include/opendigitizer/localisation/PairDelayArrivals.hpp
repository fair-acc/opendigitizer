#ifndef OPENDIGITIZER_LOCALISATION_PAIRDELAYARRIVALS_HPP
#define OPENDIGITIZER_LOCALISATION_PAIRDELAYARRIVALS_HPP

#include "DelayAssociation.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/algorithm/ArrayGeometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace opendigitizer::localisation {

GR_REGISTER_BLOCK(opendigitizer::localisation::PairDelayArrivals, [T], [ float, double ])

template<typename T>
struct PairDelayArrivals : gr::Block<PairDelayArrivals<T>> {
    using Description = gr::Doc<R""(@brief Turns gr::localisation::PairDelay frames [P,C] (all pairs of M receivers in pairOf order, column 0 the delay t_i - t_j in s, column 2 the confidence) into M dated arrival events named by `receiver_names`.

Receiver 0 arrives at the frame's time (its gr:trigger_time tag, else the previous frame's time plus `frame_period`), receiver j at that time
minus the delay of pair (0, j). Frames in which one of these pairs' confidence is below `min_confidence` are dropped (PairDelay also emits frames of silence).
Receivers beyond `receiver_names` (when not empty) are unused inputs: they get no event and do not gate.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    static constexpr std::size_t kMaxReceivers = 64UZ; // one frame's arrivals leave in one work call; larger frames are refused

    gr::TensorPortIn<T> in;
    gr::EventPortOut    evtOut{{.streamSlotsPerPublish = kMaxReceivers}};

    ReceiverNames                                                                                                                                          receiver_names = {};
    A<double, "frame_period", gr::Unit<"s">, gr::Doc<"time from one frame to the next, dating untagged frames; 0: only tagged frames">>                    frame_period   = 0.0;
    A<double, "min_confidence", gr::Doc<"PairDelay's peak over its second peak; 0: every frame">>                                                          min_confidence = 0.0;
    A<gr::Size_t, "n_frames", gr::Doc<"frames turned into arrivals">>                                                                                      n_frames       = 0U;
    A<gr::Size_t, "n_refused", gr::Doc<"frames whose rows are no pair count, without columns, of over kMaxReceivers, undated or with a non-finite delay">> n_refused      = 0U;
    A<gr::Size_t, "n_gated", gr::Doc<"frames below min_confidence">>                                                                                       n_gated        = 0U;

    GR_MAKE_REFLECTABLE(PairDelayArrivals, in, evtOut, receiver_names, frame_period, min_confidence, n_frames, n_refused, n_gated);

    std::optional<std::uint64_t> _frameTimeNs;
    std::deque<gr::property_map> _pending;

    void start() {
        _frameTimeNs.reset();
        _pending.clear();
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& inSpan, gr::OutputSpanLike auto& evtSpan) {
        std::size_t emitted = 0UZ;
        const auto  drain   = [&] {
            while (!_pending.empty() && emitted < evtSpan.size() && gr::emit(evtSpan, emitted, gr::property_map_view{_pending.front()})) {
                _pending.pop_front();
                ++emitted;
            }
        };
        drain();
        std::size_t k = 0UZ;
        for (; k < inSpan.size() && _pending.empty(); ++k) { // a frame is read once the previous one's arrivals have left
            if (const auto tagged = triggerTimeAt(inSpan, k)) {
                _frameTimeNs = tagged;
            } else if (_frameTimeNs && frame_period.value > 0.0) {
                _frameTimeNs = *_frameTimeNs + static_cast<std::uint64_t>(std::llround(frame_period.value * 1e9));
            } else {
                _frameTimeNs.reset();
            }
            arrivalsOf(inSpan[k]);
            drain();
        }
        std::ignore = inSpan.consume(k);
        evtSpan.publish(emitted);
        return k == 0UZ && emitted == 0UZ && !_pending.empty() ? gr::work::Status::INSUFFICIENT_OUTPUT_ITEMS : gr::work::Status::OK;
    }

    [[nodiscard]] static std::optional<std::uint64_t> triggerTimeAt(auto& inSpan, std::size_t k) {
        std::optional<std::uint64_t> tagged;
        for (const auto& [index, tagMap] : inSpan.tags()) {
            if (const auto* time = gr::property_map_view{tagMap.get()}.template get_if<std::uint64_t>(gr::tag::kTriggerTime.key()); time != nullptr && static_cast<std::size_t>(index) == k) {
                tagged = *time;
            }
        }
        return tagged;
    }

    void arrivalsOf(const auto& frame) {
        const std::size_t     P                 = frame.rank() == 2UZ ? frame.extent(0UZ) : 0UZ;
        const std::size_t     C                 = P > 0UZ ? frame.extent(1UZ) : 0UZ;
        const auto            M                 = static_cast<std::size_t>(std::lround((1.0 + std::sqrt(1.0 + 8.0 * static_cast<double>(P))) / 2.0));
        constexpr std::size_t kConfidenceColumn = 2UZ;
        const std::size_t     named             = receiver_names.value.empty() ? M : std::min(M, receiver_names.value.size());
        const auto            delayOf           = [&](std::size_t j) { return static_cast<double>(frame.data()[(j - 1UZ) * C]); }; // pair (0, j) is row j - 1 in pairOf order
        if (P == 0UZ || C == 0UZ || M > kMaxReceivers || gr::algorithm::pairsOf(M) != P || !_frameTimeNs || !std::ranges::all_of(std::views::iota(1UZ, named), [&](std::size_t j) { return std::isfinite(delayOf(j)); })) {
            n_refused = n_refused + 1U;
            return;
        }
        const auto gates = [&](std::size_t j) { return static_cast<double>(frame.data()[(j - 1UZ) * C + kConfidenceColumn]) < min_confidence.value; }; // pair (0, j)
        if (C > kConfidenceColumn && min_confidence.value > 0.0 && std::ranges::any_of(std::views::iota(1UZ, named), gates)) {
            n_gated = n_gated + 1U;
            return;
        }
        _pending.push_back(datedEvent(nameOf(receiver_names.value, 0UZ, ""), 0.0, *_frameTimeNs));
        for (std::size_t j = 1UZ; j < named; ++j) {
            _pending.push_back(datedEvent(nameOf(receiver_names.value, j, ""), -delayOf(j), *_frameTimeNs));
        }
        n_frames = n_frames + 1U;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_PAIRDELAYARRIVALS_HPP
