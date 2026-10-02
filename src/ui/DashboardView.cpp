#include "DashboardView.hpp"

#include <algorithm>

#include "common/ImguiWrap.hpp"
#include "common/LookAndFeel.hpp"

#include "scope_exit.hpp"

namespace DigitizerUi {

namespace {
constexpr inline std::size_t kGridCells = 16UZ;
} // namespace

void alignForWidth(float width, float alignment) noexcept {
    const float avail  = ImGui::GetContentRegionAvail().x;
    const float offset = (avail - width) * alignment;
    if (offset > 0.0f) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
    }
}

DashboardView::DashboardView() = default;

DashboardView::~DashboardView() {
    if (opendigitizer::charts::g_chartRequests == std::addressof(_chartRequests)) {
        opendigitizer::charts::g_chartRequests = nullptr;
    }
}

void DashboardView::processPendingRequests() {
    if (_pendingTransmutation) {
        const auto request    = std::move(*_pendingTransmutation);
        _pendingTransmutation = std::nullopt;
        for (auto& uiWindow : _dashboard->uiWindows) {
            if (uiWindow.block && uiWindow.block->uniqueName() == request.chartId) {
                _dashboard->transmuteUIWindow(uiWindow, request.newChartType);
                break;
            }
        }
    }

    for (const auto& chartId : _pendingRemovals) {
        if (auto* uiWindow = _dashboard->findUIWindowByName(chartId)) {
            _dashboard->deleteChart(uiWindow);
        }
    }
    _pendingRemovals.clear();
}

DashboardView::Result DashboardView::draw(Dashboard& dashboard, Mode mode) { return draw(dashboard, mode, Options{}); }

void DashboardView::setLayout(DockingLayoutType type, const std::optional<gr::property_map>& freeLayoutDescription) {
    _dockSpace.setLayoutType(type);
    if (freeLayoutDescription) {
        _dockSpace.loadFreeLayout(*freeLayoutDescription);
    }
    _layoutApplied = true;
}

DashboardView::Result DashboardView::draw(Dashboard& dashboard, Mode mode, const Options& options) {
    if (_dashboard != std::addressof(dashboard)) { // requests and layout of another dashboard no longer apply
        _pendingTransmutation.reset();
        _pendingRemovals.clear();
        _layoutApplied = _layoutApplied && _dashboard == nullptr; // a layout set before the first draw stays
        _dashboard     = std::addressof(dashboard);
    }
    dashboard.handleMessages(); // the dashboard knows its charts and sinks only once the scheduler has replied

    ImGui::PushID(this); // dock ids per view
    Digitizer::utils::scope_exit popId = [] { ImGui::PopID(); };

    Result     result;
    IMW::Child plotsChild("##plots", options.size, false, ImGuiWindowFlags_NoScrollbar);
    if (!dashboard.isInitialised) {
        const char*  text     = "loading dashboard ...";
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        ImGui::SetCursorPos((ImGui::GetWindowSize() - textSize) * 0.5f);
        ImGui::TextDisabled("%s", text);
        return result;
    }
    if (!_layoutApplied) {
        setLayout(dashboard.layoutType, dashboard.windowLayout.empty() ? std::nullopt : std::optional<gr::property_map>(dashboard.windowLayout));
    }
    opendigitizer::charts::g_chartRequests = std::addressof(_chartRequests);
    processPendingRequests();

    result.backgroundClicked = ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    // quickfix for an imgui bug?: a SetCursorPos alone does not seem to be sufficient for getting our cursor to
    // return there after SameLine(). The issue is visible iff we do manual cursor manipulation (as the global signal
    // legend does for the first colour rect). So to make sure the first item draws at the same position as the
    // succeeding ones after SameLine(), insert a dummy size and SameLine, so the imgui context is in the same state as
    // later
    const auto alignBarStart = [] {
        ImGui::ItemSize(ImVec2{}, 0.f);
        ImGui::SameLine();
    };

    switch (options.legend) {
    case LegendPosition::Bottom:
        result.chartPaneSize = ImGui::GetContentRegionAvail() - ImVec2(0.f, _legendBox.y);
        drawCharts(mode, options, result.chartPaneSize);
        ImGui::SetCursorPos(ImVec2(0, ImGui::GetWindowHeight() - _legendBox.y));
        alignBarStart();
        drawBar(mode, result.chartPaneSize, options, result);
        break;
    case LegendPosition::Top:
        alignBarStart();
        drawBar(mode, ImGui::GetContentRegionAvail(), options, result);
        result.chartPaneSize = ImGui::GetContentRegionAvail();
        drawCharts(mode, options, result.chartPaneSize);
        break;
    case LegendPosition::Left:
    case LegendPosition::Right: {
        const ImVec2 avail   = ImGui::GetContentRegionAvail();
        const float  spacing = ImGui::GetStyle().ItemSpacing.x;
        result.chartPaneSize = ImVec2(std::max(1.f, avail.x - _legendColumnWidth - spacing), avail.y);
        if (options.legend == LegendPosition::Left) {
            drawLegendColumn(mode, avail.y, options, result);
            ImGui::SameLine();
            drawCharts(mode, options, result.chartPaneSize);
        } else {
            drawCharts(mode, options, result.chartPaneSize);
            ImGui::SameLine();
            drawLegendColumn(mode, avail.y, options, result);
        }
        break;
    }
    case LegendPosition::None:
        result.chartPaneSize = ImGui::GetContentRegionAvail();
        drawCharts(mode, options, result.chartPaneSize);
        break;
    }
    return result;
}

