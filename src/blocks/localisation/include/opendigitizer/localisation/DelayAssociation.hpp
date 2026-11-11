#ifndef OPENDIGITIZER_LOCALISATION_DELAYASSOCIATION_HPP
#define OPENDIGITIZER_LOCALISATION_DELAYASSOCIATION_HPP

#include "DelayConstraints.hpp"

#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/algorithm/ArrayGeometry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendigitizer::localisation {

enum class PairSelection : std::uint8_t { sequential, all, listed };

using ModeSetting            = gr::Annotated<Mode, "mode", gr::Visible, gr::Doc<"tdoa: receiver-pair hyperbolas; bistatic: transmitter-receiver ellipses">>;
using ReceiverNames          = gr::Annotated<std::vector<std::string>, "receiver_names", gr::Doc<"trigger_name of each element_positions row, e.g. audio0, audio1; empty: \"0\", \"1\", ...">>;
using TransmitterPositions   = gr::Annotated<gr::Tensor<double>, "transmitter_positions", gr::Unit<"m">, gr::Doc<"[T,3], bistatic">>;
using TransmitterNames       = gr::Annotated<std::vector<std::string>, "transmitter_names", gr::Doc<"trigger_name of each transmitter row; empty: \"tx0\", ...">>;
using PairSelectionSetting   = gr::Annotated<PairSelection, "pairs", gr::Visible, gr::Doc<"TDOA receiver pairs: sequential (0-1, 1-2, ...), all, or listed in pair_list">>;
using PairList               = gr::Annotated<std::vector<gr::Size_t>, "pair_list", gr::Doc<"pairs: listed: receiver indices i0, j0, i1, j1, ...">>;
using CoincidenceWindow      = gr::Annotated<double, "coincidence_window", gr::Unit<"s">, gr::Doc<"how far apart one event's arrivals may be; 0: from the geometry">>;
using TimingOffsets          = gr::Annotated<std::vector<double>, "timing_offsets", gr::Unit<"s">, gr::Doc<"per receiver, subtracted from its arrival times">>;
using DirectPathGuard        = gr::Annotated<double, "direct_path_guard", gr::Unit<"s">, gr::Doc<"bistatic: arrivals before the direct path plus this are the direct path, not echoes; 0: no gating">>;
using RegionOfInterest       = gr::Annotated<std::vector<double>, "roi", gr::Unit<"m">, gr::Doc<"search rectangle x_min, x_max, y_min, y_max">>;
using ViewHeight             = gr::Annotated<double, "view_z", gr::Unit<"m">, gr::Doc<"height of the view plane; NaN: the located height, else the receivers' mean">>;
using CurveSegments          = gr::Annotated<gr::Size_t, "curve_segments", gr::Limits<gr::Size_t{8}, gr::Size_t{4096}>, gr::Doc<"per constraint curve">>;
using CandidateMergeDistance = gr::Annotated<double, "candidate_merge_distance", gr::Unit<"m">, gr::Doc<"intersections closer than this are one">>;
using ConsensusDistance      = gr::Annotated<double, "consensus_distance", gr::Unit<"m">, gr::Doc<"accepted intersections within this form one solution">>;
using MaxRangeResidual       = gr::Annotated<double, "max_range_residual", gr::Unit<"m">, gr::Doc<"a constraint supports a point within this range error">>;
using SourcePosition         = gr::Annotated<std::vector<double>, "source_position", gr::Unit<"m">, gr::Visible, gr::Doc<"x, y, z">>;
using OrbitRadius            = gr::Annotated<double, "orbit_radius", gr::Unit<"m">, gr::Visible, gr::Doc<"of the source's circle around source_position in the xy plane, once per orbit_period">>;
using OrbitPeriod            = gr::Annotated<double, "orbit_period", gr::Unit<"s">, gr::Limits<1e-3, 1e6>>;

inline const gr::Tensor<double> kDeckSquare        = gr::algorithm::geometry::grid(2UZ, 2UZ, 1.2);
inline const gr::Tensor<double> kOneTransmitter    = gr::Tensor<double>(std::array{1UZ, 3UZ}, std::vector<double>{0.0, -1.0, 0.0});
inline constexpr double         kSpeedOfSoundInAir = 343.0; // m/s

[[nodiscard]] inline bool changesGeometry(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
    return std::ranges::any_of(std::array{"mode", "element_positions", "receiver_names", "transmitter_positions", "transmitter_names", "pairs", "pair_list"}, [&](const char* key) {
        const auto now    = newSettings.find(std::pmr::string(key));
        const auto before = oldSettings.find(std::pmr::string(key));
        return now != newSettings.end() && (before == oldSettings.end() || before->second != now->second);
    });
}

