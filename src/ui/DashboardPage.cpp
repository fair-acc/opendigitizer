#include "DashboardPage.hpp"

#include <format>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/Tag.hpp>
#include <implot.h>
#include <memory>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"
#include "common/TouchHandler.hpp"
#include "conversion.hpp"

#include "components//ImGuiNotify.hpp"
#include "components/Splitter.hpp"

#include "blocks/ImPlotSink.hpp"
#include "blocks/RemoteSource.hpp"

namespace DigitizerUi {

namespace {
constexpr inline auto kMaxPlots   = 16u;
constexpr inline auto kGridWidth  = 16u;
constexpr inline auto kGridHeight = 16u;

// Holds quantity + unit
struct AxisCategory {
    std::string quantity;
    std::string unit;
    uint32_t    color    = 0xAAAAAA;
    AxisScale   scale    = AxisScale::Linear;
    bool        plotTags = true;
};

std::optional<std::size_t> findOrCreateCategory(std::array<std::optional<AxisCategory>, 3>& cats, std::string_view qStr, std::string_view uStr, uint32_t colorDef) {
    for (std::size_t i = 0UZ; i < cats.size(); ++i) {
        // see if it already exists
        if (cats[i].has_value()) {
            const auto& cat = cats[i].value();
            if (cat.quantity == qStr && cat.unit == uStr) {
                return static_cast<int>(i); // found match
            }
        }
    }

    for (std::size_t i = 0UZ; i < cats.size(); ++i) {
        // else try to open a new slot
        if (!cats[i].has_value()) {
            cats[i] = AxisCategory{.quantity = std::string(qStr), .unit = std::string(uStr), .color = colorDef};
            return i;
        }
    }
    return std::nullopt; // no slot left
}

void assignSourcesToAxes(const Dashboard::Plot& plot, Dashboard& /*dashboard*/, std::array<std::optional<AxisCategory>, 3>& xCats, std::array<std::vector<std::string>, 3>& xAxisGroups, std::array<std::optional<AxisCategory>, 3>& yCats, std::array<std::vector<std::string>, 3>& yAxisGroups) {
    enum class AxisKind { X = 0, Y };

    xCats.fill(std::nullopt);
    yCats.fill(std::nullopt);
    for (auto& g : xAxisGroups) {
        g.clear();
    }
    for (auto& g : yAxisGroups) {
        g.clear();
    }

    for (const auto* plotSinkBlock : plot.plotSinkBlocks) {
        auto grBlock = opendigitizer::ImPlotSinkManager::instance().findSink([plotSinkBlock](auto& block) { return block.name() == plotSinkBlock->name(); });
        if (!grBlock) {
            continue;
        }

        // quantity, unit
        std::string qStr, uStr;
        if (auto qOpt = grBlock->settings().get("signal_quantity")) {
            if (auto strPtr = std::get_if<std::string>(&*qOpt)) {
                qStr = *strPtr;
            }
        }
        if (auto uOpt = grBlock->settings().get("signal_unit")) {
            if (auto strPtr = std::get_if<std::string>(&*uOpt)) {
                uStr = *strPtr;
            }
        }

        // axis kind = X or Y
        AxisKind axisKind = AxisKind::Y; // default
        if (auto axisOpt = grBlock->settings().get("signal_axis")) {
            if (auto strPtr = std::get_if<std::string>(&*axisOpt)) {
                if (*strPtr == "X") {
                    axisKind = AxisKind::X;
                }
            }
        }

        bool plotTagFlag = true;
        if (auto tagOpt = grBlock->settings().get("plot_tags")) {
            if (auto boolPtr = std::get_if<bool>(&*tagOpt)) {
                plotTagFlag = *boolPtr;
            }
        }

        auto color = plotSinkBlock->color();
        if (auto idx = findOrCreateCategory(axisKind == AxisKind::X ? xCats : yCats, qStr, uStr, color); idx) {
            if (axisKind == AxisKind::X) {
                xAxisGroups[idx.value()].push_back(plotSinkBlock->name());
                xCats[idx.value()]->plotTags = plotTagFlag;
            } else {
                yAxisGroups[idx.value()].push_back(plotSinkBlock->name());
                yCats[idx.value()]->plotTags = plotTagFlag;
            }
        } else {
            components::Notification::warning(std::format("No free slots for {} axis. Ignoring plotSinkBlock '{}' (q='{}', u='{}')\n", (axisKind == AxisKind::X ? "X" : "Y"), plotSinkBlock->name(), qStr, uStr));
        }
    }
}

std::string buildLabel(const std::optional<AxisCategory>& catOpt, bool isX) {
    if (!catOpt.has_value()) {
        // fallback
        return isX ? "time" : "y-axis [a.u.]";
    }
    const auto& cat = catOpt.value();
    if (!cat.quantity.empty() && !cat.unit.empty()) {
        return std::format("{} [{}]", cat.quantity, cat.unit);
    }
    if (!cat.quantity.empty()) {
        return cat.quantity;
    }
    if (!cat.unit.empty()) {
        return cat.unit;
    }
    return std::format("{}-axis", (isX ? "X" : "Y")); // fallback if both empty
}

template<typename Result>
int enforceNullTerminate(char* buff, int size, const Result& result) {
    if ((result.out - buff) < static_cast<ptrdiff_t>(size)) {
        *result.out = '\0';
    } else if (size > 0) {
        buff[size - 1] = '\0';
    }
    return static_cast<int>(result.out - buff); // number of characters written (excluding null terminator)
}

inline constexpr std::string_view boundedStringView(const char* ptr, std::size_t maxLength) {
    if (!ptr) {
        return "";
    }

    for (std::size_t i = 0UZ; i < maxLength; ++i) {
        if (ptr[i] == '\0') {
            return std::string_view(ptr, i);
        }
    }
    return ""; // null terminator not found
}

inline int formatMetric(double value, char* buff, int size, void* data) {
    constexpr std::array<double, 11>      kScales{1e15, 1e12, 1e9, 1e6, 1e3, 1.0, 1e-3, 1e-6, 1e-9, 1e-12, 1e-15};
    constexpr std::array<const char*, 11> kPrefixes{"P", "T", "G", "M", "k", "", "m", "u", "n", "p", "f"};
    constexpr std::size_t                 maxUnitLength = 10UZ;

    const std::string_view unit = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "0{}", unit));
    }

    std::string_view prefix      = kPrefixes.back();
    double           scaledValue = value / kScales.back(); // fallback if no match

    for (auto [scale, p] : std::views::zip(kScales, kPrefixes)) {
        if (std::abs(value) >= scale) {
            scaledValue = value / scale;
            prefix      = p;
            break;
        }
    }

    return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "{:g}{}{}", scaledValue, prefix, unit));
}

