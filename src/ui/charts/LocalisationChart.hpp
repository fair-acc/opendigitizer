#ifndef OPENDIGITIZER_CHARTS_LOCALISATIONCHART_HPP
#define OPENDIGITIZER_CHARTS_LOCALISATIONCHART_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <opendigitizer/localisation/DelayAssociation.hpp>
#include <opendigitizer/localisation/DelayConstraints.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

#include "../common/LookAndFeel.hpp"
#include "../common/TouchHandler.hpp"
#include <implot.h>

namespace opendigitizer::charts {

/// the latest result event of a DelayLocator: one whose trigger_meta_info carries `status`
struct LocatedResult {
    std::string          status;
    localisation::Point3 position{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    double               viewZ            = std::numeric_limits<double>::quiet_NaN();
    double               rmsRangeResidual = std::numeric_limits<double>::quiet_NaN();
};

[[nodiscard]] inline std::optional<LocatedResult> locatedResultOf(const gr::property_map& event) {
    const auto meta = gr::property_map_view{event}.get_if<gr::property_map>(gr::tag::kTriggerMetaInfo.key());
    if (!meta) {
        return std::nullopt;
    }
    const gr::property_map_view info{*meta};
    const auto                  status = info.get_if<std::string_view>("status");
    if (!status) {
        return std::nullopt;
    }
    const auto number = [&](std::string_view key) {
        const auto* value = info.get_if<double>(key);
        return value != nullptr ? *value : std::numeric_limits<double>::quiet_NaN();
    };
    return LocatedResult{.status = std::string(*status), .position = {number("x"), number("y"), number("z")}, .viewZ = number("view_z"), .rmsRangeResidual = number("rms_range_residual")};
}

struct LocalisationChart : gr::Block<LocalisationChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<R""(@brief Draws TDOA hyperbolas or bistatic ellipses from dated arrival events, their intersections and the position a DelayLocator published.

Event sinks feed it: arrival events named by `receiver_names` (emissions by `transmitter_names`) are associated as DelayLocator does
(each receiver's latest arrival, `pairs` within `coincidence_window`); a DelayLocator's result events give the detected position.
Each sink is a layer of its own (e.g. threshold onsets and correlated delays of one array), drawn in the sink's colour when there are several.)"">;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    static constexpr std::string_view kChartTypeName = "LocalisationChart";

    A<std::string, "chart name", gr::Visible>              chart_name;
    A<std::string, "chart title">                          chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks = {};

    localisation::ModeSetting            mode                     = localisation::Mode::tdoa;
    gr::algorithm::ElementPositions      element_positions        = localisation::kDeckSquare;
    localisation::ReceiverNames          receiver_names           = {};
    localisation::TransmitterPositions   transmitter_positions    = localisation::kOneTransmitter;
    localisation::TransmitterNames       transmitter_names        = {};
    gr::algorithm::PropagationSpeed      propagation_speed        = 343.0;
    localisation::PairSelectionSetting   pairs                    = localisation::PairSelection::sequential;
    localisation::PairList               pair_list                = {};
    localisation::CoincidenceWindow      coincidence_window       = 0.0;
    localisation::TimingOffsets          timing_offsets           = {};
    localisation::DirectPathGuard        direct_path_guard        = 0.0;
    localisation::RegionOfInterest       roi                      = localisation::kDefaultRoi;
    localisation::ViewHeight             view_z                   = std::numeric_limits<double>::quiet_NaN();
    localisation::CurveSegments          curve_segments           = 192U;
    localisation::CandidateMergeDistance candidate_merge_distance = 0.02;
    localisation::ConsensusDistance      consensus_distance       = 0.15;
    localisation::MaxRangeResidual       max_range_residual       = 0.05;

    A<bool, "show legend", gr::Visible>                                                                          show_legend         = true;
    A<bool, "show grid", gr::Visible>                                                                            show_grid           = true;
    A<bool, "show_receivers", gr::Visible>                                                                       show_receivers      = true;
    A<bool, "show_receiver_names", gr::Visible>                                                                  show_receiver_names = true;
    A<bool, "show_curves", gr::Visible>                                                                          show_curves         = true;
    A<bool, "show_curve_labels", gr::Visible, gr::Doc<"each curve's foci and delay on the curve">>               show_curve_labels   = false;
    A<bool, "show_intersections", gr::Visible>                                                                   show_intersections  = true;
    A<bool, "show_position", gr::Visible, gr::Doc<"the position a DelayLocator published">>                      show_position       = true;
    A<bool, "show_status", gr::Visible>                                                                          show_status         = true;
    A<bool, "X auto-scale", gr::Doc<"false: the roi, then the user's pan and zoom">>                             x_auto_scale        = false;
    A<bool, "Y auto-scale">                                                                                      y_auto_scale        = false;
    A<std::vector<double>, "reference_position", gr::Unit<"m">, gr::Doc<"x, y of a known source, display only">> reference_position  = {};

    static constexpr SignalKind supportedSignals = SignalKind::Events;

    GR_MAKE_REFLECTABLE(LocalisationChart, chart_name, chart_title, data_sinks, mode, element_positions, receiver_names, transmitter_positions, transmitter_names, propagation_speed, pairs, pair_list, coincidence_window, timing_offsets, direct_path_guard, roi, view_z, curve_segments, candidate_merge_distance, consensus_distance, max_range_residual, //
        show_legend, show_grid, show_receivers, show_receiver_names, show_curves, show_curve_labels, show_intersections, show_position, show_status, x_auto_scale, y_auto_scale, reference_position);

    struct TracedCurve {
        std::vector<localisation::Point2>   points;
        std::string                         legend;
        std::string                         annotation;
        std::optional<localisation::Point2> annotationAt;
    };

    struct DrawCounts {
        std::size_t receivers     = 0UZ;
        std::size_t curves        = 0UZ;
        std::size_t curveLabels   = 0UZ;
        std::size_t intersections = 0UZ;
        bool        position      = false;
        bool        status        = false;
    };

    struct Layer {                          // one per data sink, associated and solved apart from the others
        std::string                  label; // the sink's signal name
        std::uint32_t                colour = 0U;
        localisation::Solution       traced{}; // curves and intersections of the sink's associated arrivals
        std::vector<TracedCurve>     curves{};
        std::optional<LocatedResult> result{};
    };

    std::optional<std::uint64_t>                      _tracedGeneration; // sum of the sinks' event generations the curves were traced for
    std::vector<Layer>                                _layers;
    std::size_t                                       _tracedEvents = 0UZ;
    std::map<std::string, std::uint64_t, std::less<>> _generationAtGeometry; // per sink: the events before the geometry changed belong to another array
    DrawCounts                                        _drawn;                // in the last draw, over every layer
    std::vector<DrawCounts>                           _drawnLayers;          // in the last draw, per layer (receivers in none)
    std::vector<std::optional<localisation::Point2>>  _drawnPositions;       // in the last draw, per layer
    std::optional<localisation::Point2>               _drawnPosition;        // in the last draw: the first layer's that has one
    std::optional<std::array<double, 4>>              _fittedRoi;            // the roi the axes were last fitted to; then the user's pan and zoom

    [[nodiscard]] static constexpr std::string_view                    chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                                     uniqueId() const noexcept { return this->unique_name; }
    [[nodiscard]] const DrawCounts&                                    drawn() const noexcept { return _drawn; }
    [[nodiscard]] std::optional<localisation::Point2>                  drawnPosition() const noexcept { return _drawnPosition; }
    [[nodiscard]] std::span<const DrawCounts>                          drawnLayers() const noexcept { return _drawnLayers; }
    [[nodiscard]] std::span<const std::optional<localisation::Point2>> drawnPositions() const noexcept { return _drawnPositions; }
    [[nodiscard]] std::size_t                                          tracedEvents() const noexcept { return _tracedEvents; }
    [[nodiscard]] std::optional<std::uint64_t>                         tracedGeneration() const noexcept { return _tracedGeneration; }

    void reset() { requestRenderReset(); }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        handleSettingsChanged(newSettings);
        _tracedGeneration.reset();
        if (localisation::changesGeometry(oldSettings, newSettings)) {
            _generationAtGeometry.clear();
            for (const auto& sink : _signalSinks) {
                _generationAtGeometry.insert_or_assign(std::string(sink->uniqueName()), sink->eventGeneration());
            }
            resetRenderState();
        }
    }

    void resetRenderState() {
        _tracedGeneration.reset();
        _layers.clear();
        _drawnLayers.clear();
        _drawnPositions.clear();
        _drawn = {};
        _drawnPosition.reset();
        _fittedRoi.reset();
    }

    void traceIfChanged() {
        std::uint64_t generation = 0U;
        for (const auto& sink : _signalSinks) {
            generation += sink->eventGeneration();
        }
        if (_tracedGeneration == generation) {
            return;
        }
        _tracedGeneration              = generation;
        const localisation::Geometry g = localisation::geometryOf(*this);
        _layers.clear();
        for (const auto& sink : _signalSinks) {
            Layer                            layer{.label = std::string(sink->signalName()), .colour = sink->color()};
            std::vector<localisation::Dated> arrivals;
            std::optional<std::uint64_t>     resultTimeNs;
            {
                DataGuard         lock   = sink->dataGuard();
                const auto        events = sink->events();
                const auto        since  = _generationAtGeometry.find(sink->uniqueName());
                const std::size_t fresh  = since == _generationAtGeometry.end() ? events.size() : static_cast<std::size_t>(std::min<std::uint64_t>(sink->eventGeneration() - since->second, events.size()));
                for (const gr::property_map& event : events.last(fresh)) {
                    if (const auto result = locatedResultOf(event)) {
                        const auto dated = localisation::datedOf(event);
                        if (!resultTimeNs || (dated && dated->ns >= *resultTimeNs)) {
                            layer.result = result;
                            resultTimeNs = dated ? std::optional{dated->ns} : resultTimeNs;
                        }
                    } else if (const auto dated = localisation::datedOf(event)) {
                        arrivals.push_back(*dated);
                    }
                }
            }
            const double                    viewZ       = localisation::viewHeightOf(view_z.value, layer.result && std::isfinite(layer.result->viewZ) ? std::optional{layer.result->viewZ} : std::nullopt, g.receivers);
            const localisation::Association association = localisation::associate(arrivals, g, viewZ);
            layer.traced                                = localisation::solve(association.constraints, localisation::solverSettingsOf(*this, viewZ));
            for (std::size_t n = 0UZ; n < association.constraints.size(); ++n) {
                const auto& constraint = association.constraints[n];
                TracedCurve curve{.points = n < layer.traced.curves.size() ? layer.traced.curves[n] : std::vector<localisation::Point2>{}, .legend = std::format("{}##constraint{}", pairName(constraint, g), n), .annotation = curveAnnotation(constraint, g), .annotationAt = std::nullopt};
                curve.annotationAt = annotationPoint(curve.points, layer.result);
                layer.curves.push_back(std::move(curve));
            }
            _layers.push_back(std::move(layer));
        }
        ++_tracedEvents;
    }

    /// "audio0-audio1" (TDOA) or "tx0+audio1" (bistatic)
    [[nodiscard]] static std::string pairName(const localisation::Constraint& constraint, const localisation::Geometry& g) {
        const auto focus = [&](int index) { return index >= 0 ? localisation::nameOf(g.receiverNames, static_cast<std::size_t>(index), "") : localisation::nameOf(g.transmitterNames, static_cast<std::size_t>(-1 - index), "tx"); };
        return std::format("{}{}{}", focus(constraint.focusA), constraint.sign > 0.0 ? "+" : "-", focus(constraint.focusB));
    }

    [[nodiscard]] static std::string curveAnnotation(const localisation::Constraint& constraint, const localisation::Geometry& g) { return std::format("{} {:.3f} ms", pairName(constraint, g), g.speed > 0.0 ? 1e3 * constraint.value / g.speed : std::numeric_limits<double>::quiet_NaN()); }

    /// where a curve's label goes: its point farthest from the detected position (else the roi centre), clear of the roi edge
    [[nodiscard]] std::optional<localisation::Point2> annotationPoint(const std::vector<localisation::Point2>& curve, const std::optional<LocatedResult>& result) const {
        const std::array                    box    = localisation::roiOf(roi.value);
        const double                        margin = static_cast<double>(DigitizerUi::LookAndFeel::instance().chartStyle.localisationTagEdgeMargin) * std::min(box[1] - box[0], box[3] - box[2]);
        const bool                          placed = result && std::isfinite(result->position[0]);
        const localisation::Point2          anchor = placed ? localisation::Point2{result->position[0], result->position[1]} : localisation::Point2{(box[0] + box[1]) / 2.0, (box[2] + box[3]) / 2.0};
        std::optional<localisation::Point2> farthest;
        double                              distance = -1.0;
        for (const localisation::Point2& p : curve) {
            const bool clear = p[0] > box[0] + margin && p[0] < box[1] - margin && p[1] > box[2] + margin && p[1] < box[3] - margin;
            if (clear && std::hypot(p[0] - anchor[0], p[1] - anchor[1]) > distance) {
                distance = std::hypot(p[0] - anchor[0], p[1] - anchor[1]);
                farthest = p;
            }
        }
        return farthest;
    }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);
        if (_signalSinks.empty()) {
            drawEmptyPlot("No signals", plotFlags, plotSize, chartMode);
            return gr::work::Status::OK;
        }
        traceIfChanged();
        if (!DigitizerUi::TouchHandler<>::BeginZoomablePlot(chart_name.value, plotSize, plotFlags | ImPlotFlags_Equal)) {
            return gr::work::Status::OK;
        }
        const ImPlotAxisFlags gridFlags = showGrid ? ImPlotAxisFlags_None : ImPlotAxisFlags_NoGridLines;
        ImPlot::SetupAxis(ImAxis_X1, "x [m]", gridFlags | (x_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None));
        ImPlot::SetupAxis(ImAxis_Y1, "y [m]", gridFlags | (y_auto_scale ? ImPlotAxisFlags_AutoFit : ImPlotAxisFlags_None));
        const std::array box = localisation::roiOf(roi.value);
        if (_fittedRoi != box) {
            ImPlot::SetupAxesLimits(box[0], box[1], box[2], box[3], ImPlotCond_Always);
            _fittedRoi = box;
        }
        setupFinish();
        drawLayers();
        tooltip::showPlotMouseTooltip();
        handleCommonInteractions(chartMode);
        DigitizerUi::TouchHandler<>::EndZoomablePlot();
        return gr::work::Status::OK;
    }

