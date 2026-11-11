#include <opendigitizer/localisation/DataSetToTensor.hpp>
#include <opendigitizer/localisation/DelayConstraints.hpp>
#include <opendigitizer/localisation/DelayLocator.hpp>
#include <opendigitizer/localisation/PairDelayArrivals.hpp>
#include <opendigitizer/localisation/PointSourceArrivals.hpp>
#include <opendigitizer/localisation/PointSourceAudio.hpp>

#include <boost/ut.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/ClockSource.hpp>
#include <gnuradio-4.0/basic/StreamToTensor.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>
#include <gnuradio-4.0/localisation/PairCrossSpectra.hpp>
#include <gnuradio-4.0/testing/EventScript.hpp>
#include <gnuradio-4.0/testing/TagMonitors.hpp>
#include <gnuradio-4.0/testing/TensorSource.hpp>
#include <gnuradio-4.0/trigger/EventShaping.hpp>
#include <gnuradio-4.0/trigger/MultiChannelRecorder.hpp>
#include <gnuradio-4.0/trigger/ValueTrigger.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <numbers>
#include <ranges>
#include <string>
#include <vector>

namespace {

using namespace opendigitizer::localisation;

constexpr double kSpeed = 343.0;

struct EventLog : gr::Block<EventLog> {
    gr::EventPortIn evtIn;
    GR_MAKE_REFLECTABLE(EventLog, evtIn);

    std::vector<gr::property_map> _events;
    std::size_t                   _stopAfter = 1UZ;

    gr::work::Status processBulk(gr::InputSpanLike auto& evtSpan) {
        for (const gr::property_map_view& event : evtSpan) {
            _events.push_back(gr::property_map(event));
        }
        std::ignore = evtSpan.consume(evtSpan.size());
        if (_events.size() >= _stopAfter) {
            this->requestStop();
            return gr::work::Status::DONE;
        }
        return gr::work::Status::OK;
    }
};

struct DataSetSource : gr::Block<DataSetSource> {
    gr::PortOut<gr::DataSet<float>> out;
    GR_MAKE_REFLECTABLE(DataSetSource, out);

    std::vector<gr::DataSet<float>> _sets;
    std::size_t                     _sent = 0UZ;