inline std::string formatMinimalScientific(double value, int maxDecimals = 2) {
    if (value == 0.0) {
        return "0";
    }

    const int    exponent = static_cast<int>(std::floor(std::log10(std::abs(value))));
    const double mantissa = value / std::pow(10.0, exponent);

    std::string mantissaStr = std::format("{:.{}f}", mantissa, maxDecimals);
    // trim trailing zeros and '.'
    while (!mantissaStr.empty() && mantissaStr.back() == '0') {
        mantissaStr.pop_back();
    }
    if (!mantissaStr.empty() && mantissaStr.back() == '.') {
        mantissaStr.pop_back();
    }

    return std::format("{}E{}", mantissaStr, exponent);
}

inline int formatScientific(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "0{}", unit));
    }

    const double absVal = std::abs(value);
    if (absVal >= 1e-3 && absVal < 10000.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "{:.3g}{}", value, unit));
    } else {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "{}{}", formatMinimalScientific(value), unit));
    }
}

inline int formatDefault(double value, char* buff, int size, void* data) {
    constexpr std::size_t  maxUnitLength = 10UZ;
    const std::string_view unit          = boundedStringView(static_cast<const char*>(data), maxUnitLength);

    if (value == 0.0) {
        return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "0{}", unit));
    }

    return enforceNullTerminate(buff, size, std::format_to_n(buff, size, "{:g}{}", value, unit));
}

