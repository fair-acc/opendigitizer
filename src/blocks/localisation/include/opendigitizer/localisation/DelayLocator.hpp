#ifndef OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP
#define OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP

#include "DelayAssociation.hpp"
#include "DelayConstraints.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/algorithm/ArrayGeometry.hpp>
#if __has_include(<gnuradio-4.0/localisation/SourceLocation.hpp>)
#include <gnuradio-4.0/localisation/SourceLocation.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <format>
#include <limits>
#include <optional>
#include <ranges>
#include <string>
#include <vector>

namespace opendigitizer::localisation {

/// GR4's `locate` result as points: after 6b29d783 every position that fits, nearest first; before, the one it converged to
template<typename TLocated>
[[nodiscard]] std::vector<Point3> pointsOfLocated(const TLocated& located) {
    if constexpr (requires { located.front().point; }) {
        return located | std::views::transform([](const auto& row) { return Point3{row.point[0], row.point[1], row.point[2]}; }) | std::ranges::to<std::vector>();
    } else {
        return {Point3{located.point[0], located.point[1], located.point[2]}};
    }
}

GR_REGISTER_BLOCK(opendigitizer::localisation::DelayLocator)

struct DelayLocator : gr::Block<DelayLocator> {
    using Description = gr::Doc<R""(@brief Locates a source from dated arrival events: the mean of the consistent intersections of its TDOA hyperbolas or bistatic ellipses, one result event per event.

Arrival events are named by `receiver_names` (emissions by `transmitter_names`); each receiver's latest arrival counts, pairs within
`coincidence_window`; bistatic arrivals before their emission are ignored. A result is published when every receiver of `pairs` has
arrived, or when a later event shows the window has passed: `trigger_meta_info` = {status, x, y, z, view_z, rms_range_residual,
candidate_spread, n_supporting, n_candidates, diagnostics}. LocalisationChart draws the same curves from the same events.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::EventPortIn  evtIn;
    gr::EventPortOut evtOut{{.streamSlotsPerPublish = 8UZ}};

    ModeSetting                                                                                   mode                     = Mode::tdoa;
    gr::algorithm::ElementPositions                                                               element_positions        = kDeckSquare;
    ReceiverNames                                                                                 receiver_names           = {};
    TransmitterPositions                                                                          transmitter_positions    = kOneTransmitter;
    TransmitterNames                                                                              transmitter_names        = {};
    DelayConventionSetting                                                                        delay_convention         = DelayConvention::total;
    gr::algorithm::PropagationSpeed                                                               propagation_speed        = 343.0;
    PairSelectionSetting                                                                          pairs                    = PairSelection::sequential;
    PairList                                                                                      pair_list                = {};
    CoincidenceWindow                                                                             coincidence_window       = 0.0;
    TimingOffsets                                                                                 timing_offsets           = {};
    DirectPathGuard                                                                               direct_path_guard        = 0.0;
    RegionOfInterest                                                                              roi                      = kDefaultRoi;
    ViewHeight                                                                                    view_z                   = std::numeric_limits<double>::quiet_NaN();
    CurveSegments                                                                                 curve_segments           = 192U;
    CandidateMergeDistance                                                                        candidate_merge_distance = 0.02;
    ConsensusDistance                                                                             consensus_distance       = 0.15;
    MaxRangeResidual                                                                              max_range_residual       = 0.05;
    gr::TriggerName                                                                               trigger_name;
    A<gr::Size_t, "n_results", gr::Doc<"result events published">>                                n_results         = 0U;
    A<gr::Size_t, "n_unmatched", gr::Doc<"events naming no receiver or transmitter, or undated">> n_unmatched       = 0U;
    A<gr::Size_t, "n_results_dropped", gr::Doc<"results a full evtOut refused">>                  n_results_dropped = 0U;

    GR_MAKE_REFLECTABLE(DelayLocator, evtIn, evtOut, mode, element_positions, receiver_names, transmitter_positions, transmitter_names, delay_convention, propagation_speed, pairs, pair_list, coincidence_window, timing_offsets, direct_path_guard, roi, view_z, curve_segments, candidate_merge_distance, consensus_distance, max_range_residual, trigger_name, n_results, n_unmatched, n_results_dropped);

    static constexpr std::size_t kMaxRecentEvents      = 256UZ; // older ones cannot belong to the newest event
    static constexpr std::size_t kMaxDiagnosticsLength = 240UZ; // characters in the result event, which must fit a 1024-byte event slot

    std::deque<Dated>            _recent;
    std::optional<std::uint64_t> _publishedEpochNs;
    std::deque<gr::property_map> _pending;

    void start() {
        if (trigger_name.value.empty()) {
            trigger_name = std::string(this->name.value);
        }
        _recent.clear();
        _publishedEpochNs.reset();
        _pending.clear();
    }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        if (changesGeometry(oldSettings, newSettings)) {
            _recent.clear();
        }
    }