    gr::work::Status processBulk(gr::OutputSpanLike auto& output) {
        const std::size_t n = std::min(output.size(), _sets.size() - _sent);
        std::copy_n(std::next(_sets.begin(), static_cast<std::ptrdiff_t>(_sent)), n, output.begin());
        output.publish(n);
        _sent += n;
        return _sent == _sets.size() ? gr::work::Status::DONE : gr::work::Status::OK;
    }
};

[[nodiscard]] std::vector<Point3> deckSquare() { return {{-0.6, -0.6, 0.0}, {-0.6, 0.6, 0.0}, {0.6, -0.6, 0.0}, {0.6, 0.6, 0.0}}; }

[[nodiscard]] std::vector<Constraint> tdoaOf(const std::vector<Point3>& receivers, const Point3& source, double error = 0.0) {
    std::vector<Constraint> constraints;
    for (std::size_t i = 0UZ; i < receivers.size(); ++i) {
        for (std::size_t j = i + 1UZ; j < receivers.size(); ++j) {
            const double d = distance(source, receivers[i]) - distance(source, receivers[j]) + (constraints.empty() ? error : 0.0);
            constraints.push_back(tdoaConstraint(receivers[i], receivers[j], static_cast<int>(i), static_cast<int>(j), d, 1e-9));
        }
    }
    return constraints;
}

[[nodiscard]] std::vector<Constraint> bistaticOf(const std::vector<Point3>& receivers, const Point3& transmitter, const Point3& object) {
    std::vector<Constraint> constraints;
    for (std::size_t i = 0UZ; i < receivers.size(); ++i) {
        const double path = distance(transmitter, object) + distance(object, receivers[i]);
        constraints.push_back(bistaticConstraint(transmitter, receivers[i], transmitterFocus(0UZ), static_cast<int>(i), path, 1e-9));
    }
    return constraints;
}

[[nodiscard]] double gap(const Point3& p, const Point3& q) { return std::hypot(p[0] - q[0], p[1] - q[1]); }

[[nodiscard]] gr::Tensor<double> positionsOf(const std::vector<Point3>& points) {
    std::vector<double> flat;
    for (const Point3& p : points) {
        flat.insert(flat.end(), p.begin(), p.end());
    }
    return gr::Tensor<double>(std::array{points.size(), 3UZ}, flat);
}

[[nodiscard]] std::string statusOf(const gr::property_map& event) {
    const auto meta = gr::property_map_view{event}.get_if<gr::property_map>(gr::tag::kTriggerMetaInfo.key());
    if (!meta) {
        return {};
    }
    const auto status = gr::property_map_view{*meta}.get_if<std::string_view>("status");
    return status ? std::string(*status) : std::string{};
}

[[nodiscard]] std::string metaText(const gr::property_map& event, std::string_view key) {
    const auto meta = gr::property_map_view{event}.get_if<gr::property_map>(gr::tag::kTriggerMetaInfo.key());
    const auto text = meta ? gr::property_map_view{*meta}.get_if<std::string_view>(key) : std::nullopt;
    return text ? std::string(*text) : std::string{};
}

[[nodiscard]] double metaValue(const gr::property_map& event, std::string_view key) {
    const auto meta  = gr::property_map_view{event}.get_if<gr::property_map>(gr::tag::kTriggerMetaInfo.key());
    const auto value = meta ? gr::property_map_view{*meta}.get_if<double>(key) : nullptr;
    return value != nullptr ? *value : std::numeric_limits<double>::quiet_NaN();
}

[[nodiscard]] std::vector<gr::property_map> arrivalsOf(const std::vector<Point3>& receivers, const Point3& source, double emission) {
    std::vector<gr::property_map> events;
    for (std::size_t m = 0UZ; m < receivers.size(); ++m) {
        events.push_back(datedEvent(std::to_string(m), emission + distance(source, receivers[m]) / kSpeed));
    }
    return events;
}

const boost::ut::suite<"delay constraints"> constraintTests = [] {
    using namespace boost::ut;

    "four receivers of the deck's 1.2 m square recover an in-plane source from exact TDOAs"_test = [] {
        for (const Point3 source : {Point3{0.3, 0.2, 0.0}, Point3{-1.1, 0.4, 0.0}, Point3{0.0, 0.0, 0.0}, Point3{1.5, -1.7, 0.0}}) {
            const Solution solution = solve(tdoaOf(deckSquare(), source), Settings{});
            expect(solution.status == Status::valid) << statusName(solution.status);
            expect(lt(gap(solution.mean, source), 1e-6)) << std::format("({}, {}) found as ({}, {})", source[0], source[1], solution.mean[0], solution.mean[1]);
            expect(lt(solution.rmsRangeResidual, 1e-9));
        }
    };

    "a reversed pair with the negated delay draws the same curve"_test = [] {
        const std::vector<Point3> r = deckSquare();
        const Point3              source{0.3, 0.2, 0.0};
        const double              d       = distance(source, r[0]) - distance(source, r[3]);
        const auto                ab      = sectionOf(tdoaConstraint(r[0], r[3], 0, 3, d, 1e-9), Settings{});
        const auto                ba      = sectionOf(tdoaConstraint(r[3], r[0], 3, 0, -d, 1e-9), Settings{});
        const auto                onCurve = [&](const std::vector<Point2>& curve) { return std::ranges::all_of(curve, [&](const Point2& p) { return std::isnan(p[0]) || std::abs(distance({p[0], p[1], 0.0}, r[0]) - distance({p[0], p[1], 0.0}, r[3]) - d) < 1e-9; }); };
        expect(gt(ab.size(), 10UZ) && gt(ba.size(), 10UZ));
        expect(onCurve(ab) && onCurve(ba)) << "both trace the branch with the measured sign";
    };

    "a zero TDOA is the perpendicular bisector"_test = [] {
        const Point3 a{-0.6, 0.0, 0.0}, b{0.6, 0.0, 0.0};
        const auto   curve = sectionOf(tdoaConstraint(a, b, 0, 1, 0.0, 1e-9), Settings{});
        expect(gt(curve.size(), 10UZ));
        expect(std::ranges::all_of(curve, [](const Point2& p) { return std::isnan(p[0]) || std::abs(p[0]) < 1e-9; }));
    };

    "two receivers give one hyperbola and no position"_test = [] {
        const std::vector<Point3> stereo{{-0.15, 0.0, 0.0}, {0.15, 0.0, 0.0}};
        const Solution            solution = solve(tdoaOf(stereo, {0.5, 1.0, 0.0}), Settings{});
        expect(solution.status == Status::insufficientData) << statusName(solution.status);
        expect(eq(solution.curves.size(), 1UZ) && gt(solution.curves.front().size(), 10UZ));
        expect(std::isnan(solution.mean[0]));
    };

    "a range difference beyond the baseline is refused, not clamped"_test = [] {
        const std::vector<Point3> r          = deckSquare();
        const Constraint          impossible = tdoaConstraint(r[0], r[1], 0, 1, 1.3, 1e-9);
        expect(!impossible.usable && impossible.refusal == "range difference beyond the baseline");
        const Solution solution = solve(std::vector{impossible}, Settings{});
        expect(solution.status == Status::invalidInput) << statusName(solution.status);
        expect(!tdoaConstraint(r[0], r[0], 0, 0, 0.0, 1e-9).usable) << "coincident receivers";
    };

    "three receivers on a line leave the mirror image: ambiguous"_test = [] {
        const std::vector<Point3> line{{-1.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}};
        const Solution            solution = solve(tdoaOf(line, {0.3, 0.6, 0.0}), Settings{});
        expect(solution.status == Status::ambiguous) << statusName(solution.status);
        expect(eq(solution.alternatives.size(), 2UZ));
        expect(std::ranges::any_of(solution.alternatives, [](const Point3& p) { return gap(p, {0.3, 0.6, 0.0}) < 1e-6; }) && std::ranges::any_of(solution.alternatives, [](const Point3& p) { return gap(p, {0.3, -0.6, 0.0}) < 1e-6; }));
        expect(std::isnan(solution.mean[0])) << "no detected position";
    };

    "bistatic ellipses of one transmitter and four receivers locate the reflector"_test = [] {
        const Point3   transmitter{0.0, -1.0, 0.0};
        const Point3   object{0.3, 0.2, 0.0};
        const Solution total = solve(bistaticOf(deckSquare(), transmitter, object), Settings{});
        expect(total.status == Status::valid) << statusName(total.status);
        expect(lt(gap(total.mean, object), 1e-6));
        expect(eq(total.supporting.size(), 4UZ));
    };

    "a transmitter at a receiver draws a circle of half the path"_test = [] {
        const Point3 r{0.4, -0.2, 0.0};
        const auto   curve = sectionOf(bistaticConstraint(r, r, transmitterFocus(0UZ), 0, 1.6, 1e-9), Settings{});
        expect(gt(curve.size(), 100UZ));
        expect(std::ranges::all_of(curve, [&](const Point2& p) { return std::isnan(p[0]) || std::abs(std::hypot(p[0] - r[0], p[1] - r[1]) - 0.8) < 1e-9; }));
    };

    "a bistatic path shorter than the direct one is refused"_test = [] {
        const Constraint refused = bistaticConstraint({0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}, transmitterFocus(0UZ), 0, 1.5, 1e-9);
        expect(!refused.usable && refused.refusal == "path shorter than the direct path");
        const Constraint segment = bistaticConstraint({0.0, -1.0, 0.0}, {0.0, 1.0, 0.0}, transmitterFocus(0UZ), 0, 2.0, 1e-9);
        expect(segment.usable && segment.degenerate) << "the path along the baseline is the segment between the foci";
    };

    "four microphones on a 9 m line, 3 m apart: the mirror image over a symmetric roi, the source over one side"_test = [] {
        const std::vector<Point3> line{{-4.5, 0.0, 0.0}, {-1.5, 0.0, 0.0}, {1.5, 0.0, 0.0}, {4.5, 0.0, 0.0}};
        const Point3              source{2.0, 4.0, 0.0};
        Settings                  both;
        both.roi                = {-8.0, 8.0, -8.0, 8.0};
        const Solution mirrored = solve(tdoaOf(line, source), both);
        expect(mirrored.status == Status::ambiguous) << statusName(mirrored.status);
        expect(std::ranges::any_of(mirrored.alternatives, [&](const Point3& p) { return gap(p, source) < 1e-6; }) && std::ranges::any_of(mirrored.alternatives, [](const Point3& p) { return gap(p, {2.0, -4.0, 0.0}) < 1e-6; }));
        Settings front;
        front.roi              = {-8.0, 8.0, 0.0, 8.0};
        const Solution located = solve(tdoaOf(line, source), front);
        expect(located.status == Status::valid) << statusName(located.status);
        expect(lt(gap(located.mean, source), 1e-6));
    };

    "an inconsistent delay is outvoted, gross errors leave no solution"_test = [] {
        const Solution outvoted = solve(tdoaOf(deckSquare(), {0.3, 0.2, 0.0}, 0.3), Settings{});
        expect(outvoted.status == Status::valid) << statusName(outvoted.status);
        expect(lt(gap(outvoted.mean, {0.3, 0.2, 0.0}), 1e-6));
        expect(!std::ranges::contains(outvoted.supporting, 0UZ)) << "the corrupted pair does not support the mean";

        std::vector<Constraint> scrambled = tdoaOf(deckSquare(), {0.3, 0.2, 0.0});
        constexpr std::array    kGrossErrors{0.25, 0.0, -0.25, 0.25, 0.0, -0.25}; // m, no point within 5 cm of all six curves
        for (std::size_t n = 0UZ; n < scrambled.size(); ++n) {
            scrambled[n].value += kGrossErrors[n];
        }
        const Solution scattered = solve(scrambled, Settings{});
        expect(std::ranges::none_of(scattered.intersections, &Intersection::accepted)) << "no candidate agrees with all receivers";
        expect(scattered.status == Status::noConsistentSolution) << statusName(scattered.status);
        expect(std::isnan(scattered.mean[0]));
    };

    "a distant source seen by a small array is poor geometry"_test = [] {
        const std::vector<Point3> small{{-0.02, -0.02, 0.0}, {-0.02, 0.02, 0.0}, {0.02, -0.02, 0.0}, {0.02, 0.02, 0.0}};
        Settings                  settings;
        settings.roi              = {-20.0, 20.0, -20.0, 20.0};
        settings.curveSegments    = 1024UZ;
        settings.maxRangeResidual = 1e-6;
        const Solution solution   = solve(tdoaOf(small, {15.0, 9.0, 0.0}), settings);
        expect(std::isnan(solution.mean[0])) << statusName(solution.status) << ": no position claimed";
    };

    "crossings narrower than min_crossing_angle are poor geometry, the point kept as the alternative"_test = [] {
        const Point3 source{0.3, 0.2, 0.0};
        Settings     strict;
        strict.minCrossingAngle = std::numbers::pi / 2.0 + 1e-3; // above any crossing angle
        const Solution solution = solve(tdoaOf(deckSquare(), source), strict);
        expect(solution.status == Status::poorGeometry) << statusName(solution.status);
        expect(std::isnan(solution.mean[0]) && eq(solution.alternatives.size(), 1UZ) && lt(gap(solution.alternatives.front(), source), 1e-6));
    };

    "a source on the extension of a receiver pair makes that pair's curve a ray: poor geometry"_test = [] {
        const Point3   source{-0.6, 1.5, 0.0}; // beyond receiver 1 on the line through receivers 0 and 1: |d| = baseline
        const auto     constraints = tdoaOf(deckSquare(), source);
        const Solution solution    = solve(constraints, Settings{});
        expect(constraints.front().degenerate) << "pair (0, 1)";
        expect(solution.status == Status::poorGeometry) << statusName(solution.status) << solution.diagnostics;
        expect(!solution.alternatives.empty() && lt(gap(solution.alternatives.front(), source), 1e-6));
    };

    "the detected mean does not depend on the curve resolution"_test = [] {
        Settings coarse;
        coarse.curveSegments = 64UZ;
        Settings fine;
        fine.curveSegments = 1024UZ;
        const Solution a   = solve(tdoaOf(deckSquare(), {0.45, -0.35, 0.0}), coarse);
        const Solution b   = solve(tdoaOf(deckSquare(), {0.45, -0.35, 0.0}), fine);
        expect(a.status == Status::valid && b.status == Status::valid);
        expect(lt(gap(a.mean, b.mean), 1e-9));
    };
};

const boost::ut::suite<"DelayLocator"> blockTests = [] {
    using namespace boost::ut;

    const auto run = [](std::vector<gr::property_map> events, gr::property_map settings, std::size_t results) {
        gr::Graph graph;
        auto&     script  = graph.emplaceBlock<gr::testing::EventScript>();
        auto&     locator = graph.emplaceBlock<DelayLocator>(std::move(settings));
        auto&     log     = graph.emplaceBlock<EventLog>();
        script._events    = std::move(events);
        log._stopAfter    = results;
        expect(graph.connect<"evtOut", "evtIn">(script, locator).has_value());
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        return std::pair{log._events, locator.n_unmatched.value};
    };
    const auto at = [](const gr::property_map& event, const Point3& p, double tolerance = 1e-6) { return std::hypot(metaValue(event, "x") - p[0], metaValue(event, "y") - p[1]) < tolerance; };

    "arrival events of two emissions give one result each, dated by the first arrival"_test = [&run, &at] {
        auto events = arrivalsOf(deckSquare(), {0.3, 0.2, 0.0}, 1.0);
        std::ranges::copy(arrivalsOf(deckSquare(), {-0.5, 0.9, 0.0}, 2.0), std::back_inserter(events));
        const auto [results, unmatched] = run(events, {{"element_positions", positionsOf(deckSquare())}, {"trigger_name", std::string("located")}}, 2UZ);
        expect(eq(results.size(), 2UZ)) << "one result per emission";
        if (results.size() == 2UZ) {
            expect(statusOf(results[0]) == "Valid" && statusOf(results[1]) == "Valid");
            expect(at(results[0], {0.3, 0.2, 0.0}) && at(results[1], {-0.5, 0.9, 0.0}));
            const auto time = gr::property_map_view{results[0]}.get_if<std::uint64_t>(gr::tag::kTriggerTime.key());
            expect(time != nullptr && *time >= 1'000'000'000U && *time < 1'010'000'000U);
        }
        expect(eq(unmatched, gr::Size_t{0U}));
    };

    "the sequential pairs 0-1, 1-2, 2-3 are the default; all and listed pairs locate the same source"_test = [&run, &at] {
        for (const auto& [pairs, list] : {std::pair{std::string("sequential"), std::vector<gr::Size_t>{}}, std::pair{std::string("all"), std::vector<gr::Size_t>{}}, std::pair{std::string("listed"), std::vector<gr::Size_t>{0U, 3U, 1U, 2U, 0U, 1U}}}) {
            const auto [results, _] = run(arrivalsOf(deckSquare(), {0.45, -0.35, 0.0}, 1.0), {{"element_positions", positionsOf(deckSquare())}, {"pairs", pairs}, {"pair_list", list}}, 1UZ);
            expect(eq(results.size(), 1UZ) && !results.empty() && at(results[0], {0.45, -0.35, 0.0})) << pairs;
        }
        expect(eq(pairsOf(PairSelection::sequential, {}, 4UZ).size(), 3UZ) && eq(pairsOf(PairSelection::all, {}, 4UZ).size(), 6UZ));
    };

    "a tetrahedron is cut at the height GR4's near-field solution gives, the source found in that slice"_test = [&run] {
        const gr::Tensor<double> tetrahedron = gr::algorithm::geometry::tetrahedron(0.6);
        std::vector<Point3>      corners     = pointsOf(tetrahedron);
        const Point3             source{0.25, -0.1, 0.15}; // determined: from (0.4, -0.3, 0.25) the four TDOAs have a second exact solution
        const auto [results, _] = run(arrivalsOf(corners, source, 1.0), {{"element_positions", tetrahedron}}, 1UZ);
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]);
            expect(lt(std::abs(metaValue(results[0], "z") - source[2]), 1e-6)) << "the slice height";
            expect(lt(std::hypot(metaValue(results[0], "x") - source[0], metaValue(results[0], "y") - source[1]), 1e-6)) << "the intersections in that slice";
        }
        std::vector<Constraint> constraints;
        for (std::size_t i = 0UZ; i < 4UZ; ++i) {
            for (std::size_t j = i + 1UZ; j < 4UZ; ++j) {
                constraints.push_back(tdoaConstraint(corners[i], corners[j], static_cast<int>(i), static_cast<int>(j), distance(source, corners[i]) - distance(source, corners[j]), 1e-9));
            }
        }
        for (const std::size_t segments : {64UZ, 1024UZ}) {
            Settings slice;
            slice.viewZ             = source[2];
            slice.curveSegments     = segments;
            const Solution solution = solve(constraints, slice);
            expect(solution.status == Status::valid && lt(gap(solution.mean, source), 1e-9)) << segments << " segments";
            for (const auto& curve : solution.curves) {
                const auto pieces = 1 + std::ranges::count_if(curve, [](const Point2& p) { return std::isnan(p[0]); });
                expect(gt(curve.size(), segments / 8UZ) && le(pieces, 2)) << "each section crosses the roi in at most two pieces";
            }
        }
    };

    "a re-trigger of one tetrahedron receiver before the others arrive leaves the slice height at its onset"_test = [&run] {
        const gr::Tensor<double> tetrahedron = gr::algorithm::geometry::tetrahedron(0.6);
        const Point3             source{0.25, -0.1, 0.15};
        auto                     events = arrivalsOf(pointsOf(tetrahedron), source, 1.0);
        events.insert(events.begin() + 1, datedEvent("0", 1.0 + distance(source, pointsOf(tetrahedron)[0]) / kSpeed + 0.5e-3));
        const auto [results, _] = run(events, {{"element_positions", tetrahedron}}, 1UZ);
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]);
            expect(lt(std::abs(metaValue(results[0], "z") - source[2]), 1e-6)) << metaValue(results[0], "z");
        }
    };

    "a missing channel disables only its pairs; a re-trigger within the window and unknown names are not used"_test = [&run, &at] {
        auto events = arrivalsOf(deckSquare(), {0.3, 0.2, 0.0}, 5.0);
        events.erase(events.begin() + 2);
        events.push_back(datedEvent("1", 5.0 + distance({0.3, 0.2, 0.0}, deckSquare()[1]) / kSpeed + 0.0005));
        events.push_back(datedEvent("nobody", 5.001));
        events.push_back(datedEvent("0", 9.0));
        const auto [results, unmatched] = run(events, {{"element_positions", positionsOf(deckSquare())}, {"pairs", std::string("all")}}, 1UZ);
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]);
            expect(at(results[0], {0.3, 0.2, 0.0}));
            expect(metaText(results[0], "diagnostics") == "no arrival from 2") << metaText(results[0], "diagnostics");
        }
        expect(eq(unmatched, gr::Size_t{1U}));
    };

    "a new geometry forgets the arrivals of the old one; a new view keeps them"_test = [] {
        DelayLocator locator({{"element_positions", positionsOf(deckSquare())}});
        locator.init(locator.progress);
        const auto change = [&locator](gr::property_map settings) {
            expect(locator.settings().set(std::move(settings)).empty());
            std::ignore = locator.settings().activateContext();
            locator.applyChangedSettings();
        };
        locator._recent.push_back(*datedOf(datedEvent("0", 1.0)));
        change({{"roi", std::vector{-1.0, 1.0, -1.0, 1.0}}});
        expect(eq(locator._recent.size(), 1UZ)) << "roi";
        change({{"receiver_names", std::vector<std::string>{"a", "b", "c", "d"}}});
        expect(locator._recent.empty()) << "receiver_names";
    };

    "arrivals delivered channel by channel never mix two emissions; the latest one is located"_test = [&run, &at] {
        std::vector<gr::property_map> events;
        const auto                    first  = arrivalsOf(deckSquare(), {0.3, 0.2, 0.0}, 1.0);
        const auto                    second = arrivalsOf(deckSquare(), {-0.5, 0.6, 0.0}, 1.02);
        for (std::size_t m = 0UZ; m < 4UZ; ++m) { // as triggers on separate channels deliver them, chunk by chunk
            events.push_back(first[m]);
            events.push_back(second[m]);
        }
        const auto [results, _] = run(events, {{"element_positions", positionsOf(deckSquare())}}, 1UZ);
        expect(ge(results.size(), 1UZ));
        for (const auto& result : results) {
            expect(at(result, {0.3, 0.2, 0.0}) || at(result, {-0.5, 0.6, 0.0})) << "a mixed result lies elsewhere";
        }
        expect(!results.empty() && at(results.back(), {-0.5, 0.6, 0.0}));
    };

    "arrival times near 2^62 ns keep their sub-microsecond differences"_test = [&run, &at] {
        std::vector<gr::property_map> events;
        for (std::size_t m = 0UZ; m < 4UZ; ++m) {
            const double travel = distance({0.3, 0.2, 0.0}, deckSquare()[m]) / kSpeed;
            const double whole  = std::floor(travel * 1e9);
            events.push_back({{std::string(gr::tag::kTriggerName.key()), std::to_string(m)}, {std::string(gr::tag::kTriggerTime.key()), (std::uint64_t{1} << 62U) + static_cast<std::uint64_t>(whole)}, {std::string(gr::tag::kTriggerOffset.key()), static_cast<float>(travel - whole * 1e-9)}});
        }
        const auto [results, _] = run(events, {{"element_positions", positionsOf(deckSquare())}}, 1UZ);
        expect(eq(results.size(), 1UZ) && !results.empty() && at(results[0], {0.3, 0.2, 0.0}));
    };

    "with direct_path_guard, a bistatic arrival on the direct path is skipped and the echo after it used"_test = [&run, &at] {
        const Point3                  transmitter{0.0, -1.0, 0.0};
        const Point3                  object{0.3, 0.2, 0.0};
        std::vector<gr::property_map> events{datedEvent("tx0", 2.0)};
        for (std::size_t m = 0UZ; m < 4UZ; ++m) {
            events.push_back(datedEvent(std::to_string(m), 2.0 + distance(transmitter, deckSquare()[m]) / kSpeed));
            events.push_back(datedEvent(std::to_string(m), 2.0 + (distance(transmitter, object) + distance(object, deckSquare()[m])) / kSpeed));
        }
        events.push_back(datedEvent("2", 1.9)); // before the emission: ignored
        const auto [results, _] = run(events, {{"mode", std::string("bistatic")}, {"element_positions", positionsOf(deckSquare())}, {"direct_path_guard", 5e-5}}, 1UZ);
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]);
            expect(at(results[0], object));
        }
    };

    "PointSourceArrivals on a clock feeds the locator its own source, TDOA and bistatic"_test = [&at] {
        for (const std::string mode : {"tdoa", "bistatic"}) {
            gr::Graph graph;
            auto&     clock    = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", 1000.f}, {"n_samples_max", gr::Size_t{2000U}}, {"chunk_size", gr::Size_t{100U}}, {"use_internal_thread", false}});
            auto&     arrivals = graph.emplaceBlock<PointSourceArrivals>({{"mode", mode}, {"source_position", std::vector{-0.4, 0.7, 0.0}}, {"period", 0.5}, {"n_events", gr::Size_t{3U}}});
            auto&     locator  = graph.emplaceBlock<DelayLocator>({{"mode", mode}});
            auto&     results  = graph.emplaceBlock<EventLog>();
            results._stopAfter = 3UZ;
            expect(graph.connect<"out", "clk">(clock, arrivals).has_value());
            expect(graph.connect<"evtOut", "evtIn">(arrivals, locator).has_value());
            expect(graph.connect<"evtOut", "evtIn">(locator, results).has_value());
            gr::scheduler::Simple sched;
            expect(sched.exchange(std::move(graph)).has_value());
            expect(sched.runAndWait().has_value());
            expect(eq(results._events.size(), 3UZ)) << mode;
            for (const auto& event : results._events) {
                expect(statusOf(event) == "Valid" && at(event, {-0.4, 0.7, 0.0})) << mode << statusOf(event);
            }
        }
    };
};