void setupPlotAxes(Dashboard::Plot& plot, const std::array<std::optional<AxisCategory>, 3>& xCats, const std::array<std::optional<AxisCategory>, 3>& yCats) {
    using Axis = Dashboard::Plot::AxisKind;

    const std::size_t nAxesX          = static_cast<std::size_t>(std::ranges::count_if(xCats, [](const auto& c) { return c.has_value(); }));
    const std::size_t nAxesY          = static_cast<std::size_t>(std::ranges::count_if(yCats, [](const auto& c) { return c.has_value(); }));
    const auto        setupSingleAxis = [&nAxesX, &nAxesY, &plot](ImAxis axisId, const std::optional<AxisCategory>& cat, float axisWidth, std::optional<Dashboard::Plot::AxisData*> axisConfigInfo = std::nullopt) {
        if (axisId != ImAxis_X1 && !cat.has_value()) { // workaround for missing xCats definition
            return;
        }

        const bool      isX       = axisId == ImAxis_X1 || axisId == ImAxis_X2 || axisId == ImAxis_X3;
        const bool      finiteMin = axisConfigInfo.has_value() ? std::isfinite(axisConfigInfo.value()->min) : false;
        const bool      finiteMax = axisConfigInfo.has_value() ? std::isfinite(axisConfigInfo.value()->max) : false;
        const AxisScale scale     = axisConfigInfo.has_value() ? axisConfigInfo.value()->scale : cat->scale;

        ImPlotAxisFlags flags = ImPlotAxisFlags_None;
        if (finiteMin && !finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMin;
        } else if (!finiteMin && finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_LockMax;
        } else if (!finiteMin && !finiteMax) {
            flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
        } // else both limits set -> no auto-fit
        if ((axisId == ImAxis_X2) || (axisId == ImAxis_X3) || (axisId == ImAxis_Y2) || (axisId == ImAxis_Y3)) {
            flags |= ImPlotAxisFlags_Opposite;
        }

        bool pushedColor = false;
        if (cat && (isX ? nAxesX > 1 : nAxesY > 1) && !isX) { // axis colour handling isn't enabled for the x-axis for the time being.
            const auto   colorU32toImVec4 = [](uint32_t c) { return ImVec4{float((c >> 16) & 0xFF) / 255.f, float((c >> 8) & 0xFF) / 255.f, float((c >> 0) & 0xFF) / 255.f, 1.f}; };
            const ImVec4 col              = colorU32toImVec4(cat->color);
            ImPlot::PushStyleColor(ImPlotCol_AxisText, col);
            ImPlot::PushStyleColor(ImPlotCol_AxisTick, col);
            pushedColor = true;
        }

        const auto truncateLabel = [](std::string_view original, float availableWidth) -> std::string {
            const float textWidth = ImGui::CalcTextSize(original.data()).x;
            if (textWidth <= availableWidth) {
                return std::string(original);
            }
            static const float ellipsisWidth = ImGui::CalcTextSize("...").x;
            if (availableWidth <= ellipsisWidth + 1.f) {
                return "...";
            }
            const float       scaleFactor  = (availableWidth - ellipsisWidth) / std::max(1.f, textWidth);
            const std::size_t fitCharCount = static_cast<std::size_t>(std::floor(scaleFactor * static_cast<float>(original.size())));
            return std::format("...{}", original.substr(original.size() - fitCharCount));
        };

        const std::string label = (scale == AxisScale::Time) ? "" : truncateLabel(buildLabel(cat, isX), axisWidth);
        ImPlot::SetupAxis(axisId, label.c_str(), flags);

        if (scale != AxisScale::Time) {
            constexpr std::array kMetricUnits{"s", "m", "A", "K", "V", "g", "eV", "Hz"};
            constexpr std::array kLinearUnits{"dB"}; // do not use exponential
            const auto&          unit = (cat && !cat->unit.empty()) ? cat->unit : "";

            const LabelFormat format = axisConfigInfo.has_value() ? axisConfigInfo.value()->format : LabelFormat::Auto;
            using enum LabelFormat;
            switch (format) {
            case Auto:
                if (std::ranges::contains(kMetricUnits, unit)) {
                    ImPlot::SetupAxisFormat(axisId, formatMetric, nullptr);
                } else if (std::ranges::contains(kLinearUnits, unit)) {
                    ImPlot::SetupAxisFormat(axisId, formatDefault, nullptr);
                } else {
                    ImPlot::SetupAxisFormat(axisId, formatScientific, nullptr);
                }
                break;
            case Metric: ImPlot::SetupAxisFormat(axisId, formatMetric, nullptr); break;
            case Scientific: ImPlot::SetupAxisFormat(axisId, formatScientific, nullptr); break;
            case Default:
            default: ImPlot::SetupAxisFormat(axisId, formatDefault, nullptr);
            }
        }

        switch (scale) {
            using enum AxisScale;
        case Log10: ImPlot::SetupAxisScale(axisId, ImPlotScale_Log10); break;
        case SymLog: ImPlot::SetupAxisScale(axisId, ImPlotScale_SymLog); break;
        case Time:
            ImPlot::GetStyle().UseISO8601     = true;
            ImPlot::GetStyle().Use24HourClock = true;
            ImPlot::SetupAxisScale(axisId, ImPlotScale_Time);
            break;
        default: ImPlot::SetupAxisScale(axisId, ImPlotScale_Linear); break;
        }

        if (axisConfigInfo.has_value()) {
            const auto& info = *axisConfigInfo.value();
            if (finiteMin && finiteMax) {
                ImPlot::SetupAxisLimits(axisId, static_cast<double>(info.min), static_cast<double>(info.max));
            } else if (finiteMin || finiteMax) {
                assert(flags & ImPlotAxisFlags_LockMin || flags & ImPlotAxisFlags_LockMax);
                const double minConstraint = finiteMin ? static_cast<double>(info.min) : -std::numeric_limits<double>::infinity();
                const double maxConstraint = finiteMax ? static_cast<double>(info.max) : +std::numeric_limits<double>::infinity();
                ImPlot::SetupAxisLimitsConstraints(axisId, minConstraint, maxConstraint);
            }
        }

        if (pushedColor) {
            ImPlot::PopStyleColor(2);
        }
    };

    float xAxisWidth = 100.f;
    float yAxisWidth = 100.f;
    for (auto& axis : plot.axes) {
        if (axis.axis == Axis::X) {
            xAxisWidth = axis.width;
        } else {
            yAxisWidth = axis.width;
        }
    }

    auto getAxisByKind = [&plot](Dashboard::Plot::AxisKind kind, std::size_t index) -> std::optional<Dashboard::Plot::AxisData*> {
        std::size_t count = 0UZ;
        for (auto& axis : plot.axes) {
            if (axis.axis == kind) {
                if (count++ == index) {
                    return &axis;
                }
            }
        }
        return std::nullopt; // not found
    };

    for (std::size_t i = 0UZ; i < xCats.size(); ++i) {
        setupSingleAxis(static_cast<ImAxis>(ImAxis_X1 + i), xCats[i], xAxisWidth, getAxisByKind(Dashboard::Plot::AxisKind::X, i));
    }

    for (std::size_t i = 0UZ; i < yCats.size(); ++i) {
        setupSingleAxis(static_cast<ImAxis>(ImAxis_Y1 + i), yCats[i], yAxisWidth, getAxisByKind(Dashboard::Plot::AxisKind::Y, i));
    }
}
} // namespace