[[nodiscard]] inline std::size_t rowsOf(const gr::Tensor<double>& positions) noexcept { return positions.rank() == 2UZ && positions.extent(1UZ) == 3UZ ? positions.extent(0UZ) : 0UZ; }

[[nodiscard]] inline Point3 rowOf(const gr::Tensor<double>& positions, std::size_t m) noexcept { return {positions[m, 0UZ], positions[m, 1UZ], positions[m, 2UZ]}; }

[[nodiscard]] inline std::vector<Point3> pointsOf(const gr::Tensor<double>& positions) {
    return std::views::iota(0UZ, rowsOf(positions)) | std::views::transform([&](std::size_t m) { return rowOf(positions, m); }) | std::ranges::to<std::vector>();
}

[[nodiscard]] inline double scaleOf(std::span<const Point3> foci) {
    constexpr double kMinScale = 1.0; // m
    return std::max(gr::algorithm::apertureOf(gr::algorithm::geometry::positionsOf(foci)), kMinScale);
}

[[nodiscard]] inline std::string nameOf(const std::vector<std::string>& names, std::size_t index, std::string_view defaultPrefix) { return index < names.size() ? names[index] : std::format("{}{}", defaultPrefix, index); }

[[nodiscard]] inline std::optional<std::size_t> indexOf(std::string_view name, const std::vector<std::string>& names, std::size_t count, std::string_view defaultPrefix) {
    const auto indices = std::views::iota(0UZ, count);
    const auto found   = std::ranges::find_if(indices, [&](std::size_t i) { return nameOf(names, i, defaultPrefix) == name; });
    return found != indices.end() ? std::optional{*found} : std::nullopt;
}

[[nodiscard]] inline std::vector<std::array<std::size_t, 2>> selectedPairs(PairSelection selection, std::span<const gr::Size_t> listed, std::size_t M) {
    std::vector<std::array<std::size_t, 2>> pairs;
    switch (selection) {
    case PairSelection::sequential:
        for (std::size_t i = 0UZ; i + 1UZ < M; ++i) {
            pairs.push_back({i, i + 1UZ});
        }
        break;
    case PairSelection::all:
        pairs = std::views::iota(0UZ, gr::algorithm::pairsOf(M)) | std::views::transform([M](std::size_t p) {
            const auto [i, j] = gr::algorithm::pairOf(p, M);
            return std::array{i, j};
        }) | std::ranges::to<std::vector>();
        break;
    case PairSelection::listed:
        for (std::size_t k = 0UZ; k + 1UZ < listed.size(); k += 2UZ) {
            if (listed[k] < M && listed[k + 1UZ] < M && listed[k] != listed[k + 1UZ]) {
                pairs.push_back({listed[k], listed[k + 1UZ]});
            }
        }
        break;
    }
    return pairs;
}

struct Dated {
    std::string   name;
    std::uint64_t ns      = 0U;
    double        offsetS = 0.0;

    [[nodiscard]] double secondsAfter(std::uint64_t epochNs) const noexcept { return static_cast<double>(static_cast<std::int64_t>(ns - epochNs)) * 1e-9 + offsetS; }
};

[[nodiscard]] inline std::optional<Dated> datedOf(const gr::property_map& event) {
    const gr::property_map_view view{event};
    const auto                  name = view.get_if<std::string_view>(gr::tag::kTriggerName.key());
    const auto*                 time = view.get_if<std::uint64_t>(gr::tag::kTriggerTime.key());
    if (!name || time == nullptr) {
        return std::nullopt;
    }
    const auto* offset = view.get_if<float>(gr::tag::kTriggerOffset.key());
    return Dated{.name = std::string(*name), .ns = *time, .offsetS = offset != nullptr ? static_cast<double>(*offset) : 0.0};
}

[[nodiscard]] inline gr::property_map datedEvent(std::string_view name, double seconds, std::uint64_t epochNs = 0U) {
    const auto wholeNs = static_cast<std::int64_t>(std::floor(seconds * 1e9));
    return {{std::string(gr::tag::kTriggerName.key()), std::string(name)}, {std::string(gr::tag::kTriggerTime.key()), epochNs + static_cast<std::uint64_t>(wholeNs)}, {std::string(gr::tag::kTriggerOffset.key()), static_cast<float>(seconds - static_cast<double>(wholeNs) * 1e-9)}};
}