    void drawLayers() {
        const auto&  style      = DigitizerUi::LookAndFeel::instance().chartStyle;
        const ImVec4 foreground = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        const bool   layered    = _layers.size() > 1UZ; // a colour per sink; a single sink keeps a colour per curve
        _drawn                  = {};
        _drawnLayers.assign(_layers.size(), DrawCounts{});
        _drawnPositions.assign(_layers.size(), std::nullopt);
        _drawnPosition.reset();
        const auto colourOf = [&](const Layer& layer, std::size_t n) { return layered ? sinkColor(layer.colour) : ImPlot::GetColormapColor(static_cast<int>(n)); };

        for (std::size_t k = 0UZ; show_curves && k < _layers.size(); ++k) {
            const Layer& layer = _layers[k];
            for (std::size_t n = 0UZ; n < layer.curves.size(); ++n) {
                const TracedCurve& curve = layer.curves[n];
                if (curve.points.empty()) {
                    continue;
                }
                const std::string legend = layered ? std::format("{}: {}", layer.label, curve.legend) : curve.legend;
                ImPlot::SetNextLineStyle(colourOf(layer, n), style.localisationCurveWidth);
                ImPlot::PlotLine(legend.c_str(), &curve.points[0][0], &curve.points[0][1], static_cast<int>(curve.points.size()), ImPlotLineFlags_SkipNaN, 0, static_cast<int>(sizeof(localisation::Point2)));
                ++_drawnLayers[k].curves;
                if (show_curve_labels && curve.annotationAt) {
                    ImPlot::Annotation((*curve.annotationAt)[0], (*curve.annotationAt)[1], colourOf(layer, n), ImVec2(0.f, 0.f), true, "%s", curve.annotation.c_str());
                    ++_drawnLayers[k].curveLabels;
                }
            }
        }

        const auto markers = [&](const char* label, const std::vector<localisation::Point3>& points, ImPlotMarker marker, const std::vector<std::string>& names, std::string_view prefix) {
            for (std::size_t i = 0UZ; i < points.size(); ++i) {
                ImPlot::SetNextMarkerStyle(marker, style.localisationSensorMarkerSize, foreground, IMPLOT_AUTO, foreground);
                ImPlot::PlotScatter(label, &points[i][0], &points[i][1], 1);
                if (show_receiver_names) {
                    ImPlot::PlotText(localisation::nameOf(names, i, prefix).c_str(), points[i][0], points[i][1], ImVec2(0.f, -style.localisationLabelOffset));
                }
                ++_drawn.receivers;
            }
        };
        if (show_receivers) {
            markers("##receivers", localisation::pointsOf(element_positions.value), ImPlotMarker_Square, receiver_names.value, "");
            if (mode == localisation::Mode::bistatic) {
                markers("##transmitters", localisation::pointsOf(transmitter_positions.value), ImPlotMarker_Diamond, transmitter_names.value, "tx");
            }
        }

        const ImPlotRect limits = ImPlot::GetPlotLimits();
        for (std::size_t k = 0UZ; k < _layers.size(); ++k) {
            const Layer& layer  = _layers[k];
            DrawCounts&  counts = _drawnLayers[k];
            for (std::size_t c = 0UZ; show_intersections && c < layer.traced.intersections.size(); ++c) {
                const auto&  candidate = layer.traced.intersections[c];
                const ImVec4 colour    = colourOf(layer, static_cast<std::size_t>(candidate.constraints[0]));
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, style.localisationIntersectionMarkerSize, candidate.accepted ? colour : ImVec4(0.f, 0.f, 0.f, 0.f), IMPLOT_AUTO, colour);
                ImPlot::PlotScatter("##intersections", &candidate.point[0], &candidate.point[1], 1);
                ++counts.intersections;
            }
            if (show_position && layer.result && layer.result->status == localisation::statusName(localisation::Status::valid) && std::isfinite(layer.result->position[0])) {
                const auto&       p      = layer.result->position;
                const ImVec4      colour = layered ? sinkColor(layer.colour) : foreground;
                const std::string label  = layered ? layer.label : std::string("##position");
                ImPlot::SetNextMarkerStyle(ImPlotMarker_Asterisk, style.localisationMeanMarkerSize, colour, style.localisationMeanMarkerWeight, colour);
                ImPlot::PlotScatter(label.c_str(), &p[0], &p[1], 1);
                ImPlot::PlotText(std::format("({:.3f}, {:.3f})", p[0], p[1]).c_str(), p[0], p[1], ImVec2(0.f, -style.localisationLabelOffset));
                _drawnPositions[k] = localisation::Point2{p[0], p[1]};
                _drawnPosition     = _drawnPosition ? _drawnPosition : _drawnPositions[k];
                counts.position    = true;
            }
            if (show_status && layer.result) {
                const std::string measured = std::isfinite(layer.result->rmsRangeResidual) ? std::format("{}  rms {:.3f} m", layer.result->status, layer.result->rmsRangeResidual) : layer.result->status;
                const std::string status   = layered ? std::format("{}: {}", layer.label, measured) : measured;
                const ImVec2      size     = ImGui::CalcTextSize(status.c_str());
                const float       row      = static_cast<float>(k) * ImGui::GetTextLineHeightWithSpacing();
                ImPlot::PlotText(status.c_str(), limits.X.Min, limits.Y.Max, ImVec2(style.localisationStatusPadding.x + size.x / 2.f, style.localisationStatusPadding.y + size.y / 2.f + row));
                counts.status = true;
            }
            _drawn.curves += counts.curves;
            _drawn.curveLabels += counts.curveLabels;
            _drawn.intersections += counts.intersections;
            _drawn.position = _drawn.position || counts.position;
            _drawn.status   = _drawn.status || counts.status;
        }

        if (reference_position.value.size() >= 2UZ) {
            const double x = reference_position.value[0];
            const double y = reference_position.value[1];
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Plus, style.localisationMeanMarkerSize, foreground, IMPLOT_AUTO, foreground);
            ImPlot::PlotScatter("##reference", &x, &y, 1);
        }
    }
};

} // namespace opendigitizer::charts

inline auto registerLocalisationChartCompatibilities = opendigitizer::charts::registerChartSignalCompatibility<opendigitizer::charts::LocalisationChart>();

#endif // OPENDIGITIZER_CHARTS_LOCALISATIONCHART_HPP