static bool plotButton(const char* glyph, const char* tooltip) noexcept {
    const bool ret = [&] {
        IMW::StyleColor normal(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        IMW::StyleColor hovered(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0.1f));
        IMW::StyleColor active(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0.2f));
        IMW::Font       font(LookAndFeel::instance().fontIconsSolid);
        return ImGui::Button(glyph);
    }();

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }

    return ret;
}

static void alignForWidth(float width, float alignment = 0.5f) noexcept {
    float avail = ImGui::GetContentRegionAvail().x;
    float off   = (avail - width) * alignment;
    if (off > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
    }
}

DashboardPage::DashboardPage() {
    opendigitizer::ImPlotSinkManager::instance().addListener(this, [this](opendigitizer::ImPlotSinkModel& sink, bool wasAdded) {
        if (!m_dashboard || !wasAdded || _addedSourceBlocksWaitingForSink.empty()) {
            return;
        }

        auto it = std::ranges::find_if(_addedSourceBlocksWaitingForSink, [&sink](const auto& kvp) { return kvp.second.signalData.signalName == sink.signalName(); });
        if (it == _addedSourceBlocksWaitingForSink.end()) {
            std::print("[DashboardPage] Status: A sink added that is not connected to a remote source\n");
            return;
        }

        auto url             = it->first;
        auto sourceInWaiting = it->second;
        _addedSourceBlocksWaitingForSink.erase(it);

        gr::Message message;
        message.cmd      = gr::message::Command::Set;
        message.endpoint = gr::scheduler::property::kEmplaceEdge;
        message.data     = gr::property_map{                       //
            {"sourceBlock"s, sourceInWaiting.sourceBlockName}, //
            {"sourcePort"s, "out"},                            //
            {"destinationBlock"s, sink.uniqueName},            //
            {"destinationPort"s, "in"},                        //
            {"minBufferSize"s, gr::Size_t(4096)},              //
            {"weight"s, 1},                                    //
            {"edgeName"s, std::string()}};
        m_dashboard->graphModel().sendMessage(std::move(message));

        auto& plot = m_dashboard->newPlot(0, 0, 1, 1);
        plot.sourceNames.push_back(sourceInWaiting.signalData.signalName);
        m_dashboard->loadPlotSourcesFor(plot);
    });
}

DashboardPage::~DashboardPage() { opendigitizer::ImPlotSinkManager::instance().removeListener(this); }

inline void formatAxisValue(const ImPlotAxis& axis, double value, char* buf, int size) {
    if (axis.Scale == ImPlotScale_Time) {
        const auto formatIsoTime = [](double timestamp, char* buffer, std::size_t size_) noexcept {
            using Clock          = std::chrono::system_clock;
            const auto timePoint = Clock::time_point(std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(timestamp)));
            const auto secs      = std::chrono::time_point_cast<std::chrono::seconds>(timePoint);
            const auto ms        = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint - secs).count();

            auto result = std::format_to_n(buffer, static_cast<int>(size_) - 1, "{:%Y-%m-%dT%H:%M:%S}.{:03}", secs, ms);

            buffer[std::min(static_cast<std::size_t>(result.size), size_ - 1UZ)] = '\0'; // ensure null-termination
        };
        formatIsoTime(value, buf, static_cast<std::size_t>(size));
    } else if (axis.Formatter) {
        axis.Formatter(value, buf, size, axis.FormatterData);
    } else {
        // do nothing
    }
}

inline void showPlotMouseTooltip(double on_delay_s = 1.0f, double off_delay_s = 30.0f) {
    if (!ImPlot::IsPlotHovered()) {
        return;
    }

    ImPlotContext* ctx  = ImPlot::GetCurrentContext();
    ImPlotPlot*    plot = ImPlot::GetCurrentPlot();
    if (!ctx || !plot) {
        return;
    }

    const ImVec2  px = ImGui::GetMousePos();
    static ImVec2 lastPX{0, 0};
    static double lastTime = 0.0;
    const double  now      = ImGui::GetTime();

    constexpr float epsilon = 10.0f;
    const bool      samePos = std::abs(px.x - lastPX.x) < epsilon && std::abs(px.y - lastPX.y) < epsilon;

    if (!samePos) {
        lastPX   = px;
        lastTime = now;
        return;
    }

    if ((now - lastTime) < on_delay_s || (now - lastTime) > off_delay_s) {
        return;
    }

    auto drawAxisTooltip = [](ImPlotPlot* plot_, ImAxis axisIdx) {
        ImPlotAxis& axis = plot_->Axes[axisIdx];
        if (!axis.Enabled) {
            return;
        }

        const auto mousePos = ImPlot::GetPlotMousePos(axis.Vertical ? IMPLOT_AUTO : axisIdx, axis.Vertical ? axisIdx : IMPLOT_AUTO);

        char buf[128UZ];
        formatAxisValue(axis, axis.Vertical ? mousePos.y : mousePos.x, buf, sizeof(buf));

        std::string_view label = plot_->GetAxisLabel(axis);
        if (label.empty()) {
            ImGui::Text("%s", buf);
        } else {
            ImGui::Text("%s: %s", label.data(), buf);
        }
    };

    // draw actual tooltip
    IMW::Font    font(LookAndFeel::instance().fontSmall[LookAndFeel::instance().prototypeMode ? 1UZ : 0UZ]);
    IMW::ToolTip tooltip;
    for (int i = 0; i < 3; ++i) {
        drawAxisTooltip(plot, ImAxis_X1 + i);
    }
    for (int i = 0; i < 3; ++i) {
        drawAxisTooltip(plot, ImAxis_Y1 + i);
    }
}

