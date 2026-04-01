#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <deque>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>

#include "Dashboard.hpp"
#include "charts/Chart.hpp"        // For g_addSinkToChart, g_requestChartTransmutation
#include "charts/SinkRegistry.hpp" // For SinkRegistry listener
#include "common/ImguiWrap.hpp"
#include "components/Block.hpp"
#include "components/Docking.hpp"
#include "components/SignalSelector.hpp"

#include <memory>

namespace DigitizerUi {

class DashboardPage {
public:
    enum class Mode { View, Interaction, Layout };

private:
    static constexpr const char* addChartPopupID          = "New Chart";
    static constexpr const char* enterViewOnlyModePopupID = "Lock the dashboard?##lockMode";

    ImVec2 _legendBox{500, 40}; // updated by drawLegend(...)

    std::function<void()>     _requestViewOnlyMode;
    std::function<void(bool)> _requestSetLayoutMode;

    // signals which are scheduled to be added
    // (source block creation requested)
    std::unordered_map<std::string, SignalData> _addingRemoteSignals;

    // source blocks which are added, waiting for the plot sinks to be created
    struct SourceBlockInWaiting {
        SignalData  signalData;
        std::string sourceBlockName;
    };
    std::unordered_map<std::string, SourceBlockInWaiting> _addedSourceBlocksWaitingForSink;

    DockSpace                             _dockSpace;
    components::BlockControlsPanelContext _editPane;
    std::unique_ptr<SignalSelector>       _remoteSignalSelector;

    Dashboard* _dashboard = nullptr;

    // modal dialog state for new plot creation
    std::string _sinkForNewPlot;

    // deferred chart transmutation request (processed at start of next frame)
    struct PendingTransmutation {
        std::string chartId;
        std::string newChartType;
    };
    std::optional<PendingTransmutation> _pendingTransmutation;

    // deferred chart removal requests (processed at start of next frame)
    std::vector<std::string> _pendingRemovals;

    struct LegendItemClickResult {
        bool        shouldOpenEnterViewOnlyModeModal = false;
        bool        shouldOpenNewPlotModal           = false;
        std::string sinkForNewPlot;
    };

    void                                drawNewPlotModal();                                         // modifies _showNewPlotModal if close is requested
    [[nodiscard]] ImVec2                drawCharts(Mode mode) noexcept;                             // returns the size of area used for charts
    [[nodiscard]] LegendItemClickResult drawLegend(Mode mode, ImVec2 chartPaneSize) noexcept;       // sets _legendBox
    [[nodiscard]] ImVec2                drawLegendCenter(Mode mode, ImVec2 chartPaneSize) noexcept; // returns total size of centered legend part
    void                                drawToolbarLayoutButtons(float plotButtonSize) noexcept;
    void                                addSelectedRemoteSignal(const SignalData& selectedRemoteSignal) noexcept;
    void                                doViewModeOverlayArea() noexcept;

public:
    DashboardPage();
    ~DashboardPage();

    void draw(Mode mode = Mode::View) noexcept;
    void setLayoutType(DockingLayoutType);
    void setRequestViewOnlyModeHandler(std::function<void()>&& function) { _requestViewOnlyMode = std::move(function); }
    void setRequestSetLayoutModeHandler(std::function<void(bool)>&& function) { _requestSetLayoutMode = std::move(function); }

    /* no optional of ref yet */ DigitizerUi::Dashboard::UIWindow* newUIBlock(std::string_view chartType = "XYChart", std::string_view initialSignal = {});

    void setDashboard(Dashboard& dashboard) {
        _dashboard = std::addressof(dashboard);
#ifndef OPENDIGITIZER_TEST
        // SignalSelector triggers RemoteSignalSources which uses opencmw::client::ClientContext
        // making self-contained tests difficult to write. Everything is tightly coupled and
        // this is the best place to break the dependency.
        _remoteSignalSelector = std::make_unique<SignalSelector>(dashboard.graphModel);
#endif
        // set up g_addSinkToChart callback for D&D add operations
        opendigitizer::charts::dnd::g_addSinkToChart = [this](std::string_view chartId, std::string_view sinkName) {
            if (!_dashboard) {
                return;
            }
            for (auto& uiWindow : _dashboard->uiWindows) {
                if (uiWindow.block && uiWindow.block->uniqueName() == chartId) {
                    auto names = grc_compat::getBlockSinkNames(uiWindow.block.get());
                    if (std::find(names.begin(), names.end(), std::string(sinkName)) == names.end()) {
                        names.push_back(std::string(sinkName));
                        grc_compat::setBlockSinkNames(uiWindow.block.get(), names);
                    }
                    return;
                }
            }
        };

        // set up g_requestChartTransmutation callback for chart type changes
        // transmutation is deferred to the start of next frame to avoid
        // modifying/destroying charts during their draw() call
        opendigitizer::charts::g_requestChartTransmutation = [this](std::string_view chartId, std::string_view newChartType) -> bool {
            if (!_dashboard) {
                return false;
            }
            // store request for deferred processing
            _pendingTransmutation = PendingTransmutation{std::string(chartId), std::string(newChartType)};
            return true;
        };

        // set up g_requestChartDuplication callback for "Duplicate Chart" menu item
        opendigitizer::charts::g_requestChartDuplication = [this](std::string_view chartId) {
            if (!_dashboard) {
                return;
            }
            _dashboard->copyChart(chartId);
        };

        // set up g_requestChartRemoval callback for "Remove Chart" menu item
        // removal is deferred to avoid modifying the chart collection during iteration
        opendigitizer::charts::g_requestChartRemoval = [this](std::string_view chartId) { requestChartRemoval(chartId); };
    }

    void processPendingTransmutation() {
        if (!_pendingTransmutation || !_dashboard) {
            return;
        }

        auto request          = std::move(*_pendingTransmutation);
        _pendingTransmutation = std::nullopt;

        // find UIWindow by chartId and transmute
        for (auto& uiWindow : _dashboard->uiWindows) {
            if (uiWindow.block && uiWindow.block->uniqueName() == request.chartId) {
                _dashboard->transmuteUIWindow(uiWindow, request.newChartType);
                return;
            }
        }
    }

    void requestChartRemoval(std::string_view chartId) { _pendingRemovals.emplace_back(chartId); }

    void processPendingRemovals() {
        if (_pendingRemovals.empty() || !_dashboard) {
            return;
        }

        for (const auto& chartId : _pendingRemovals) {
            if (auto* uiWindow = _dashboard->findUIWindowByName(chartId)) {
                _dashboard->deleteChart(uiWindow);
            }
        }
        _pendingRemovals.clear();
    }
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
