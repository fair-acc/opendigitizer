#ifndef OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP
#define OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP

#include "DelayAssociation.hpp"
#include "DelayConstraints.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/algorithm/ArrayGeometry.hpp>
#include <gnuradio-4.0/localisation/SourceLocation.hpp>

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

GR_REGISTER_BLOCK(opendigitizer::localisation::DelayLocator)

struct DelayLocator : gr::Block<DelayLocator> {
    using Description = gr::Doc<R""(@brief Locates a source from dated arrival events: the mean of the consistent intersections of its TDOA hyperbolas or bistatic ellipses, one result event per event.
A result is published once every receiver of `pairs` has arrived, or a later event shows `coincidence_window` has passed;
its `trigger_meta_info` = {status, x, y, z, view_z, rms_range_residual, candidate_spread, n_supporting, n_candidates, diagnostics}.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::EventPortIn  evtIn;
    gr::EventPortOut evtOut{{.streamSlotsPerPublish = 8UZ}};

    ModeSetting                                                                                   mode                     = Mode::tdoa;
    gr::algorithm::ElementPositions                                                               element_positions        = kDeckSquare;
    ReceiverNames                                                                                 receiver_names           = {};
    TransmitterPositions                                                                          transmitter_positions    = kOneTransmitter;
    TransmitterNames                                                                              transmitter_names        = {};
    gr::algorithm::PropagationSpeed                                                               propagation_speed        = kSpeedOfSoundInAir;
    PairSelectionSetting                                                                          pairs                    = PairSelection::sequential;
    PairList                                                                                      pair_list                = {};
    CoincidenceWindow                                                                             coincidence_window       = 0.0;
    TimingOffsets                                                                                 timing_offsets           = {};
    DirectPathGuard                                                                               direct_path_guard        = 0.0;
    RegionOfInterest                                                                              roi                      = kDefaultRoi | std::ranges::to<std::vector>();
    ViewHeight                                                                                    view_z                   = std::numeric_limits<double>::quiet_NaN();
    CurveSegments                                                                                 curve_segments           = static_cast<gr::Size_t>(SolverSettings{}.curveSegments);
    CandidateMergeDistance                                                                        candidate_merge_distance = SolverSettings{}.candidateMergeDistance;
    ConsensusDistance                                                                             consensus_distance       = SolverSettings{}.consensusDistance;
    MaxRangeResidual                                                                              max_range_residual       = SolverSettings{}.maxRangeResidual;
    gr::TriggerName                                                                               trigger_name;
    A<gr::Size_t, "n_results", gr::Doc<"result events published">>                                n_results   = 0U;
    A<gr::Size_t, "n_unmatched", gr::Doc<"events naming no receiver or transmitter, or undated">> n_unmatched = 0U;

    GR_MAKE_REFLECTABLE(DelayLocator, evtIn, evtOut, mode, element_positions, receiver_names, transmitter_positions, transmitter_names, propagation_speed, pairs, pair_list, coincidence_window, timing_offsets, direct_path_guard, roi, view_z, curve_segments, candidate_merge_distance, consensus_distance, max_range_residual, trigger_name, n_results, n_unmatched);

    static constexpr std::size_t kMaxRecentEvents      = 256UZ; // older ones cannot belong to the newest event
    static constexpr std::size_t kMaxDiagnosticsLength = 240UZ; // characters in the result event, which must fit a 1024-byte event slot

    Geometry                                   _geometry;
    double                                     _viewZ = 0.0; // the planar view height, before any 3D solution
    gr::HistoryBuffer<Dated, kMaxRecentEvents> _recent{};
    Association                                _association;
    std::optional<std::uint64_t>               _publishedEpochNs;
    std::deque<gr::property_map>               _pending;

    void start() {
        if (trigger_name.value.empty()) {
            trigger_name = std::string(this->name.value);
        }
        _recent.reset();
        _publishedEpochNs.reset();
        _pending.clear();
        settingsChanged({}, {});
    }

    gr::work::Status processBulk(gr::InputSpanLike auto& evtInSpan, gr::OutputSpanLike auto& evtOutSpan) {
        for (const gr::property_map_view& event : evtInSpan) {
            admit(gr::property_map(event));
        }
        std::ignore = evtInSpan.consume(evtInSpan.size());
        evtOutSpan.publish(emitPending(_pending, evtOutSpan));
        return gr::work::Status::OK;
    }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        _geometry = geometryOf(*this);
        _viewZ    = viewHeightOf(view_z.value, std::nullopt, _geometry.receivers);
        if (changesGeometry(oldSettings, newSettings)) {
            _recent.reset();
        }
        _association = associate(_recent.get_span(0UZ), _geometry, _viewZ);
    }