    void stop() {
        const Geometry g       = geometryOf(*this);
        const auto     current = associate(std::vector(_recent.begin(), _recent.end()), g, viewHeightOf(view_z.value, std::nullopt, pointsOf(element_positions.value)));
        if (!current.constraints.empty() && _publishedEpochNs != current.epochNs) {
            publish(current, g);
        }
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& evtInSpan, gr::OutputSpanLike auto& evtOutSpan) {
        for (const gr::property_map_view& event : evtInSpan) {
            admit(gr::property_map(event));
        }
        std::ignore         = evtInSpan.consume(evtInSpan.size());
        std::size_t emitted = 0UZ;
        while (!_pending.empty() && emitted < evtOutSpan.size()) {
            if (gr::emit(evtOutSpan, emitted, gr::property_map_view{_pending.front()})) {
                ++emitted;
            } else {
                n_results_dropped = n_results_dropped + 1U;
            }
            _pending.pop_front();
        }
        evtOutSpan.publish(emitted);
        return gr::work::Status::OK;
    }

    void admit(const gr::property_map& event) {
        const auto     dated = datedOf(event);
        const Geometry g     = geometryOf(*this);
        if (!dated || (!indexOf(dated->name, g.receiverNames, g.receivers.size(), "") && !(g.mode == Mode::bistatic && indexOf(dated->name, g.transmitterNames, g.transmitters.size(), "tx")))) {
            n_unmatched = n_unmatched + 1U;
            return;
        }
        const double viewZ    = viewHeightOf(view_z.value, std::nullopt, pointsOf(element_positions.value));
        const auto   previous = associate(std::vector(_recent.begin(), _recent.end()), g, viewZ);
        if (!previous.constraints.empty() && _publishedEpochNs != previous.epochNs && dated->secondsAfter(previous.epochNs) > g.window(viewZ)) {
            publish(previous, g);
        }
        _recent.push_back(*dated);
        while (_recent.size() > kMaxRecentEvents) {
            _recent.pop_front();
        }
        const auto current = associate(std::vector(_recent.begin(), _recent.end()), g, viewZ);
        if (complete(current, g) && _publishedEpochNs != current.epochNs) {
            publish(current, g);
        }
    }

    [[nodiscard]] static bool complete(const Association& association, const Geometry& g) {
        if (association.constraints.empty()) {
            return false;
        }
        if (g.mode == Mode::bistatic) {
            return association.receivers.size() == g.receivers.size();
        }
        return std::ranges::all_of(g.pairs, [&](const auto& pair) { return std::ranges::contains(association.receivers, pair[0]) && std::ranges::contains(association.receivers, pair[1]); });
    }