// Draw the multi-axis plot
void DashboardPage::drawPlot(Dashboard::Plot& plot) noexcept {
    // 1) Build up two sets of categories for X & Y
    std::array<std::optional<AxisCategory>, 3> xCats{};
    std::array<std::vector<std::string>, 3>    xAxisGroups{};
    std::array<std::optional<AxisCategory>, 3> yCats{};
    std::array<std::vector<std::string>, 3>    yAxisGroups{};

    assignSourcesToAxes(plot, *m_dashboard, xCats, xAxisGroups, yCats, yAxisGroups);

    // 2) Setup up to 3 X axes & 3 Y axes
    setupPlotAxes(plot, xCats, yCats);
    ImPlot::SetupFinish();

    // show tool-tip
    showPlotMouseTooltip();

    // compute axis pixel width or height
    {
        const ImPlotRect axisLimits = ImPlot::GetPlotLimits(IMPLOT_AUTO, IMPLOT_AUTO);
        const ImVec2     p0         = ImPlot::PlotToPixels(axisLimits.X.Min, axisLimits.Y.Min);
        const ImVec2     p1         = ImPlot::PlotToPixels(axisLimits.X.Max, axisLimits.Y.Max);
        const float      xWidth     = std::round(std::abs(p1.x - p0.x));
        const float      yHeight    = std::round(std::abs(p1.y - p0.y));
        std::ranges::for_each(plot.axes, [xWidth, yHeight](auto& a) { a.width = (a.axis == Dashboard::Plot::AxisKind::X) ? xWidth : yHeight; });
    }

    // draw each source on the correct axis
    bool drawTag = true;
    for (opendigitizer::ImPlotSinkModel* plotSinkBlock : plot.plotSinkBlocks) {
        // if tab is invisible work() function does the job in Scheduler thread
        if (!plotSinkBlock->isVisible && isTabVisible()) {
            // consume data if hidden
            std::ignore = plotSinkBlock->invokeWork();
            continue;
        }

        const auto& sourceBlockName = plotSinkBlock->name();

        // figure out which axis group check X first
        std::size_t xAxisID = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0UZ; i < 3UZ && xAxisID == std::numeric_limits<std::size_t>::max(); ++i) {
            auto it = std::ranges::find(xAxisGroups[i], plotSinkBlock->name());
            if (it != xAxisGroups[i].end()) {
                xAxisID = i;
            }
        }
        if (xAxisID == std::numeric_limits<std::size_t>::max()) {
            // default to X0 if not found
            xAxisID = 0UZ;
        }

        // check Y if not found
        std::size_t yAxisID = std::numeric_limits<std::size_t>::max();
        for (std::size_t i = 0UZ; i < 3UZ && yAxisID == std::numeric_limits<std::size_t>::max(); ++i) {
            auto it = std::ranges::find(yAxisGroups[i], plotSinkBlock->name());
            if (it != yAxisGroups[i].end()) {
                yAxisID = i;
            }
        }
        if (yAxisID == std::numeric_limits<std::size_t>::max()) {
            // default to Y0 if not found
            yAxisID = 0;
        }

        if (!plot.axes[xAxisID].plotTags) {
            drawTag = false;
        }
        std::ignore = plotSinkBlock->draw({{"draw_tag", drawTag}, {"xAxisID", xAxisID}, {"yAxisID", yAxisID}, {"scale", std::string(magic_enum::enum_name(plot.axes[0].scale))}});
        drawTag     = false;

        // allow legend item labels to be DND sources
        if (ImPlot::BeginDragDropSourceItem(sourceBlockName.c_str())) {
            DigitizerUi::DashboardPage::DndItem dnd = {&plot, plotSinkBlock};
            ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));

            ImPlot::ItemIcon(plotSinkBlock->color());
            ImGui::SameLine();
            ImGui::TextUnformatted(sourceBlockName.c_str());
            ImPlot::EndDragDropSource();
        }
    }
}