    void admit(const gr::property_map& event) {
        const auto      dated = datedOf(event);
        const Geometry& g     = _geometry;
        if (!dated || (!indexOf(dated->name, g.receiverNames, g.receivers.size(), "") && !(g.mode == Mode::bistatic && indexOf(dated->name, g.transmitterNames, g.transmitters.size(), "tx")))) {
            n_unmatched = n_unmatched + 1U;
            return;
        }
        if (!_association.constraints.empty() && _publishedEpochNs != _association.epochNs && dated->secondsAfter(_association.epochNs) > g.window(_viewZ)) {
            publish(_association, g);
        }
        _recent.push_back(*dated);
        _association = associate(_recent.get_span(0UZ), g, _viewZ);
        if (complete(_association, g) && _publishedEpochNs != _association.epochNs) {
            publish(_association, g);
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
        constexpr std::size_t kMaxIterations   = 50UZ;
        constexpr double      kLocateTolerance = 1e-9;
        const auto&           present          = association.receivers;
        if (g.mode != Mode::tdoa || present.size() < kMinimumElements) {
            return {};
        }
        const auto [lowest, highest] = std::ranges::minmax(present | std::views::transform([&](std::size_t m) { return g.receivers[m][2]; }));
        if (highest - lowest <= g.tolerance()) {
            return {};
        }
        const gr::Tensor<double>  positions  = gr::algorithm::geometry::positionsOf(present | std::views::transform([&](std::size_t m) { return g.receivers[m]; }) | std::ranges::to<std::vector>());
        const std::vector<double> pairDelays = std::views::iota(0UZ, gr::algorithm::pairsOf(present.size())) | std::views::transform([&](std::size_t p) {
            const auto [i, j] = gr::algorithm::pairOf(p, present.size());
            return *association.onsetsS[present[i]] - *association.onsetsS[present[j]];
        }) | std::ranges::to<std::vector>();
        const auto                located    = gr::localisation::locate(positions, pairDelays, g.speed, gr::algorithm::Field::near, kMaxIterations, kLocateTolerance);
        if (!located) {
            return {};
        }
        return *located | std::views::transform(&gr::localisation::Located::point) | std::ranges::to<std::vector>();
    }

    void publish(const Association& association, const Geometry& g) {
        _publishedEpochNs             = association.epochNs;
        const auto           located  = locatedPoints(association, g);
        const double         viewZ    = viewHeightOf(view_z.value, located.empty() ? std::nullopt : std::optional{located.front()[2]}, g.receivers);
        const SolverSettings settings = solverSettingsOf(*this, viewZ);
        Solution             solution = g.speed > 0.0 && std::isfinite(g.speed) ? solve(association.constraints, settings) : Solution{.status = SolutionStatus::InvalidInput, .diagnostics = "propagation_speed must be positive and finite"};
        if (located.size() > 1UZ) {
            solution.status       = SolutionStatus::Ambiguous;
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
        const auto       accepted = static_cast<gr::Size_t>(std::ranges::count_if(solution.intersections, &Intersection::accepted));
        gr::property_map meta{{"status", std::string(statusName(solution.status))}, {"x", solution.mean[0]}, {"y", solution.mean[1]}, {"z", solution.mean[2]}, {"view_z", viewZ},                                 //
            {"rms_range_residual", solution.rmsRangeResidual}, {"candidate_spread", solution.candidateSpread}, {"n_supporting", static_cast<gr::Size_t>(solution.supporting.size())}, {"n_candidates", accepted}, //
            {"diagnostics", solution.diagnostics.substr(0UZ, kMaxDiagnosticsLength)}};
        _pending.push_back({{std::string(gr::tag::kTriggerName.key()), trigger_name.value}, {std::string(gr::tag::kTriggerTime.key()), association.epochNs}, {std::string(gr::tag::kTriggerOffset.key()), 0.f}, {std::string(gr::tag::kTriggerMetaInfo.key()), std::move(meta)}});
        n_results = n_results + 1U;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_DELAYLOCATOR_HPP