void DashboardView::drawCharts(Mode mode, const Options& options, ImVec2 paneSize) {
    IMW::Group group;

    if (mode == Mode::Layout) { // layout guide: kGridCells x kGridCells cells spanning the pane
        const uint32_t gridLineColor = ImGui::ColorConvertFloat4ToU32(LookAndFeel::instance().palette().gridLines);
        const ImVec2   pos           = ImGui::GetCursorScreenPos();
        for (std::size_t i = 0UZ; i < kGridCells; ++i) {
            const float x = pos.x + paneSize.x * static_cast<float>(i) / static_cast<float>(kGridCells);
            const float y = pos.y + paneSize.y * static_cast<float>(i) / static_cast<float>(kGridCells);
            ImGui::GetWindowDrawList()->AddLine({x, pos.y}, {x, pos.y + paneSize.y}, gridLineColor);
            ImGui::GetWindowDrawList()->AddLine({pos.x, y}, {pos.x + paneSize.x, y}, gridLineColor);
        }
    }

    DockSpace::Windows windows;
    for (auto& blockPtr : _dashboard->uiGraph.blocks()) {
        if (blockPtr->uiCategory() != gr::UICategory::Content) {
            continue;
        }
        auto& uiWindow = _dashboard->getOrCreateUIWindow(blockPtr); // created lazily
        if (!uiWindow.window) {
            continue;
        }
        windows.push_back(uiWindow.window);
        // the block is captured by value so that it stays alive while it draws
        uiWindow.window->renderFunc                   = [block = blockPtr, mode] { std::ignore = block->draw(opendigitizer::charts::chartDrawConfig(mode)); };
        uiWindow.window->renderDockingContextMenuFunc = [block = blockPtr] {
            opendigitizer::charts::drawDuplicateChartMenuItem(block->uniqueName());
            opendigitizer::charts::drawRemoveChartMenuItem(block->uniqueName());
        };
    }
    if (options.addDockWindows) {
        options.addDockWindows(windows);
    }

    _dockSpace.render(windows, paneSize, mode == Mode::Layout);
}

// one legend entry per line, in a column as wide as the widest entry of the last frame
void DashboardView::drawLegendColumn(Mode mode, float height, const Options& options, Result& result) {
    IMW::Child column("##legendColumn", ImVec2(_legendColumnWidth, height), false, ImGuiWindowFlags_NoScrollbar);
    if (options.barLeading) {
        options.barLeading();
        ImGui::NewLine();
    }
    _signalLegend.setDragDropEnabled(mode == Mode::Interaction);
    const auto rightClickedSinkName = _signalLegend.draw(_dashboard->graphModel, 1.f); // narrower than any entry: one per line
    if (mode == Mode::Interaction) {
        result.rightClickedSinkName = std::string(rightClickedSinkName);
    }
    _legendColumnWidth = std::max(50.f, _signalLegend.legendSize().x + 2.f * ImGui::GetStyle().WindowPadding.x);
    if (options.barTrailing) {
        options.barTrailing();
    }
}

void DashboardView::drawBar(Mode mode, ImVec2 chartPaneSize, const Options& options, Result& result) {
    IMW::Group group;

    if (options.barLeading) {
        options.barLeading();
    }

    alignForWidth(std::max(10.f, _legendBox.x), 0.5f); // centred, using the width of the last frame
    if (options.barCentre) {
        options.barCentre();
        _legendBox = ImGui::GetItemRectSize();
    } else {
        _signalLegend.setDragDropEnabled(mode == Mode::Interaction);
        const auto rightClickedSinkName = _signalLegend.draw(_dashboard->graphModel, chartPaneSize.x);
        if (mode == Mode::Interaction) {
            result.rightClickedSinkName = std::string(rightClickedSinkName);
        }
        _legendBox = _signalLegend.legendSize();
    }

    if (options.barTrailing) {
        options.barTrailing();
    }
    ImGui::Dummy(ImVec2(0.f, 0.f));

    _legendBox.y = std::max(_legendBox.y, LookAndFeel::instance().mainWindowIconButtonSize());
}

} // namespace DigitizerUi