[[nodiscard]] inline Point3 orbitPoint(const std::vector<double>& centre, double radius, double period, double t) {
    const Point3 base  = centre.size() == 3UZ ? Point3{centre[0], centre[1], centre[2]} : Point3{};
    const double angle = 2.0 * std::numbers::pi * t / period;
    return {base[0] + radius * std::cos(angle), base[1] + radius * std::sin(angle), base[2]};
}

struct Geometry {
    Mode                                    mode = Mode::tdoa;
    std::vector<Point3>                     receivers;
    std::vector<std::string>                receiverNames;
    std::vector<Point3>                     transmitters;
    std::vector<std::string>                transmitterNames;
    std::vector<std::array<std::size_t, 2>> pairs;
    double                                  speed   = kSpeedOfSoundInAir;
    double                                  windowS = 0.0; // 0: from the geometry
    std::vector<double>                     offsetsS;
    double                                  directPathGuardS = 0.0;
    std::array<double, 4>                   roi              = kDefaultRoi;

    [[nodiscard]] double tolerance() const {
        constexpr double    kRelativeTolerance = 1e-9;
        std::vector<Point3> foci               = receivers;
        foci.insert(foci.end(), transmitters.begin(), transmitters.end());
        return kRelativeTolerance * scaleOf(foci);
    }

    [[nodiscard]] double window(double viewZ) const {
        constexpr double kTdoaWindowMargin = 1.1;
        if (windowS > 0.0) {
            return windowS;
        }
        const double c = speed > 0.0 ? speed : kSpeedOfSoundInAir;
        if (mode == Mode::tdoa) {
            return kTdoaWindowMargin * scaleOf(receivers) / c;
        }
        double longest = 0.0;
        for (const Point3& s : transmitters) {
            for (const Point3& r : receivers) {
                for (const Point2& corner : {Point2{roi[0], roi[2]}, Point2{roi[0], roi[3]}, Point2{roi[1], roi[2]}, Point2{roi[1], roi[3]}}) {
                    const Point3 p{corner[0], corner[1], viewZ};
                    longest = std::max(longest, distance(s, p) + distance(p, r));
                }
            }
        }
        return longest / c;
    }

    [[nodiscard]] double offsetOf(std::size_t m) const noexcept { return m < offsetsS.size() ? offsetsS[m] : 0.0; }
};

[[nodiscard]] inline std::array<double, 4> roiOf(const std::vector<double>& roi) noexcept { return roi.size() == 4UZ ? std::array{roi[0], roi[1], roi[2], roi[3]} : kDefaultRoi; }

[[nodiscard]] Geometry geometryOf(const auto& block) {
    return {.mode         = block.mode,
        .receivers        = pointsOf(block.element_positions.value),
        .receiverNames    = block.receiver_names.value,
        .transmitters     = pointsOf(block.transmitter_positions.value), //
        .transmitterNames = block.transmitter_names.value,
        .pairs            = selectedPairs(block.pairs, block.pair_list.value, rowsOf(block.element_positions.value)),
        .speed            = block.propagation_speed.value, //
        .windowS          = block.coincidence_window.value,
        .offsetsS         = block.timing_offsets.value,
        .directPathGuardS = block.direct_path_guard.value,
        .roi              = roiOf(block.roi.value)};
}

[[nodiscard]] SolverSettings solverSettingsOf(const auto& block, double viewZ) { return {.roi = roiOf(block.roi.value), .viewZ = viewZ, .curveSegments = block.curve_segments.value, .candidateMergeDistance = block.candidate_merge_distance.value, .consensusDistance = block.consensus_distance.value, .maxRangeResidual = block.max_range_residual.value}; }

[[nodiscard]] inline double viewHeightOf(double viewZ, std::optional<double> located, const std::vector<Point3>& receivers) {
    if (std::isfinite(viewZ)) {
        return viewZ;
    }
    if (located) {
        return *located;
    }
    const double sum = std::ranges::fold_left(receivers | std::views::transform([](const Point3& p) { return p[2]; }), 0.0, std::plus<>{});
    return receivers.empty() ? 0.0 : sum / static_cast<double>(receivers.size());
}

/// the constraints of the newest event: TDOA, each receiver's latest onset (an arrival more than the window after its previous
/// one; re-triggers within the window are ignored), a pair kept when its two onsets lie within the window; bistatic, the latest
/// emission and each receiver's first arrival after it (past the direct path plus its guard) within the window; arrivals before the
/// emission are ignored
struct Association {
    std::vector<Constraint>            constraints;
    std::vector<std::size_t>           receivers;
    std::vector<std::optional<double>> onsetsS;      // TDOA: per receiver, its onset after the newest one, offset subtracted
    std::uint64_t                      epochNs = 0U; // the earliest arrival used (TDOA) or the emission (bistatic)
    std::optional<std::size_t>         transmitter;
};