const boost::ut::suite<"PairDelayArrivals"> converterTests = [] {
    using namespace boost::ut;

    const auto pairDelays = [](const std::vector<Point3>& receivers, const Point3& source) {
        std::vector<float> rows;
        for (std::size_t p = 0UZ; p < gr::algorithm::pairsOf(receivers.size()); ++p) {
            const auto [i, j] = gr::algorithm::pairOf(p, receivers.size());
            rows.insert(rows.end(), {static_cast<float>((distance(source, receivers[i]) - distance(source, receivers[j])) / kSpeed), 1.f, 10.f});
        }
        return rows;
    };
    const auto convert = [](std::vector<gr::testing::TensorSource<float>::Frame> frames, gr::property_map firstTag, gr::property_map settings, gr::property_map locatorSettings, std::size_t results) {
        gr::Graph graph;
        auto&     source    = graph.emplaceBlock<gr::testing::TensorSource<float>>();
        auto&     converter = graph.emplaceBlock<PairDelayArrivals<float>>(std::move(settings));
        auto&     locator   = graph.emplaceBlock<DelayLocator>(std::move(locatorSettings));
        auto&     log       = graph.emplaceBlock<EventLog>();
        source._frames      = std::move(frames);
        source._firstTag    = std::move(firstTag);
        log._stopAfter      = results;
        expect(graph.connect<"out", "in">(source, converter).has_value());
        expect(graph.connect<"evtOut", "evtIn">(converter, locator).has_value());
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        return std::tuple{log._events, converter.n_frames.value, converter.n_refused.value, converter.n_gated.value};
    };
    const gr::property_map dated{{std::string(gr::tag::kTriggerTime.key()), std::uint64_t{3'000'000'000U}}};

    "a 16-channel PairDelay frame of 120 pair delays becomes 16 arrivals that locate the source"_test = [&] {
        const gr::Tensor<double> uma16 = gr::algorithm::geometry::uma16();
        const Point3             object{0.12, -0.08, 0.0};
        const auto [results, frames, refused, gated] = convert({{.extents = {120UZ, 3UZ}, .elements = pairDelays(pointsOf(uma16), object)}}, dated, {}, {{"element_positions", uma16}, {"roi", std::vector{-1.0, 1.0, -1.0, 1.0}}, {"max_range_residual", 0.002}}, 1UZ);
        expect(eq(frames, gr::Size_t{1U}) && eq(refused, gr::Size_t{0U}));
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]);
            expect(lt(std::hypot(metaValue(results[0], "x") - object[0], metaValue(results[0], "y") - object[1]), 1e-3)) << "float delays over a 13 cm aperture";
            expect(eq(*gr::property_map_view{results[0]}.get_if<std::uint64_t>(gr::tag::kTriggerTime.key()) / 1'000'000'000U, std::uint64_t{2U})) << "dated by the frame's gr:trigger_time, earlier receivers before it";
        }
    };

    "a frame whose rows are no pair count, or that is undated, is refused and counted"_test = [&] {
        const auto [noPairs, framesA, refusedA, gatedA] = convert({{.extents = {5UZ, 3UZ}, .elements = std::vector<float>(15UZ, 0.f)}}, dated, {}, {}, 0UZ);
        expect(eq(framesA, gr::Size_t{0U}) && eq(refusedA, gr::Size_t{1U}));
        const auto [undated, framesB, refusedB, gatedB] = convert({{.extents = {6UZ, 3UZ}, .elements = pairDelays(deckSquare(), {0.3, 0.2, 0.0})}}, {}, {}, {}, 0UZ);
        expect(eq(framesB, gr::Size_t{0U}) && eq(refusedB, gr::Size_t{1U})) << "no gr:trigger_time and no frame_period";
    };

    "with frame_period 0 a frame without gr:trigger_time is refused, not dated by the previous one"_test = [&] {
        const auto rows                              = pairDelays(deckSquare(), {0.3, 0.2, 0.0});
        const auto [results, frames, refused, gated] = convert({{.extents = {6UZ, 3UZ}, .elements = rows}, {.extents = {6UZ, 3UZ}, .elements = rows}}, dated, {}, {}, 1UZ);
        expect(eq(frames, gr::Size_t{1U}) && eq(refused, gr::Size_t{1U}));
    };

    "frames below min_confidence are gated"_test = [&] {
        auto rows                                    = pairDelays(deckSquare(), {0.3, 0.2, 0.0});
        rows[2]                                      = 1.2f; // pair (0, 1): its peak barely beats its second peak
        const auto [results, frames, refused, gated] = convert({{.extents = {6UZ, 3UZ}, .elements = rows}}, dated, {{"min_confidence", 2.0}}, {}, 0UZ);
        expect(eq(frames, gr::Size_t{0U}) && eq(gated, gr::Size_t{1U}));
    };

    "only the pairs (0, j) that date the arrivals gate: a poor pair (1, 2) does not"_test = [&] {
        auto rows                                    = pairDelays(deckSquare(), {0.3, 0.2, 0.0});
        rows[3UZ * 3UZ + 2UZ]                        = 1.f; // pair (1, 2)
        const auto [results, frames, refused, gated] = convert({{.extents = {6UZ, 3UZ}, .elements = rows}}, dated, {{"min_confidence", 2.0}}, {}, 1UZ);
        expect(eq(frames, gr::Size_t{1U}) && eq(gated, gr::Size_t{0U}));
    };

    "receivers beyond receiver_names are unused inputs: no arrival, and their silent pairs do not gate"_test = [&] {
        const Point3 object{0.3, 0.2, 0.0};
        auto         rows = pairDelays(deckSquare(), object);
        for (const std::size_t p : {2UZ, 4UZ, 5UZ}) { // pairs (0, 3), (1, 3), (2, 3)
            rows[p * 3UZ + 2UZ] = 1.f;
        }
        const auto         square = deckSquare();
        gr::Tensor<double> three({3UZ, 3UZ}, std::vector{square[0][0], square[0][1], square[0][2], square[1][0], square[1][1], square[1][2], square[2][0], square[2][1], square[2][2]});
        const auto [results, frames, refused, gated] = convert({{.extents = {6UZ, 3UZ}, .elements = rows}}, dated, {{"min_confidence", 2.0}, {"receiver_names", std::vector<std::string>{"0", "1", "2"}}}, {{"element_positions", three}}, 1UZ);
        expect(eq(frames, gr::Size_t{1U}) && eq(gated, gr::Size_t{0U}));
        expect(eq(results.size(), 1UZ));
        if (!results.empty()) {
            expect(statusOf(results[0]) == "Valid") << statusOf(results[0]) << metaText(results[0], "diagnostics");
            expect(lt(std::hypot(metaValue(results[0], "x") - object[0], metaValue(results[0], "y") - object[1]), 1e-3));
        }
    };
};

const boost::ut::suite<"PointSourceAudio"> audioTests = [] {
    using namespace boost::ut;

    "four microphones record clicks that LevelTriggers date; the locator finds the source within 1 cm"_test = [] {
        using LevelTrigger = gr::blocks::trigger::ValueTrigger<float, gr::blocks::trigger::ValueCondition::level>;
        gr::Graph graph;
        auto&     clock   = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", 48000.f}, {"n_samples_max", gr::Size_t{48000U}}, {"chunk_size", gr::Size_t{1024U}}, {"use_internal_thread", false}, {"start_time", std::uint64_t{1'000'000'000U}}});
        auto&     audio   = graph.emplaceBlock<PointSourceAudio>({{"source_position", std::vector{0.3, 0.2, 0.0}}, {"period", 0.25}, {"sample_rate", 48000.f}});
        auto&     locator = graph.emplaceBlock<DelayLocator>();
        auto&     log     = graph.emplaceBlock<EventLog>();
        log._stopAfter    = 3UZ;
        expect(eq(audio.out.size(), 4UZ)) << fatal << "one output per element_positions row";
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        for (std::size_t m = 0UZ; m < 4UZ; ++m) {
            auto& trigger = graph.emplaceBlock<LevelTrigger>({{"threshold", 0.4f}, {"hysteresis", 0.1f}, {"edge", std::string("rising")}, {"trigger_name", std::to_string(m)}});
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), trigger, gr::PortDefinition("in")).has_value());
            expect(graph.connect<"evtOut", "evtIn">(trigger, locator).has_value());
        }
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(ge(log._events.size(), 3UZ));
        for (const auto& event : log._events) {
            expect(statusOf(event) == "Valid") << statusOf(event) << metaText(event, "diagnostics");
            expect(lt(std::hypot(metaValue(event, "x") - 0.3, metaValue(event, "y") - 0.2), 0.01)) << std::format("({}, {})", metaValue(event, "x"), metaValue(event, "y"));
        }
    };

    "n_outputs fixes the ports: outputs without a microphone are silent, a new geometry keeps them"_test = [] {
        gr::Graph graph;
        auto&     clock = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", 48000.f}, {"n_samples_max", gr::Size_t{48000U}}, {"chunk_size", gr::Size_t{1000U}}, {"use_internal_thread", false}});
        auto&     audio = graph.emplaceBlock<PointSourceAudio>({{"sample_rate", 48000.f}, {"n_outputs", gr::Size_t{4U}}, {"element_positions", gr::Tensor<double>({2UZ, 3UZ}, std::vector{-0.15, 0.0, 0.0, 0.15, 0.0, 0.0})}});
        expect(eq(audio.out.size(), 4UZ));
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        std::vector<gr::testing::TagSink<float, gr::testing::ProcessFunction::USE_PROCESS_BULK>*> sinks;
        for (std::size_t m = 0UZ; m < audio.out.size(); ++m) {
            sinks.push_back(&graph.emplaceBlock<gr::testing::TagSink<float, gr::testing::ProcessFunction::USE_PROCESS_BULK>>());
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), *sinks.back(), gr::PortDefinition("in")).has_value());
        }
        expect(audio.settings().set({{"element_positions", gr::Tensor<double>({4UZ, 3UZ}, std::vector(12UZ, 0.0))}}).empty());
        std::ignore = audio.settings().applyStagedParameters();
        expect(eq(audio.out.size(), 4UZ)) << "a connected port count does not follow the geometry";
        std::ignore = audio.settings().set({{"element_positions", gr::Tensor<double>({2UZ, 3UZ}, std::vector{-0.15, 0.0, 0.0, 0.15, 0.0, 0.0})}});
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        for (std::size_t m = 0UZ; m < sinks.size(); ++m) {
            const auto& samples = sinks[m]->_samples;
            expect(eq(samples.size(), 48000UZ)) << m;
            expect(eq(std::ranges::max(samples | std::views::transform([](float v) { return std::abs(v); })) > 0.5f, m < 2UZ)) << m;
        }
    };

    "every output repeats its time anchor each tag_interval, for a reader that joins later"_test = [] {
        gr::Graph graph;
        auto&     clock = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", 48000.f}, {"n_samples_max", gr::Size_t{120000U}}, {"chunk_size", gr::Size_t{1000U}}, {"use_internal_thread", false}, {"start_time", std::uint64_t{1'000'000'000U}}});
        auto&     audio = graph.emplaceBlock<PointSourceAudio>({{"sample_rate", 48000.f}, {"tag_interval", 1.0f}});
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        std::vector<gr::testing::TagSink<float, gr::testing::ProcessFunction::USE_PROCESS_BULK>*> sinks;
        for (std::size_t m = 0UZ; m < audio.out.size(); ++m) {
            sinks.push_back(&graph.emplaceBlock<gr::testing::TagSink<float, gr::testing::ProcessFunction::USE_PROCESS_BULK>>({{"log_tags", true}}));
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), *sinks.back(), gr::PortDefinition("in")).has_value());
        }
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        for (const auto* sink : sinks) {
            std::vector<std::pair<std::size_t, std::uint64_t>> anchors;
            for (const auto& tag : sink->_tags) {
                if (const auto* time = gr::property_map_view{tag.map}.get_if<std::uint64_t>(gr::tag::kTriggerTime.key()); time != nullptr) {
                    anchors.emplace_back(tag.index, *time);
                }
            }
            std::ranges::sort(anchors);
            const auto [first, last] = std::ranges::unique(anchors); // the clock's own anchor at sample 0 is forwarded too
            anchors.erase(first, last);
            expect(anchors == std::vector<std::pair<std::size_t, std::uint64_t>>{{0UZ, 1'000'000'000U}, {48000UZ, 2'000'000'000U}, {96000UZ, 3'000'000'000U}}) << std::format("{}", anchors);
        }
    };
};

const boost::ut::suite<"DataSetToTensor"> dataSetToTensorTests = [] {
    using namespace boost::ut;

    const auto windowOf = [](std::size_t S, std::size_t N, std::int64_t timestamp, double rateHz) {
        gr::DataSet<float> set;
        set.timestamp = timestamp;
        set.axis_values.resize(1UZ);
        for (std::size_t i = 0UZ; i < N; ++i) {
            set.axis_values[0].push_back(static_cast<float>((static_cast<double>(i) - 2.0) / rateHz));
        }
        set.extents.push_back(static_cast<std::int32_t>(N));
        for (std::size_t s = 0UZ; s < S; ++s) {
            set.signal_names.emplace_back(std::format("ch{}", s));
            for (std::size_t i = 0UZ; i < N; ++i) {
                set.signal_values.push_back(static_cast<float>(10U * s + i));
            }
        }
        return set;
    };
    const auto convert = [](std::vector<gr::DataSet<float>> sets) {
        gr::Graph graph;
        auto&     source    = graph.emplaceBlock<DataSetSource>();
        auto&     converter = graph.emplaceBlock<DataSetToTensor<float>>();
        auto&     sink      = graph.emplaceBlock<gr::testing::TensorCollector<float>>();
        source._sets        = std::move(sets);
        expect(graph.connect<"out", "in">(source, converter).has_value());
        expect(graph.connect<"out", "in">(converter, sink).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        return std::tuple{sink._shapes, sink._elements, sink._tags, converter.n_refused.value};
    };

    "a DataSet of 3 signals x 5 samples becomes one [3,5] tensor, signal s in row s, dated by its timestamp, its rate from the axis"_test = [&] {
        const auto [shapes, elements, tags, refused] = convert({windowOf(3UZ, 5UZ, 7'000'000'000, 1000.0)});
        expect(eq(refused, gr::Size_t{0U}));
        expect(eq(shapes.size(), 1UZ)) << fatal;
        expect(shapes[0] == std::vector<std::size_t>{3UZ, 5UZ});
        expect(elements[0] == std::vector<float>{0.f, 1.f, 2.f, 3.f, 4.f, 10.f, 11.f, 12.f, 13.f, 14.f, 20.f, 21.f, 22.f, 23.f, 24.f});
        const auto dated = std::ranges::find_if(tags, [](const auto& tag) { return tag.second.contains(std::string(gr::tag::kTriggerTime.key())); });
        expect(dated != tags.end()) << fatal;
        expect(eq(dated->first, 0UZ));
        expect(eq(*gr::property_map_view{dated->second}.get_if<std::uint64_t>(gr::tag::kTriggerTime.key()), std::uint64_t{7'000'000'000U}));
        expect(approx(*gr::property_map_view{dated->second}.get_if<float>(gr::tag::kSampleRate.key()), 1000.f, 1e-2f));
    };

    "a DataSet whose values are not S x N, or without a time axis, is refused and counted; the next one passes"_test = [&] {
        auto uneven = windowOf(2UZ, 4UZ, 1, 100.0);
        uneven.signal_values.pop_back();
        auto timeless = windowOf(2UZ, 4UZ, 1, 100.0);
        timeless.axis_values.clear();
        const auto [shapes, elements, tags, refused] = convert({uneven, timeless, windowOf(2UZ, 4UZ, 1, 100.0)});
        expect(eq(refused, gr::Size_t{2U}));
        expect(eq(shapes.size(), 1UZ));
    };

    "simulated clicks, cut by one channel's trigger from all four, correlated pair by pair (GCC-PHAT) and located at 96 kHz, land within 1 cm"_test = [] {
        using LevelTrigger           = gr::blocks::trigger::ValueTrigger<float, gr::blocks::trigger::ValueCondition::level>;
        constexpr float      kRate   = 96000.f;
        constexpr gr::Size_t kMaxLag = 500U; // the 1.2 m square's diagonal: 4.9 ms, 475 samples
        const Point3         source{0.3, 0.2, 0.0};
        gr::Graph            graph;
        auto&                clock       = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", kRate}, {"n_samples_max", gr::Size_t{96000U}}, {"chunk_size", gr::Size_t{1024U}}, {"use_internal_thread", false}, {"start_time", std::uint64_t{1'000'000'000U}}});
        auto&                audio       = graph.emplaceBlock<PointSourceAudio>({{"source_position", std::vector{source[0], source[1], source[2]}}, {"period", 0.25}, {"sample_rate", kRate}, {"noise", 0.02f}});
        auto&                trigger     = graph.emplaceBlock<LevelTrigger>({{"threshold", 0.4f}, {"hysteresis", 0.1f}, {"edge", std::string("rising")}, {"trigger_name", std::string("0")}});
        auto&                recorder    = graph.emplaceBlock<gr::blocks::trigger::MultiChannelRecorder<float>>({{"n_inputs", gr::Size_t{4U}}, {"n_pre", gr::Size_t{1440U}}, {"n_post", gr::Size_t{2400U}}, {"retrigger", std::string("ignore")}, {"holdoff", 0.1f}, {"emit_partial", false}});
        auto&                frames      = graph.emplaceBlock<DataSetToTensor<float>>();
        auto&                correlation = graph.emplaceBlock<gr::localisation::PairCrossCorrelation<float>>({{"max_lag", kMaxLag}, {"f_min", 300.f}, {"f_max", 12000.f}, {"weighting", "none"}}); // the clicks' band, not 48 kHz of noise
        auto&                delays      = graph.emplaceBlock<gr::localisation::PairDelay<float>>({{"max_lag", kMaxLag}});
        auto&                arrivals    = graph.emplaceBlock<PairDelayArrivals<float>>();
        auto&                locator     = graph.emplaceBlock<DelayLocator>();
        auto&                log         = graph.emplaceBlock<EventLog>();
        log._stopAfter                   = 3UZ;
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        expect(graph.connect(audio, gr::PortDefinition(0UZ, 0UZ), trigger, gr::PortDefinition("in")).has_value());
        expect(graph.connect(trigger, gr::PortDefinition("out"), recorder, gr::PortDefinition(1UZ, 0UZ)).has_value());
        for (std::size_t m = 1UZ; m < 4UZ; ++m) {
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), recorder, gr::PortDefinition(1UZ, m)).has_value());
        }
        expect(graph.connect(trigger, gr::PortDefinition("evtOut"), recorder, gr::PortDefinition("evtIn")).has_value());
        expect(graph.connect(recorder, gr::PortDefinition("out"), frames, gr::PortDefinition("in")).has_value());
        expect(graph.connect<"out", "in">(frames, correlation).has_value());
        expect(graph.connect<"out", "in">(correlation, delays).has_value());
        expect(graph.connect<"out", "in">(delays, arrivals).has_value());
        expect(graph.connect<"evtOut", "evtIn">(arrivals, locator).has_value());
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        expect(ge(log._events.size(), 3UZ)) << std::format("recorded {}, framed {}, refused {}", recorder.n_recorded.value, arrivals.n_frames.value, arrivals.n_refused.value);
        for (const auto& event : log._events) {
            expect(statusOf(event) == "Valid") << statusOf(event) << metaText(event, "diagnostics");
            expect(lt(std::hypot(metaValue(event, "x") - source[0], metaValue(event, "y") - source[1]), 0.01)) << std::format("({}, {})", metaValue(event, "x"), metaValue(event, "y"));
        }
    };

    "the dashboards' clap chain (high-pass, LevelTrigger, Throttle on every channel) records every click and locates it"_test = [] {
        using LevelTrigger           = gr::blocks::trigger::ValueTrigger<float, gr::blocks::trigger::ValueCondition::level>;
        constexpr float      kRate   = 96000.f;
        constexpr gr::Size_t kMaxLag = 500U;
        constexpr gr::Size_t kClicks = 5U; // one every 0.5 s in 2.7 s, less the one at sample 0, which has no samples before it
        const Point3         source{0.3, 0.2, 0.0};
        gr::Graph            graph;
        auto&                clock       = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", kRate}, {"n_samples_max", gr::Size_t{259200U}}, {"chunk_size", gr::Size_t{1024U}}, {"use_internal_thread", false}, {"start_time", std::uint64_t{1'000'000'000U}}});
        auto&                audio       = graph.emplaceBlock<PointSourceAudio>({{"source_position", std::vector{source[0], source[1], source[2]}}, {"period", 0.5}, {"sample_rate", kRate}, {"noise", 0.02f}});
        auto&                recorder    = graph.emplaceBlock<gr::blocks::trigger::MultiChannelRecorder<float>>({{"n_inputs", gr::Size_t{4U}}, {"n_pre", gr::Size_t{kMaxLag + 480U}}, {"n_post", gr::Size_t{kMaxLag + 480U}}, {"retrigger", std::string("ignore")}, {"holdoff", 0.2f}, {"emit_partial", false}, {"history", 0.5f}});
        auto&                frames      = graph.emplaceBlock<DataSetToTensor<float>>();
        auto&                correlation = graph.emplaceBlock<gr::localisation::PairCrossCorrelation<float>>({{"max_lag", kMaxLag}, {"f_min", 300.f}, {"f_max", 12000.f}, {"weighting", "none"}});
        auto&                delays      = graph.emplaceBlock<gr::localisation::PairDelay<float>>({{"max_lag", kMaxLag}});
        auto&                arrivals    = graph.emplaceBlock<PairDelayArrivals<float>>({{"min_confidence", 2.0}});
        auto&                locator     = graph.emplaceBlock<DelayLocator>();
        auto&                log         = graph.emplaceBlock<EventLog>();
        log._stopAfter                   = kClicks;
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        for (std::size_t m = 0UZ; m < 4UZ; ++m) {
            auto& highPass = graph.emplaceBlock<gr::filter::BasicFilter<float>>({{"filter_type", "IIR"}, {"filter_response", "HIGHPASS"}, {"iir_design_method", "BUTTERWORTH"}, {"filter_order", gr::Size_t{2U}}, {"f_high", 200.f}, {"sample_rate", kRate}});
            auto& trigger  = graph.emplaceBlock<LevelTrigger>({{"threshold", 0.2f}, {"hysteresis", 0.1f}, {"edge", std::string("rising")}, {"trigger_name", std::to_string(m)}});
            auto& throttle = graph.emplaceBlock<gr::blocks::trigger::Throttle<float>>({{"n_samples", gr::Size_t{19200U}}});
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), highPass, gr::PortDefinition("in")).has_value());
            expect(graph.connect<"out", "in">(highPass, trigger).has_value());
            expect(graph.connect<"out", "in">(trigger, throttle).has_value());
            expect(graph.connect(throttle, gr::PortDefinition("evtOut"), recorder, gr::PortDefinition("evtIn")).has_value());
            expect(graph.connect(highPass, gr::PortDefinition("out"), recorder, gr::PortDefinition(1UZ, m)).has_value());
        }
        expect(graph.connect(recorder, gr::PortDefinition("out"), frames, gr::PortDefinition("in")).has_value());
        expect(graph.connect<"out", "in">(frames, correlation).has_value());
        expect(graph.connect<"out", "in">(correlation, delays).has_value());
        expect(graph.connect<"out", "in">(delays, arrivals).has_value());
        expect(graph.connect<"evtOut", "evtIn">(arrivals, locator).has_value());
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        const std::string counters = std::format("recorded {}, suppressed {}, undated {}, unplaceable {}, lost {}; framed {}, gated {}", recorder.n_recorded.value, recorder.n_suppressed.value, recorder.n_undated.value, recorder.n_unplaceable.value, recorder.n_lost.value, arrivals.n_frames.value, arrivals.n_gated.value);
        expect(eq(recorder.n_undated.value, gr::Size_t{0U}) && eq(recorder.n_unplaceable.value, gr::Size_t{0U})) << counters;
        expect(eq(recorder.n_lost.value, gr::Size_t{1U})) << counters << ": only the click at sample 0";
        expect(eq(recorder.n_recorded.value, kClicks)) << counters;
        expect(eq(log._events.size(), static_cast<std::size_t>(kClicks))) << counters;
        for (const auto& event : log._events) {
            expect(statusOf(event) == "Valid") << statusOf(event) << metaText(event, "diagnostics");
            expect(lt(std::hypot(metaValue(event, "x") - source[0], metaValue(event, "y") - source[1]), 0.01)) << std::format("({}, {})", metaValue(event, "x"), metaValue(event, "y"));
        }
    };

    struct Continuous {
        float               amplitude = 0.8f;
        std::size_t         results   = 10UZ;
        gr::Tensor<double>  positions = kDeckSquare;
        std::vector<double> source{0.3, 0.2, 0.0};
        std::vector<double> roi{-2.0, 2.0, -2.0, 2.0};
        double              period        = 0.05;
        double              pulseWidth    = 0.5e-3;
        double              toneFrequency = 0.0;
        float               fMin          = 300.f;
        float               fMax          = 12000.f;
        gr::Size_t          maxLag        = 500U;   // samples
        gr::Size_t          frame         = 4800U;  // samples, 50 ms: more than four maximum lags
        gr::Size_t          duration      = 96000U; // samples
        double              minConfidence = 3.0;
        std::string         weighting     = "phat";
        float               rate          = 96000.f; // Hz
    };
    const auto continuous = [](Continuous c) {
        const float      kRate   = c.rate;
        const gr::Size_t kFrame  = c.frame;
        const gr::Size_t kMaxLag = c.maxLag;
        gr::Graph        graph;
        auto&            clock       = graph.emplaceBlock<gr::basic::ClockSource<std::uint8_t>>({{"sample_rate", kRate}, {"n_samples_max", c.duration}, {"chunk_size", gr::Size_t{1024U}}, {"use_internal_thread", false}, {"start_time", std::uint64_t{1'000'000'000U}}});
        auto&            audio       = graph.emplaceBlock<PointSourceAudio>({{"element_positions", c.positions}, {"source_position", c.source}, {"period", c.period}, {"pulse_width", c.pulseWidth}, {"tone_frequency", c.toneFrequency}, {"sample_rate", kRate}, {"amplitude", c.amplitude}, {"noise", 0.02f}});
        auto&            frames      = graph.emplaceBlock<gr::basic::StreamsToTensor<float>>({{"n_ports", gr::Size_t{4U}}, {"size", kFrame}});
        auto&            correlation = graph.emplaceBlock<gr::localisation::PairCrossCorrelation<float>>({{"max_lag", kMaxLag}, {"f_min", c.fMin}, {"f_max", c.fMax}, {"weighting", c.weighting}});
        auto&            delays      = graph.emplaceBlock<gr::localisation::PairDelay<float>>({{"max_lag", kMaxLag}});
        auto&            arrivals    = graph.emplaceBlock<PairDelayArrivals<float>>({{"min_confidence", c.minConfidence}, {"frame_period", static_cast<double>(kFrame) / static_cast<double>(kRate)}});
        auto&            locator     = graph.emplaceBlock<DelayLocator>({{"element_positions", c.positions}, {"roi", c.roi}});
        auto&            log         = graph.emplaceBlock<EventLog>();
        log._stopAfter               = std::max(c.results, 1UZ);
        expect(graph.connect<"out", "clk">(clock, audio).has_value());
        for (std::size_t m = 0UZ; m < 4UZ; ++m) {
            auto& highPass = graph.emplaceBlock<gr::filter::BasicFilter<float>>({{"filter_type", "IIR"}, {"filter_response", "HIGHPASS"}, {"iir_design_method", "BUTTERWORTH"}, {"filter_order", gr::Size_t{2U}}, {"f_high", 200.f}, {"sample_rate", kRate}});
            expect(graph.connect(audio, gr::PortDefinition(0UZ, m), highPass, gr::PortDefinition("in")).has_value());
            expect(graph.connect(highPass, gr::PortDefinition("out"), frames, gr::PortDefinition(1UZ, m)).has_value());
        }
        expect(graph.connect(frames, gr::PortDefinition("out"), correlation, gr::PortDefinition("in")).has_value());
        expect(graph.connect<"out", "in">(correlation, delays).has_value());
        expect(graph.connect<"out", "in">(delays, arrivals).has_value());
        expect(graph.connect<"evtOut", "evtIn">(arrivals, locator).has_value());
        expect(graph.connect<"evtOut", "evtIn">(locator, log).has_value());
        gr::scheduler::Simple sched;
        expect(sched.exchange(std::move(graph)).has_value());
        expect(sched.runAndWait().has_value());
        return std::tuple{log._events, arrivals.n_frames.value, arrivals.n_gated.value};
    };

    "a shaker-like source (20 clicks/s), framed back to back every 50 ms and correlated (PHAT) over the clicks' band, is located frame by frame within 1 cm"_test = [&continuous] {
        const auto [results, framed, gated] = continuous({.fMax = 4000.f}); // a 0.5 ms raised cosine holds little above 4 kHz, where PHAT would weigh noise alone
        expect(ge(results.size(), 10UZ)) << std::format("framed {}, gated {}", framed, gated);
        for (const auto& event : results) {
            expect(statusOf(event) == "Valid") << statusOf(event) << metaText(event, "diagnostics");
            expect(lt(std::hypot(metaValue(event, "x") - 0.3, metaValue(event, "y") - 0.2), 0.01)) << std::format("({}, {})", metaValue(event, "x"), metaValue(event, "y"));
        }
    };

    "frames of noise alone stay below min_confidence: gated, none located"_test = [&continuous] {
        const auto [results, framed, gated] = continuous({.amplitude = 0.f, .results = 0UZ});
        expect(results.empty()) << std::format("framed {}, gated {}", framed, gated);
        expect(ge(gated, gr::Size_t{15U})) << "every 50 ms frame of the second";
    };

    "every dashboard's continuous layer, unweighted with its generated frame and lags, locates clicks every 0.5 s within 1 cm, the 0.3 m line within 2 cm"_test = [&continuous] {
        const auto               line = [](double spacing) { return gr::Tensor<double>({4UZ, 3UZ}, std::vector{-1.5 * spacing, 0.0, 0.0, -0.5 * spacing, 0.0, 0.0, 0.5 * spacing, 0.0, 0.0, 1.5 * spacing, 0.0, 0.0}); };
        const gr::Tensor<double> tetrahedron({4UZ, 3UZ}, std::vector{-0.15, -0.086603, 0.061237, 0.15, -0.086603, 0.061237, 0.0, 0.173205, 0.061237, 0.0, 0.0, -0.183712});
        const gr::Tensor<double> uma16Corners({4UZ, 3UZ}, std::vector{-0.063, -0.063, 0.0, 0.063, -0.063, 0.0, -0.063, 0.063, 0.0, 0.063, 0.063, 0.0});
        struct Dashboard {
            std::string_view name;
            Continuous       layer;
            double           tolerance = 0.01; // m
        };
        constexpr double kCollinearTolerance = 0.02; // m: at 48 kHz a sample is 7 mm of path, which a collinear 0.3 m array stretches in range
        for (const auto& [name, layer, tolerance] : {Dashboard{"square, 96 kHz", {.maxLag = 483U, .frame = 4800U}}, Dashboard{"tetrahedron, 96 kHz", {.positions = tetrahedron, .source = {0.1, -0.05, 0.05}, .roi = {-1.0, 1.0, -1.0, 1.0}, .maxLag = 92U, .frame = 4800U}}, Dashboard{"3 m line, 96 kHz", {.positions = line(3.0), .source = {1.0, 4.0, 0.0}, .roi = {-8.0, 8.0, 0.0, 8.0}, .maxLag = 2527U, .frame = 10240U}}, Dashboard{"3 m line, 48 kHz", {.positions = line(3.0), .source = {1.0, 4.0, 0.0}, .roi = {-8.0, 8.0, 0.0, 8.0}, .maxLag = 1268U, .frame = 5120U, .rate = 48000.f}}, Dashboard{"0.3 m line, 48 kHz", {.positions = line(0.1), .source = {0.1, 0.3, 0.0}, .roi = {-1.0, 1.0, 0.0, 1.5}, .maxLag = 50U, .frame = 5120U, .rate = 48000.f}, kCollinearTolerance}, Dashboard{"UMA16 corners, 48 kHz", {.positions = uma16Corners, .source = {0.15, 0.1, 0.0}, .roi = {-0.5, 0.5, -0.5, 0.5}, .maxLag = 33U, .frame = 2560U, .rate = 48000.f}}}) {
            Continuous c                        = layer;
            c.results                           = 3UZ;
            c.period                            = 0.5;
            c.weighting                         = "none";
            c.duration                          = static_cast<gr::Size_t>(2.f * c.rate);
            const auto [results, framed, gated] = continuous(c);
            expect(ge(results.size(), 3UZ)) << name << std::format(": framed {}, gated {}", framed, gated);
            for (const auto& event : results) {
                expect(statusOf(event) == "Valid") << name << statusOf(event) << metaText(event, "diagnostics");
                expect(lt(std::hypot(metaValue(event, "x") - c.source[0], metaValue(event, "y") - c.source[1]), tolerance)) << name << std::format(" ({}, {})", metaValue(event, "x"), metaValue(event, "y"));
            }
        }
    };

    "a 440 Hz tuning fork struck every 0.5 s before a 0.3 m line, correlated in 400-480 Hz, is located from the frames inside its tone"_test = [&continuous] {
        const auto [results, framed, gated] = continuous({.results = 5UZ, .positions = gr::Tensor<double>({4UZ, 3UZ}, std::vector{-0.15, 0.0, 0.0, -0.05, 0.0, 0.0, 0.05, 0.0, 0.0, 0.15, 0.0, 0.0}), .source = {0.1, 0.3, 0.0}, .roi = {-1.0, 1.0, 0.0, 1.5}, .period = 0.5, .pulseWidth = 0.4, .toneFrequency = 440.0, .fMin = 400.f, .fMax = 480.f, .maxLag = 92U}); // 0.3 m and a few samples: under half the tone's period
        expect(ge(results.size(), 5UZ)) << std::format("framed {}, gated {}", framed, gated);
        for (const auto& event : results) {
            expect(statusOf(event) == "Valid") << statusOf(event) << metaText(event, "diagnostics");
            expect(lt(std::hypot(metaValue(event, "x") - 0.1, metaValue(event, "y") - 0.3), 0.05)) << std::format("({}, {})", metaValue(event, "x"), metaValue(event, "y"));
        }
    };
};

} // namespace

int main() { return boost::ut::cfg<>.run({.report_errors = true}) ? 1 : 0; }
