#ifndef OPENDIGITIZER_CHARTS_CHARTINTERFACE_HPP
#define OPENDIGITIZER_CHARTS_CHARTINTERFACE_HPP

#include "ChartUtils.hpp"
#include "SignalSink.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace opendigitizer::charts {

/**
 * @brief Callback for drag & drop operations.
 *
 * @param droppedData Pointer to the dropped payload data
 * @param dataSize Size of the dropped payload
 * @param payloadType The ImGui payload type string
 */
using DropCallback = std::function<void(const void* droppedData, std::size_t dataSize, const char* payloadType)>;

/**
 * @brief Callback for setting up a drag source from a chart legend item.
 *
 * Called by charts when a legend item should become a drag source.
 * The callback should set up the ImGui drag payload using ImGui::SetDragDropPayload().
 *
 * @param sinkUniqueName The unique name of the sink being dragged
 * @param sourceChartId The unique ID of the source chart (for removal by drop target)
 */
using DragSourceCallback = std::function<void(std::string_view sinkUniqueName, std::string_view sourceChartId)>;

/**
 * @brief Abstract interface for chart blocks.
 *
 * This interface provides polymorphic access to chart-specific methods.
 * Chart blocks (XYChart, YYChart, etc.) inherit from both gr::Block<T> and ChartInterface.
 *
 * The inheritance scheme:
 *   - Concrete charts (e.g., XYChart) : gr::Block<XYChart, ...>, ChartInterface
 *   - gr::BlockWrapper<XYChart> wraps the block for type-erased BlockModel access
 *   - ChartInterface provides chart-specific polymorphic methods
 *
 * This design allows:
 *   - Type-erased block storage via gr::BlockModel/BlockWrapper<T>
 *   - Polymorphic chart method access via ChartInterface*
 *   - New chart types without modifying Dashboard or other code
 */
struct ChartInterface {
    virtual ~ChartInterface() = default;

    // --- Signal sink management ---

    /**
     * @brief Add a signal sink to this chart (shared ownership).
     */
    virtual void addSignalSink(std::shared_ptr<SignalSink> sink) = 0;

    /**
     * @brief Remove a signal sink by name.
     */
    virtual void removeSignalSink(std::string_view uniqueName) = 0;

    /**
     * @brief Remove all signal sinks from this chart.
     */
    virtual void clearSignalSinks() = 0;

    /**
     * @brief Get the number of signal sinks.
     */
    [[nodiscard]] virtual std::size_t signalSinkCount() const noexcept = 0;

    /**
     * @brief Get read-only access to signal sinks.
     */
    [[nodiscard]] virtual const std::vector<std::shared_ptr<SignalSink>>& signalSinks() const noexcept = 0;

    // --- Chart identity ---

    /**
     * @brief Get the chart type name (e.g., "XYChart", "YYChart").
     */
    [[nodiscard]] virtual std::string_view chartTypeName() const noexcept = 0;

    /**
     * @brief Get the unique ID for this chart instance.
     */
    [[nodiscard]] virtual std::string_view uniqueId() const noexcept = 0;

    // --- Dashboard integration ---

    /**
     * @brief Set dashboard axis configuration.
     */
    virtual void setDashboardAxisConfig(std::vector<DashboardAxisConfig> configs) = 0;

    /**
     * @brief Clear dashboard axis configuration.
     */
    virtual void clearDashboardAxisConfig() = 0;

    /**
     * @brief Build axis categories from signal sinks.
     */
    virtual void buildAxisCategories() = 0;

    /**
     * @brief Setup all ImPlot axes (called before drawing).
     */
    virtual void setupAllAxes() = 0;

    // --- Rendering ---

    /**
     * @brief Draw chart content within an already-opened ImPlot context.
     *
     * This method renders the chart's signals AFTER ImPlot::BeginPlot()
     * and BEFORE ImPlot::EndPlot(). The caller is responsible for
     * BeginPlot/EndPlot and any wrapper logic (like TouchHandler).
     *
     * For charts that handle their own full rendering (BeginPlot/EndPlot),
     * this method may do nothing or call the internal rendering logic.
     */
    virtual void drawContent() = 0;

    /**
     * @brief Check if this chart handles its own ImPlot Begin/End.
     *
     * If true, the chart's draw() method should be called directly
     * without wrapping in BeginPlot/EndPlot.
     *
     * @return true if chart manages its own ImPlot context
     */
    [[nodiscard]] virtual bool handlesOwnPlotContext() const noexcept { return false; }

    // --- Drag & Drop ---

    /**
     * @brief Set the callback for drag & drop operations.
     *
     * Charts that handle their own ImPlot context should call this callback
     * when a payload is dropped on the plot area.
     *
     * @param callback Function to call when a drop occurs
     * @param payloadType The ImGui payload type to accept (e.g., "DND_SOURCE")
     */
    virtual void setDropCallback(DropCallback callback, const char* payloadType) = 0;

    /**
     * @brief Clear the drop callback.
     */
    virtual void clearDropCallback() = 0;

    /**
     * @brief Set the callback for setting up drag sources from legend items.
     *
     * Charts that handle their own ImPlot context should call this callback
     * when a legend item starts being dragged, so the caller can set up
     * the appropriate drag payload.
     *
     * @param callback Function to call when a legend item drag starts
     */
    virtual void setDragSourceCallback(DragSourceCallback callback) = 0;

    /**
     * @brief Clear the drag source callback.
     */
    virtual void clearDragSourceCallback() = 0;
};

} // namespace opendigitizer::charts

#endif // OPENDIGITIZER_CHARTS_CHARTINTERFACE_HPP