[[nodiscard]] inline Association associate(std::span<const Dated> events, const Geometry& geometry, double viewZ) {
    Association       association;
    const std::size_t M         = geometry.receivers.size();
    const double      windowS   = geometry.window(viewZ);
    const double      tolerance = geometry.tolerance();
    const auto        receiver  = [&](const Dated& e) { return indexOf(e.name, geometry.receiverNames, M, ""); };
    if (geometry.mode == Mode::bistatic) {
        const Dated* emission = nullptr;
        for (const Dated& e : events) {
            const auto k = indexOf(e.name, geometry.transmitterNames, geometry.transmitters.size(), "tx");
            if (k && (emission == nullptr || e.secondsAfter(emission->ns) > emission->offsetS)) {
                emission                = &e;
                association.transmitter = k;
            }
        }
        if (emission == nullptr) {
            return association;
        }
        association.epochNs                  = emission->ns;
        const Point3&                      s = geometry.transmitters[*association.transmitter];
        std::vector<std::optional<double>> echo(M);
        for (const Dated& e : events) {
            const auto m = receiver(e);
            if (!m) {
                continue;
            }
            const double since  = e.secondsAfter(emission->ns) - emission->offsetS - geometry.offsetOf(*m);
            const double direct = geometry.directPathGuardS > 0.0 ? distance(s, geometry.receivers[*m]) / geometry.speed + geometry.directPathGuardS : 0.0;
            if (since >= direct && since <= windowS && (!echo[*m] || since < *echo[*m])) {
                echo[*m] = since;
            }
        }
        for (std::size_t m = 0UZ; m < M; ++m) {
            if (echo[m]) {
                association.constraints.push_back(bistaticConstraint(s, geometry.receivers[m], transmitterFocus(*association.transmitter), static_cast<int>(m), geometry.speed * *echo[m], tolerance));
                association.receivers.push_back(m);
            }
        }
        return association;
    }
    std::vector<const Dated*> ordered = events | std::views::transform([](const Dated& e) { return &e; }) | std::ranges::to<std::vector>();
    std::ranges::stable_sort(ordered, [](const Dated* a, const Dated* b) { return a->secondsAfter(b->ns) < b->offsetS; });
    std::vector<const Dated*> latest(M, nullptr);
    for (const Dated* e : ordered) {
        if (const auto m = receiver(*e); m && (latest[*m] == nullptr || e->secondsAfter(latest[*m]->ns) - latest[*m]->offsetS > windowS)) {
            latest[*m] = e;
        }
    }
    const Dated* newest = nullptr;
    for (const Dated* e : latest) {
        if (e != nullptr && (newest == nullptr || e->secondsAfter(newest->ns) > newest->offsetS)) {
            newest = e;
        }
    }
    if (newest == nullptr) {
        return association;
    }
    association.epochNs = newest->ns;
    association.onsetsS.resize(M);
    for (std::size_t m = 0UZ; m < M; ++m) {
        if (latest[m] != nullptr && std::abs(latest[m]->secondsAfter(newest->ns)) <= windowS) {
            association.receivers.push_back(m);
            association.onsetsS[m] = latest[m]->secondsAfter(newest->ns) - geometry.offsetOf(m);
            association.epochNs    = std::min(association.epochNs, latest[m]->ns);
        }
    }
    for (const auto [i, j] : geometry.pairs) {
        if (std::ranges::contains(association.receivers, i) && std::ranges::contains(association.receivers, j)) {
            association.constraints.push_back(tdoaConstraint(geometry.receivers[i], geometry.receivers[j], static_cast<int>(i), static_cast<int>(j), geometry.speed * (*association.onsetsS[i] - *association.onsetsS[j]), tolerance));
        }
    }
    return association;
}

/// moves events from the front of `pending` into the slots of `evtSpan` from `from` on until the span is full; returns one past the last slot filled
[[nodiscard]] std::size_t emitPending(std::deque<gr::property_map>& pending, auto& evtSpan, std::size_t from = 0UZ) {
    std::size_t next = from;
    while (!pending.empty() && next < evtSpan.size()) {
        const auto emitted = gr::emit(evtSpan, next, gr::property_map_view{pending.front()});
        if (!emitted && emitted.error() != gr::EmitError::tooLarge) {
            break;
        }
        next += emitted ? 1UZ : 0UZ;
        pending.pop_front(); // an event too large for any slot is dropped, not retried
    }
    return next;
}

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_DELAYASSOCIATION_HPP