void DashboardPage::draw(Mode mode) noexcept {
    const float  left = ImGui::GetCursorPosX();
    const float  top  = ImGui::GetCursorPosY();
    const ImVec2 size = ImGui::GetContentRegionAvail();

    const bool      horizontalSplit   = size.x > size.y;
    constexpr float splitterWidth     = 6;
    constexpr float halfSplitterWidth = splitterWidth / 2.f;
    const float     ratio             = components::Splitter(size, horizontalSplit, splitterWidth, 0.2f, !m_editPane.selectedBlock());

    ImGui::SetCursorPosX(left);
    ImGui::SetCursorPosY(top);

    {
        IMW::Child plotsChild("##plots", horizontalSplit ? ImVec2(size.x * (1.f - ratio) - halfSplitterWidth, size.y) : ImVec2(size.x, size.y * (1.f - ratio) - halfSplitterWidth), false, ImGuiWindowFlags_NoScrollbar);

        if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_editPane.setSelectedBlock(nullptr, nullptr);
        }

        // Plots
        {
            IMW::Group group;
            drawPlots(mode);
        }
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - legend_box.y));

        // Legend
        {
            IMW::Group group;
            // Button strip
            if (mode == Mode::Layout) {
                if (plotButton("\uF201", "create new chart")) {
                    // chart-line
                    newPlot();
                }
                ImGui::SameLine();
                if (plotButton("\uF7A5", "change to the horizontal layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Row);
                }
                ImGui::SameLine();
                if (plotButton("\uF7A4", "change to the vertical layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Column);
                }
                ImGui::SameLine();
                if (plotButton("\uF58D", "change to the grid layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Grid);
                }
                ImGui::SameLine();
                if (plotButton("\uF248", "change to the free layout")) {
                    m_dockSpace.setLayoutType(DockingLayoutType::Free);
                }
                ImGui::SameLine();
            }

            drawGlobalLegend(mode);

            if (m_dashboard && m_remoteSignalSelector) {
                // Post button strip
                if (mode == Mode::Layout) {
                    ImGui::SameLine();
                    if (plotButton("\uf067", "add signal")) {
                        // 'plus' button in the global legend, adds a new signal
                        // to the dashboard
                        m_remoteSignalSelector->open();
                    }

                    for (const auto& selectedRemoteSignal : m_remoteSignalSelector->drawAndReturnSelected()) {
                        const auto& uriStr_           = selectedRemoteSignal.uri();
                        _addingRemoteSignals[uriStr_] = selectedRemoteSignal;

                        m_dashboard->addRemoteSignal(selectedRemoteSignal);

                        opendigitizer::RemoteSourceManager::instance().setRemoteSourceAddedCallback(uriStr_, [this, selectedRemoteSignal](opendigitizer::RemoteSourceModel& remoteSource) {
                            const auto& uriStr = selectedRemoteSignal.uri();
                            // Switching state for the signal -- from "adding the source block"
                            // to "waiting for sink block to be created"
                            _addedSourceBlocksWaitingForSink[uriStr] = SourceBlockInWaiting{
                                .signalData      = std::move(_addingRemoteSignals[uriStr]), //
                                .sourceBlockName = remoteSource.uniqueName()                //
                            };
                            _addingRemoteSignals.erase(uriStr);

                            // Can be opendigitizer::RemoteStreamSource<float32> or opendigitizer::RemoteDataSetSource<float32>
                            // for the time being, but let's support double (float64) out of the box as well
                            const std::string remoteSourceType = remoteSource.typeName();

                            auto                   it = std::ranges::find(remoteSourceType, '<');
                            const std::string_view remoteSourceBaseType(remoteSourceType.begin(), it);
                            const std::string_view remoteSourceTypeParams(it, remoteSourceType.end());

                            std::string sinkBlockType   = "opendigitizer::ImPlotSink";
                            std::string sinkBlockParams =                                                                                                             //
                                (remoteSourceBaseType == "opendigitizer::RemoteStreamSource" && remoteSourceTypeParams == "<float32>")    ? "<float32>"s              //
                                : (remoteSourceBaseType == "opendigitizer::RemoteStreamSource" && remoteSourceTypeParams == "<float64>")  ? "<float64>"s              //
                                : (remoteSourceBaseType == "opendigitizer::RemoteDataSetSource" && remoteSourceTypeParams == "<float32>") ? "<gr::DataSet<float32>>"s //
                                : (remoteSourceBaseType == "opendigitizer::RemoteDataSetSource" && remoteSourceTypeParams == "<float64>") ? "<gr::DataSet<float64>>"s //
                                                                                                                                          : /* otherwise error */ ""s;

                            gr::Message message;
                            message.cmd      = gr::message::Command::Set;
                            message.endpoint = gr::scheduler::property::kEmplaceBlock;
                            message.data     = gr::property_map{
                                //
                                {"type"s, sinkBlockType + sinkBlockParams}, //
                                {
                                    "properties"s,
                                    gr::property_map{
                                        //
                                        {"signal_name"s, selectedRemoteSignal.signalName}, //
                                        {"signal_unit"s, selectedRemoteSignal.unit}        //
                                    } //
                                } //
                            };
                            m_dashboard->graphModel().sendMessage(std::move(message));
                        });
                    }
                }
            }

            if (LookAndFeel::instance().prototypeMode) {
                ImGui::SameLine();
                // Retrieve FPS and milliseconds per iteration
                const float fps     = ImGui::GetIO().Framerate;
                const auto  str     = std::format("FPS:{:5.0f}({:2}ms)", fps, LookAndFeel::instance().execTime.count());
                const auto  estSize = ImGui::CalcTextSize(str.c_str());
                alignForWidth(estSize.x, 1.0);
                ImGui::Text("%s", str.c_str());
            }
        }
        legend_box.y = std::floor(ImGui::GetItemRectSize().y * 1.5f);
    }

    if (horizontalSplit) {
        const float w = size.x * ratio;
        components::BlockControlsPanel(m_editPane, {left + size.x - w + halfSplitterWidth, top}, {w - halfSplitterWidth, size.y}, true);
    } else {
        const float h = size.y * ratio;
        components::BlockControlsPanel(m_editPane, {left, top + size.y - h + halfSplitterWidth}, {size.x, h - halfSplitterWidth}, false);
    }
}

void DashboardPage::drawPlots(DigitizerUi::DashboardPage::Mode mode) {
    pane_size = ImGui::GetContentRegionAvail();
    pane_size.y -= legend_box.y;

    const float w = pane_size.x / float(kGridWidth);
    const float h = pane_size.y / float(kGridHeight);

    if (mode == Mode::Layout) {
        drawGrid(w, h);
    }

    Dashboard::Plot* toDelete = nullptr;

    DockSpace::Windows windows;
    auto&              plots = m_dashboard->plots();
    windows.reserve(plots.size());

    for (auto& plot : plots) {
        windows.push_back(plot.window);
        plot.window->renderFunc = [this, &plot, mode] {
            const float offset = (mode == Mode::Layout) ? 5.f : 0.f;

            const bool  showTitle = false; // TODO: make this and the title itself a configurable/editable entity
            ImPlotFlags plotFlags = ImPlotFlags_NoChild | ImPlotFlags_NoMouseText;
            plotFlags |= showTitle ? ImPlotFlags_None : ImPlotFlags_NoTitle;
            plotFlags |= mode == Mode::Layout ? ImPlotFlags_None : ImPlotFlags_NoLegend;

            ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{0, 0}); // TODO: make this perhaps a global style setting via ImPlot::GetStyle()
            ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2{3, 1});
            auto plotSize = ImGui::GetContentRegionAvail();
            ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2(0.00f, 0.05f));
            if (TouchHandler<>::BeginZoomablePlot(plot.name, plotSize - ImVec2(2 * offset, 2 * offset), plotFlags)) {
                drawPlot(plot);

                // allow the main plot area to be a DND target
                if (ImPlot::BeginDragDropTargetPlot()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd_type)) {
                        auto* dnd = static_cast<DndItem*>(payload->Data);
                        plot.plotSinkBlocks.push_back(dnd->plotSource);
                        if (auto* dndPlot = dnd->plot) {
                            dndPlot->plotSinkBlocks.erase(std::ranges::find(dndPlot->plotSinkBlocks, dnd->plotSource));
                        }
                    }
                    ImPlot::EndDragDropTarget();
                }

                // update plot.axes if auto-fit is not used
                ImPlotRect rect = ImPlot::GetPlotLimits();
                for (auto& axis : plot.axes) {
                    const bool      isX              = (axis.axis == Dashboard::Plot::AxisKind::X);
                    const ImAxis    axisId           = isX ? ImAxis_X1 : ImAxis_Y1; // TODO multi-axis extension
                    ImPlotAxisFlags axisFlags        = ImPlot::GetCurrentPlot()->Axes[axisId].Flags;
                    bool            isAutoOrRangeFit = (axisFlags & (ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit)) != 0;
                    if (!isAutoOrRangeFit) {
                        // store back min/max from the actual final plot region
                        if (axis.axis == Dashboard::Plot::AxisKind::X) {
                            axis.min = static_cast<float>(rect.X.Min);
                            axis.max = static_cast<float>(rect.X.Max);
                        } else {
                            axis.min = static_cast<float>(rect.Y.Min);
                            axis.max = static_cast<float>(rect.Y.Max);
                        }
                    }
                }

                // handle hover detection if in layout mode
                if (mode == Mode::Layout) {
                    bool plotItemHovered = ImPlot::IsPlotHovered() || ImPlot::IsAxisHovered(ImAxis_X1) || ImPlot::IsAxisHovered(ImAxis_X2) || ImPlot::IsAxisHovered(ImAxis_X3) || ImPlot::IsAxisHovered(ImAxis_Y1) || ImPlot::IsAxisHovered(ImAxis_Y2) || ImPlot::IsAxisHovered(ImAxis_Y3);
                    if (!plotItemHovered) {
                        // Unfortunately there is no function that returns whether the entire legend is hovered,
                        // we need to check one entry at a time
                        for (const auto& plotSinkBlock : plot.plotSinkBlocks) {
                            const auto& sourceName = plotSinkBlock->name();
                            if (ImPlot::IsLegendEntryHovered(sourceName.c_str())) {
                                plotItemHovered = true;

                                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                                    // TODO(NOW) which node was clicked -- it needs to have a sane way to get this?
                                    m_editPane.setSelectedBlock(nullptr, nullptr); // dashboard.localFlowGraph.findBlock(plotSinkBlock->blockName);
                                    m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
                                }
                                break;
                            }
                        }
                    }
                }

                TouchHandler<>::EndZoomablePlot();
                ImPlot::PopStyleVar(3);
            }
        };
    }

    m_dockSpace.render(windows, pane_size);

    if (toDelete) {
        m_dashboard->deletePlot(toDelete);
    }
}

