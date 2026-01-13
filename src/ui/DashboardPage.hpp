#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <deque>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>

#include "Dashboard.hpp"
#include "charts/Chart.hpp" // For g_removeSinkFromChart, g_requestChartTransmutation
#include "common/ImguiWrap.hpp"
#include "components/Block.hpp"
#include "components/Docking.hpp"
#include "components/SignalSelector.hpp"

#include <memory.h>

namespace DigitizerUi {

class DashboardPage {
public:
    enum class Mode { View, Layout };

    /**
     * @brief Drag & drop payload for signal sink transfers.
     *
     * This struct is memcpy-safe (fixed-size char arrays) for ImGui's SetDragDropPayload.
     * Uses string-based lookup: target finds sink by name in SinkRegistry,
     * and removes from source chart by ID.
     */
    struct DndPayload {
        char sinkName[256]     = {}; // Sink's name() for lookup in SinkRegistry
        char sourceChartId[64] = {}; // Source chart's uniqueId (empty = global legend, no removal needed)
    };

private:
    ImVec2 pane_size{0, 0};     // updated by drawPlots(...)
    ImVec2 legend_box{500, 40}; // updated by drawLegend(...)

    static constexpr inline auto* dnd_type = "SIGNAL_SINK_DND";

    // Signals which are schedulerd to be added
    // (source block creation requested)
    std::unordered_map<std::string, SignalData> _addingRemoteSignals;

    // Source blocks which are added, and are
    // waiting for the plot sinks to be created
    struct SourceBlockInWaiting {
        SignalData  signalData;
        std::string sourceBlockName;
    };
    std::unordered_map<std::string, SourceBlockInWaiting> _addedSourceBlocksWaitingForSink;

    DockSpace                             m_dockSpace;
    components::BlockControlsPanelContext m_editPane;
    std::unique_ptr<SignalSelector>       m_remoteSignalSelector;

    Dashboard* m_dashboard = nullptr;

    // Modal dialog state for new plot creation
    bool        _showNewPlotModal  = false;
    std::string _selectedChartType = "XYChart";

    // Deferred chart transmutation request (processed at start of next frame)
    struct PendingTransmutation {
        std::string chartId;
        std::string newChartType;
    };
    std::optional<PendingTransmutation> _pendingTransmutation;

    void drawPlots(DigitizerUi::DashboardPage::Mode mode);
    void drawGrid(float w, float h);
    void drawGlobalLegend(const Mode& mode) noexcept;

public:
    DashboardPage();
    ~DashboardPage();

    void draw(Mode mode = Mode::View) noexcept;
    void setLayoutType(DockingLayoutType);

    /* no optional of ref yet */ DigitizerUi::Dashboard::Plot* newPlot(std::string_view chartType = "XYChart");
    void                                                       drawNewPlotModal();

    void setDashboard(Dashboard& dashboard) {
        m_dashboard = std::addressof(dashboard);
#ifndef OPENDIGITIZER_TEST
        // SignalSelector triggers RemoteSignalSources which uses opencmw::client::ClientContext
        // making self-contained tests difficult to write. Everything is tightly coupled and
        // this is the best place to break the dependency.
        m_remoteSignalSelector = std::make_unique<SignalSelector>(dashboard.graphModel());
#endif
        // Set up g_removeSinkFromChart callback for cross-chart D&D operations
        opendigitizer::charts::g_removeSinkFromChart = [this](std::string_view chartId, std::string_view sinkName) {
            if (!m_dashboard) {
                return;
            }
            // Find UIWindow by block's uniqueName and remove sink via settings
            for (auto& uiWindow : m_dashboard->uiWindows()) {
                if (uiWindow.block && uiWindow.block->uniqueName() == chartId) {
                    auto names = uiWindow.sinkNames();
                    std::erase(names, std::string(sinkName));
                    uiWindow.setSinkNames(names);
                    return;
                }
            }
        };

        // Set up g_requestChartTransmutation callback for chart type changes
        // Note: Transmutation is deferred to the start of next frame to avoid
        // modifying/destroying charts during their draw() call
        opendigitizer::charts::g_requestChartTransmutation = [this](std::string_view chartId, std::string_view newChartType) -> bool {
            if (!m_dashboard) {
                return false;
            }
            // Store request for deferred processing
            _pendingTransmutation = PendingTransmutation{std::string(chartId), std::string(newChartType)};
            return true;
        };
    }

    /**
     * @brief Process any pending chart transmutation request.
     * Call this at the start of draw() before rendering any charts.
     */
    void processPendingTransmutation() {
        if (!_pendingTransmutation || !m_dashboard) {
            return;
        }

        auto request          = std::move(*_pendingTransmutation);
        _pendingTransmutation = std::nullopt;

        // Try UIWindow-based transmutation first
        for (auto& uiWindow : m_dashboard->uiWindows()) {
            if (uiWindow.block && uiWindow.block->uniqueName() == request.chartId) {
                m_dashboard->transmuteUIWindow(uiWindow, request.newChartType);
                return;
            }
        }

        // Fallback to Plot-based transmutation (legacy, during transition)
        for (auto& plot : m_dashboard->plots()) {
            if (plot.hasChart() && plot.chart->uniqueId() == request.chartId) {
                m_dashboard->transmuteChart(plot, request.newChartType);
                return;
            }
        }
    }
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
