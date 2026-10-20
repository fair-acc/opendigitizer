#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <optional>
#include <string>
#include <unordered_map>

#include "Dashboard.hpp"
#include "DashboardView.hpp"
#include "charts/Chart.hpp"
#include "charts/SinkRegistry.hpp" // For SinkRegistry listener
#include "common/ImguiWrap.hpp"
#include "components/Block.hpp"
#include "components/Docking.hpp"
#include "components/SignalSelector.hpp"
#include "components/Splitter.hpp"

#include <memory>

namespace DigitizerUi {

class DashboardPage {
public:
    using Mode = opendigitizer::charts::ChartMode;

private:
    static constexpr const char* addChartPopupID                      = "New Chart";
    static constexpr const char* enterViewOnlyModePopupID             = "Set dashboard to locked mode?##lockMode";
    static constexpr const char* changeLabelPopupID                   = "##Change property control window label";
    static constexpr const char* propertyControLWindowContextWindowID = "propertyControlWindowContextMenu";

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

    components::BlockControlsPanelContext _editPane;
    components::SplitterState             _splitter;
    std::unique_ptr<SignalSelector>       _remoteSignalSelector;
    DashboardView                         _view;

    Dashboard* _dashboard = nullptr;

    // modal dialog state for new plot creation
    std::string _sinkForNewPlot;

    // dialog state for the change label popup
    std::size_t _propertyControlWindowID;

    struct LegendItemClickResult {
        bool        shouldOpenEnterViewOnlyModeModal = false;
        bool        shouldOpenNewPlotModal           = false;
        std::string sinkForNewPlot;
    };

    void drawNewPlotModal();
    void drawBarLeading(LegendItemClickResult& clickResult) noexcept;
    void drawBarTrailing(Mode mode, LegendItemClickResult& clickResult) noexcept;
    void drawAddSignalButton(ImVec2 size) noexcept;
    void drawToolbarLayoutButtons(float plotButtonSize) noexcept;
    void addSelectedRemoteSignal(const SignalData& selectedRemoteSignal) noexcept;

    [[nodiscard]] LegendItemClickResult drawChartsLegendAndEditPane(Mode mode, std::vector<std::size_t>& windowRemoveList);

    enum class PropertyControlWindowContextMenuAction {
        None,
        OpenChangeLabelPopup,
    };

    struct PropertyControlWindowsDrawParams {
        std::size_t               windowId;
        std::vector<std::size_t>& removeList;
    };

    struct AddPropertyControlWindowsParams {
        DockSpace::Windows&                                         output;
        std::function<void(PropertyControlWindowContextMenuAction)> onContextMenuAction;
        std::vector<std::size_t>&                                   removeList;
    };

    void addPropertyControlWindows(const AddPropertyControlWindowsParams& params);
    void drawChangeLabelPopup();

    [[nodiscard]] PropertyControlWindowContextMenuAction drawPropertyControlWindow(const PropertyControlWindowsDrawParams& params);
    [[nodiscard]] PropertyControlWindowContextMenuAction drawPropertyControlWindowContextMenu(const PropertyControlWindowsDrawParams& params);

public:
    DashboardPage();
    ~DashboardPage();

    void draw(Mode mode = Mode::View) noexcept;

    void setRequestViewOnlyModeHandler(std::function<void()>&& function) { _requestViewOnlyMode = std::move(function); }
    void setRequestSetLayoutModeHandler(std::function<void(bool)>&& function) { _requestSetLayoutMode = std::move(function); }

    std::pair<DockingLayoutType, gr::property_map> saveLayoutConfiguration() const;

    void newUIBlock(std::string_view chartType = "XYChart", std::string_view initialSignal = {});

    void setDashboard(Dashboard& dashboard) {
        _remoteSignalSelector.reset();
        _dashboard = std::addressof(dashboard);
    }
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