void DashboardPage::drawGrid(float w, float h) {
    const uint32_t gridLineColor = LookAndFeel::instance().style == LookAndFeel::Style::Light ? 0x40000000 : 0x40ffffff;

    auto pos = ImGui::GetCursorScreenPos();
    for (float x = pos.x; x < pos.x + pane_size.x; x += w) {
        ImGui::GetWindowDrawList()->AddLine({x, pos.y}, {x, pos.y + pane_size.y}, gridLineColor);
    }
    for (float y = pos.y; y < pos.y + pane_size.y; y += h) {
        ImGui::GetWindowDrawList()->AddLine({pos.x, y}, {pos.x + pane_size.x, y}, gridLineColor);
    }
}

void DashboardPage::drawGlobalLegend([[maybe_unused]] const DashboardPage::Mode& mode) noexcept {
    alignForWidth(std::max(10.f, legend_box.x), 0.5f);
    legend_box.x = 0.f;
    {
        IMW::Group group;

        enum class MouseClick { No, Left, Right };

        const auto LegendItem = [](std::uint32_t color, std::string_view text, bool enabled = true) -> MouseClick {
            MouseClick   result    = MouseClick::No;
            const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            const ImVec2 rectSize(ImGui::GetTextLineHeight() - 4, ImGui::GetTextLineHeight());
            ImGui::GetWindowDrawList()->AddRectFilled(cursorPos + ImVec2(0, 2), cursorPos + rectSize - ImVec2(0, 2), color | 0xFF000000);
            if (ImGui::InvisibleButton("##Button", rectSize)) {
                result = MouseClick::Left;
            }
            ImGui::SameLine();

            // Draw button text with transparent background
            ImVec2          buttonSize(rectSize.x + ImGui::CalcTextSize(text.data()).x - 4, ImGui::GetTextLineHeight());
            IMW::StyleColor buttonStyle(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            IMW::StyleColor hoveredStyle(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.1f));
            IMW::StyleColor activeStyle(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.2f));
            IMW::StyleColor textStyle(ImGuiCol_Text, enabled ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));

            if (ImGui::Button(text.data(), buttonSize)) {
                result = MouseClick::Left;
            }

            if (ImGui::IsMouseReleased(ImGuiPopupFlags_MouseButtonRight & ImGuiPopupFlags_MouseButtonMask_) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup)) {
                result = MouseClick::Right;
            }

            return result;
        };

        int index    = 0;
        legend_box.x = pane_size.x; // The plots have already filled in full width, legend should be on the new line
        opendigitizer::ImPlotSinkManager::instance().forEach([&](opendigitizer::ImPlotSinkModel& signal) {
            IMW::ChangeId itemId(index++);
            auto          color = signal.color();

            const std::string& label = signal.signalName().empty() ? signal.name() : signal.signalName();

            const auto widthEstimate = ImGui::CalcTextSize(label.c_str()).x + 20 /* icon width */;
            if ((legend_box.x + widthEstimate) < 0.9f * pane_size.x) {
                ImGui::SameLine();
            } else {
                legend_box.x = 0.f; // start a new line
            }

            auto clickedMouseButton = LegendItem(color, label, signal.isVisible);
            if (clickedMouseButton == MouseClick::Right) {
                m_editPane.setSelectedBlock(m_dashboard->graphModel().findBlockByUniqueName(signal.uniqueName), std::addressof(m_dashboard->graphModel()));
                m_editPane.closeTime = std::chrono::system_clock::now() + LookAndFeel::instance().editPaneCloseDelay;
            }
            if (clickedMouseButton == MouseClick::Left) {
                signal.isVisible = !signal.isVisible;
            }
            legend_box.x += ImGui::GetItemRectSize().x;

            if (auto dndSource = IMW::DragDropSource(ImGuiDragDropFlags_None)) {
                DndItem dnd = {nullptr, &signal};
                ImGui::SetDragDropPayload(dnd_type, &dnd, sizeof(dnd));
                LegendItem(color, label, signal.isVisible);
            }
        });
    }
    legend_box.x = ImGui::GetItemRectSize().x;
    legend_box.y = std::max(5.f, ImGui::GetItemRectSize().y);

    if (auto dndTarget = IMW::DragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(dnd_type)) {
            auto* dnd = static_cast<DndItem*>(payload->Data);
            if (auto* dndPlot = dnd->plot) {
                dndPlot->plotSinkBlocks.erase(std::ranges::find(dndPlot->plotSinkBlocks, dnd->plotSource));
            }
        }
    }
    // end draw legend
}

DigitizerUi::Dashboard::Plot* DashboardPage::newPlot() {
    if (m_dashboard->plots().size() < kMaxPlots) {
        // Plot will get adjusted by the layout automatically
        return std::addressof(m_dashboard->newPlot(0, 0, 1, 1));
    }

    return nullptr;
}

void DashboardPage::setLayoutType(DockingLayoutType type) { m_dockSpace.setLayoutType(type); }

} // namespace DigitizerUi