    /// TDOA receivers not at one height: GR4's near-field solutions of their arrival differences, one, or two where both fit exactly
    [[nodiscard]] std::vector<Point3> locatedPoints(const Association& association, const Geometry& g) const {
        constexpr std::size_t kMinimumElements = 4UZ;
        const auto&           present          = association.receivers;
        if (g.mode != Mode::tdoa || present.size() < kMinimumElements) {
            return {};
        }
        const auto [lowest, highest] = std::ranges::minmax(present | std::views::transform([&](std::size_t m) { return g.receivers[m][2]; }));
        if (highest - lowest <= g.tolerance()) {
            return {};
        }
#if __has_include(<gnuradio-4.0/localisation/SourceLocation.hpp>)
        std::vector<std::optional<double>> times(g.receivers.size());
        for (const Dated& e : _recent) {
            if (const auto m = indexOf(e.name, g.receiverNames, g.receivers.size(), ""); m && std::ranges::contains(present, *m)) {
                times[*m] = e.secondsAfter(association.epochNs) - g.offsetOf(*m);
            }
        }
        gr::Tensor<double> positions(std::array{present.size(), 3UZ}, std::vector<double>(present.size() * 3UZ));
        for (std::size_t k = 0UZ; k < present.size(); ++k) {
            for (std::size_t c = 0UZ; c < 3UZ; ++c) {
                positions[k, c] = g.receivers[present[k]][c];
            }
        }
        std::vector<double> pairDelays;
        for (std::size_t p = 0UZ; p < gr::algorithm::pairsOf(present.size()); ++p) {
            const auto [i, j] = gr::algorithm::pairOf(p, present.size());
            pairDelays.push_back(times[present[i]].value_or(0.0) - times[present[j]].value_or(0.0));
        }
        constexpr std::size_t kMaxIterations = 50UZ;
        const auto            located        = gr::localisation::locate(positions, pairDelays, g.speed, gr::algorithm::Field::near, kMaxIterations, 1e-9);
        if (!located) {
            return {};
        }
        return pointsOfLocated(*located);
#else
        return {};
#endif
    }

    void publish(const Association& association, const Geometry& g) {
        _publishedEpochNs       = association.epochNs;
        const auto     located  = locatedPoints(association, g);
        const double   viewZ    = viewHeightOf(view_z.value, located.empty() ? std::nullopt : std::optional{located.front()[2]}, g.receivers);
        const Settings settings = solverSettingsOf(*this, viewZ);
        Solution       solution = g.speed > 0.0 && std::isfinite(g.speed) ? solve(association.constraints, settings) : Solution{.status = Status::invalidInput, .diagnostics = "propagation_speed must be positive and finite"};
        if (located.size() > 1UZ) {
            solution.status       = Status::ambiguous;
            solution.mean         = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
            solution.alternatives = located;
            solution.diagnostics += std::format("{}{} positions fit the delays exactly", solution.diagnostics.empty() ? "" : "; ", located.size());
        }
        std::string missing;
        for (std::size_t m = 0UZ; m < g.receivers.size(); ++m) {
            if (!std::ranges::contains(association.receivers, m)) {
                missing += std::format("{}{}", missing.empty() ? "no arrival from " : ", ", nameOf(g.receiverNames, m, ""));
            }
        }
        if (!missing.empty()) {
            solution.diagnostics += std::format("{}{}", solution.diagnostics.empty() ? "" : "; ", missing);
        }
        const auto       accepted = static_cast<gr::Size_t>(std::ranges::count_if(solution.intersections, [](const Intersection& i) { return i.accepted; }));
        gr::property_map meta{{"status", std::string(statusName(solution.status))}, {"x", solution.mean[0]}, {"y", solution.mean[1]}, {"z", solution.mean[2]}, {"view_z", viewZ},                                 //
            {"rms_range_residual", solution.rmsRangeResidual}, {"candidate_spread", solution.candidateSpread}, {"n_supporting", static_cast<gr::Size_t>(solution.supporting.size())}, {"n_candidates", accepted}, //
            {"diagnostics", solution.diagnostics.substr(0UZ, kMaxDiagnosticsLength)}};
        _pending.push_back({{std::string(gr::tag::kTriggerName.key()), trigger_name.value}, {std::string(gr::tag::kTriggerTime.key()), association.epochNs}, {std::string(gr::tag::kTriggerOffset.key()), 0.f}, {std::string(gr::tag::kTriggerMetaInfo.key()), std::move(meta)}});
        n_results = n_results + 1U;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP
