#ifndef DASHBOARDPAGE_H
#define DASHBOARDPAGE_H

#include <deque>
#include <optional>
#include <stack>
#include <string>
#include <unordered_map>

#include "Dashboard.hpp"
#include "DashboardView.hpp"
#include "charts/Chart.hpp"
#include "charts/SinkRegistry.hpp" // For SinkRegistry listener
#include "common/ImguiWrap.hpp"
#include "components/Block.hpp"
#include "components/Docking.hpp"
#include "components/GlobalSignalLegend.hpp"
#include "components/SignalSelector.hpp"

#include <memory>

namespace DigitizerUi {

class DashboardPage {
public:
    using Mode = opendigitizer::charts::ChartMode;

private:
    static constexpr const char* addChartPopupID                      = "New Chart";
    static constexpr const char* enterViewOnlyModePopupID             = "Set dashboard to locked mode?##lockMode";
    static constexpr const char* currentPropertiesPopupID             = "##Current properties";
    static constexpr const char* changeLabelPopupID                   = "##Change property control window label";
    static constexpr const char* addExportedPropertyPopupID           = "Add another exported property";
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
    std::unique_ptr<SignalSelector>       _remoteSignalSelector;
    DashboardView                         _view;

    Dashboard* _dashboard = nullptr;

    // modal dialog state for new plot creation
    std::string _sinkForNewPlot;

    // dialog state for the currently exported properties popup
    std::size_t _propertyControlWindowID;

    struct LegendItemClickResult {
        bool        shouldOpenEnterViewOnlyModeModal = false;
        bool        shouldOpenNewPlotModal           = false;
        std::string sinkForNewPlot;
    };

    void drawNewPlotModal(); // modifies _showNewPlotModal if close is requested
    void drawBarLeading(LegendItemClickResult& clickResult) noexcept;
    void drawBarTrailing(Mode mode, LegendItemClickResult& clickResult) noexcept;
    void drawToolbarLayoutButtons(float plotButtonSize) noexcept;
    void addSelectedRemoteSignal(const SignalData& selectedRemoteSignal) noexcept;
    void doViewModeOverlayArea() noexcept;

    struct ExportedPropertyPairsByWindowID;

    [[nodiscard]] LegendItemClickResult drawChartsLegendAndEditPane(Mode mode, const ExportedPropertyPairsByWindowID& propertyPairsByWindowID, std::vector<std::size_t>& windowRemoveList);
    void                                applyControlPanelWindowAction(const components::BlockControlsPanelResult& controlPanelAction, const ExportedPropertyPairsByWindowID& pairs, std::vector<std::size_t>& windowRemoveList);

    using PropertyPairSpan = std::span<const components::ExportedPropertyPair>;

    [[nodiscard]] ExportedPropertyPairsByWindowID getExportedPropertyPairsByWindowID() const noexcept;

    struct ExportedPropertyPairsByWindowID {
    private:
        std::unordered_map<std::size_t, std::vector<components::ExportedPropertyPair>> values;

    public:
        PropertyPairSpan getForWindow(std::size_t id) const noexcept;

        friend ExportedPropertyPairsByWindowID DashboardPage::getExportedPropertyPairsByWindowID() const noexcept;
    };

    enum class PropertyControlWindowContextMenuAction {
        None,
        OpenDisconnectCurrentPropertiesPopup,
        OpenChangeLabelPopup,
    };

    struct PropertyControlWindowsDrawParams {
        std::size_t                       windowId;
        Dashboard::PropertyControlWindow& controlWindow;
        PropertyPairSpan                  properties;
        IMW::WidgetSize                   editWidgetSize;
        std::vector<std::size_t>&         removeList;
    };

    struct AddPropertyControlWindowsParams {
        DockSpace::Windows&                                         output;
        const ExportedPropertyPairsByWindowID&                      pairs;
        std::function<void(PropertyControlWindowContextMenuAction)> onContextMenuAction;
        std::vector<std::size_t>&                                   removeList;
    };

    void propertyControlWindowEditProperties(const PropertyControlWindowsDrawParams& params) const;
    void addPropertyControlWindows(const AddPropertyControlWindowsParams& params);
    void drawCurrentPropertiesPopup(const ExportedPropertyPairsByWindowID& pairs);
    void drawChangeLabelPopup();

    [[nodiscard]] PropertyControlWindowContextMenuAction drawPropertyControlWindow(const PropertyControlWindowsDrawParams& params);
    [[nodiscard]] PropertyControlWindowContextMenuAction drawPropertyControlWindowContextMenu(const PropertyControlWindowsDrawParams& params);

public:
    DashboardPage();
    ~DashboardPage();

    void draw(Mode mode = Mode::View) noexcept;

    void setRequestViewOnlyModeHandler(std::function<void()>&& function) { _requestViewOnlyMode = std::move(function); }
    void setRequestSetLayoutModeHandler(std::function<void(bool)>&& function) { _requestSetLayoutMode = std::move(function); }

    void                                           setLayoutConfiguration(DockingLayoutType type, std::optional<gr::property_map> freeLayoutDescription);
    std::pair<DockingLayoutType, gr::property_map> saveLayoutConfiguration() const;

    /* no optional of ref yet */ DigitizerUi::Dashboard::UIWindow* newUIBlock(std::string_view chartType = "XYChart", std::string_view initialSignal = {});

    void setDashboard(Dashboard& dashboard) {
        _remoteSignalSelector.reset();
        _dashboard = std::addressof(dashboard);
    }
};

} // namespace DigitizerUi

#endif // DASHBOARDPAGE_H
