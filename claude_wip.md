# Work In Progress: Chart Abstraction Layer Refactoring

## Issue Reference

- **GitHub Issue**: [#250 - Refactoring the plotting/charting system](https://github.com/fair-acc/opendigitizer/issues/250)
- **GR4 PR**: [#697 - Drawable UI and UICategory documentation](https://github.com/fair-acc/gnuradio4/pull/697)
- **Branch**: `250_refactorChartPlotAPI`

---

## Design Goals (from Issue #250 + GR4 Drawable UI)

### 1. Charts as GR4 Blocks
- Charts inherit from both `gr::Block<T, ...>` and `Chart` mixin
- Charts implement `draw(const property_map& config)` for full rendering (BeginPlot → content → EndPlot)
- Charts are self-contained UI blocks handling their own D&D, context menus, interactions
- Note: `ChartPane` will become `Content` in future GR4 versions

### 2. Clean Separation of Concerns
- **Dashboard/DashboardPage**: Layout management only, iterates `gr::BlockModel*` blocks, calls `draw(config)`
- **Chart blocks**: Self-contained rendering, signal sink management, user interactions
- **SignalSink**: Data access interface for sink blocks (kept as-is)

### 3. Leverage GR4 Core APIs
- `BlockModel::uiConstraints()` for layout info and axis config
- `BlockModel::draw(const property_map& config)` for rendering
- `BlockModel::uiCategory()` for filtering drawable blocks
- `gr::Graph` for storing chart blocks
- Block settings via `data_sinks` property for signal sources

### 4. Composability
- New chart types inherit from `gr::Block<T, ...>` and `Chart` mixin
- Dashboard doesn't need modification for new chart types
- Signal sinks can be shared across multiple charts
- Chart transmutation: convert type while preserving signals

### 5. Threading Model (from GR4 docs)
```
Scheduler Thread(s)                     UI Render Thread
┌─────────────────────┐                 ┌─────────────────────┐
│ processBulk/One()   │ ──mutex/────>   │ draw(config)        │
│ (data flow)         │   lock-free     │ (presentation)      │
└─────────────────────┘                 └─────────────────────┘
```
Thread safety is block's responsibility (use mutex or lock-free buffers).

---

## What's Achieved

### SignalSink Interface
- [x] SignalSink abstract interface with metadata (name, unit, color, rate)
- [x] History management with timeout-based capacity expiration
- [x] Range retrieval (`getXRange`, `getTagRange`)
- [x] SinkAdapter<T> for wrapping sink blocks
- [x] SinkRegistry for global sink lookup
- [x] Thread-safe data access via `acquireDataLock()`

### Chart Blocks
- [x] XYChart: Multiple Y signals vs common X-axis
- [x] YYChart: Correlation plots (Y1 vs Y2)
- [x] Charts inherit `gr::Block<T, BlockingIO<false>, Drawable<ChartPane, "ImGui">>` and `Chart` mixin
- [x] Charts implement `draw(const property_map& config)`
- [x] Charts use `TouchHandler<>::BeginZoomablePlot()` / `EndZoomablePlot()` for touch support
- [x] `data_sinks` block property for signal sources
- [x] `settingsChanged()` syncs sinks when `data_sinks` changes

### D&D Integration
- [x] DndHelper for self-contained D&D handling in charts
- [x] Charts handle D&D in their `handleInteractions()` method
- [x] Legend↔Chart, Chart↔Chart transfers working
- [x] `g_findChartById` callback for cross-chart operations
- [x] DnD uses `name()` consistently for sink lookup

### Tooltip Integration
- [x] `tooltip::formatAxisValue()` and `tooltip::showPlotMouseTooltip()` in Chart.hpp (namespace `opendigitizer::charts::tooltip`)
- [x] XYChart and YYChart call `tooltip::showPlotMouseTooltip()` in draw()
- [x] Shows axis values when mouse hovers stationary over plot

### Dashboard/UI Graph
- [x] `Dashboard::m_uiGraph` stores chart blocks
- [x] `Dashboard::emplaceChartBlock()` creates charts via `m_uiGraph.emplaceBlock<T>()`
- [x] `Plot::chartBlock` (BlockModel*) for type-erased access
- [x] Axis config stored in `blockModel->uiConstraints()["axes"]` at load time
- [x] Charts read axis config from `uiConstraints()` in `draw()`

---

## What's Remaining

### Step 3: Dashboard Uses `uiConstraints()` for Layout ✅ DONE
- [x] Dashboard sets `block->uiConstraints()["axes"]` after block creation
- [x] Charts read axis config from `uiConstraints()["axes"]` in `draw()`
- [x] **Legacy rendering code removed from DashboardPage** (`drawPlot()`, static axis maps)
- [x] **Window position/size stored in `uiConstraints()["window"]`** at block creation/load

### Step 4: Dashboard Iterates UI Graph for Rendering ✅ DONE
- [x] DashboardPage render loop now only uses chart-based path
- [x] `blockPtr->draw(drawConfig)` called for all charts
- [x] Window info now available in `uiConstraints()["window"]` for future graph iteration
- [x] Chart-type-agnostic: DashboardPage has no XYChart/YYChart references
- [x] **`drawPlots()` iterates `uiGraph().blocks()` filtered by `uiCategory() == ChartPane`**
- [x] **`Dashboard::findPlotByBlock()` maps blocks to Plot for window/layout info**
- [x] **uiGraph is now the source of truth for rendering; Plot provides layout metadata**

### Step 5: Chart Transmutation ✅ DONE
- [x] `transmuteChart()` removes old block, adds new block to UI Graph
- [x] Transfer `_signalSinks` from old chart to new chart via `data_sinks` property
- [x] Preserve layout via `uiConstraints()`
- [x] Dashboard has `transmuteChart(Plot&, std::string_view newChartType)` method
- [x] Deferred transmutation: requests stored and processed at start of next frame (avoids modifying chart during draw)
- [x] Context menu "Change Chart Type" in both XYChart and YYChart
- [x] Fixed deadlock: sinks transferred only via `data_sinks` property, not manually added (prevented duplicate locks)

### Header Consolidation ✅ DONE
- [x] Deleted ChartInterface.hpp (was just a redirect)
- [x] Deleted ChartUtils.hpp (merged into Chart.hpp)
- [x] Deleted SignalSink.cpp (empty placeholder)
- [x] Chart.hpp now contains: enums, axis utilities, tags namespace, tooltip namespace, DndPayload, Chart mixin, DndHelper

### Step 6: Replace Plot with UIWindow (IN PROGRESS)
- [x] Added `UIWindow` struct - generic window-to-block mapping for all UI elements
- [x] Added `m_uiWindows` storage in Dashboard
- [x] Added `findUIWindow()`, `findUIWindowByName()`, `getOrCreateUIWindow()`, `removeUIWindow()` methods
- [x] `drawPlots()` now uses `getOrCreateUIWindow()` with lazy creation
- [x] Added `transmuteUIWindow()` for UIWindow-based chart transmutation
- [x] Updated `g_findChartById` callback to search UIWindows first
- [x] Updated `processPendingTransmutation()` to use UIWindow-based transmutation
- [x] **`save()` updated to iterate UIWindows instead of m_plots**
- [x] **`doLoad()` creates UIWindows instead of populating m_plots**
- [x] **Added `newChart()` method returning UIWindow& (new API)**
- [x] **`newPlot()` is now a legacy wrapper calling `newChart()`**
- [x] **Added `deleteChart()` method for UIWindow-based deletion**
- [x] **`deletePlot()` now also cleans up corresponding UIWindow**
- [x] **`removeSinkFromPlots()` now also updates UIWindows**
- [x] **Removed `Chart*` pointer from `UIWindow`** - sink management via settings interface only
- [x] **Added `sinkNames()` / `setSinkNames()` helpers to UIWindow** (temporary, marked deprecated)
- [x] **Updated `g_removeSinkFromChart` callback** to use settings interface instead of Chart*
- [x] **Changed `transmuteUIWindow()` to use `sinkNames()` instead of `Chart::signalSinks()`**
- [x] **Added lazy sink sync in `draw()`** - UI blocks aren't scheduled, so settings aren't auto-applied
- [x] **Restored `invokeWork()` calls in chart `draw()`** - Required because ImPlotSink::work() skips processing when tab is visible, expecting draw() to consume data via invokeWork()
- [ ] Migrate remaining callers from `newPlot()` to `newUIBlock()`
- [ ] Remove Plot struct, m_plots, and related legacy methods (final cleanup)

### Step 7: ImPlotSink Refactoring (PLANNED)

**Background**: The current `ImPlotSink` implementation has several issues:
- Overcomplicated inheritance via `ImPlotSinkBase<..>` alias
- Uses `gr::BlockingIO<false>` which is obsolete and wasn't needed
- Uses `processOne()` with `atomic_flag` instead of `processBulk()` with proper mutex
- `isTabVisible()` workaround that doesn't achieve its intended purpose
- Charts must call `invokeWork()` because scheduler work is effectively disabled

**Reference**: GR4 PR [#697 - Drawable UI and UICategory documentation](https://github.com/fair-acc/gnuradio4/pull/697)

**Recommended Pattern** (from GR4 docs):
```cpp
struct PlotSink : gr::Block<PlotSink<T>, gr::Drawable<gr::UICategory::ChartPane, "ImGui">> {
    mutable std::mutex _dataMutex;  // guards data between processBulk() and draw()

    gr::work::Result processBulk(auto& input) noexcept {
        std::lock_guard lock(_dataMutex);
        // write to internal buffers
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        std::lock_guard lock(_dataMutex);
        // read from internal buffers, render
    }
};
```

**Migration Steps** (small, testable changes):

#### Phase 1: ImPlotSink Cleanup
- [ ] Remove `ImPlotSinkBase` alias - use direct inheritance
- [ ] Remove `BlockingIO<false>` from inheritance chain
- [ ] Remove `isTabVisible()` early return in `work()`
- [ ] Keep `ImPlotSink::draw()` for legacy/backward compatibility

#### Phase 2: Thread Safety Improvements
- [ ] Replace `atomic_flag` pattern with proper `std::mutex` (`_dataMutex` already exists)
- [ ] Switch from `processOne()` to `processBulk()` for lock efficiency
- [ ] `processBulk()` acquires `_dataMutex` while writing to `_xValues`/`_yValues`
- [ ] Data getters continue to require caller to hold lock via `acquireDataLock()`

#### Phase 3: Chart Updates
- [ ] Remove `invokeWork()` calls from XYChart/YYChart `draw()` methods
- [ ] Charts acquire lock guard per sink at start of draw via `acquireDataLock()`
- [ ] Lock released automatically after data copied to ImGui/ImPlot GPU buffers

**Note**: `ChartPane` will be kept until user refactors to `Pane` in a later PR.

---

### Step 8: Deprecation Roadmap (DOCUMENTED)
The following are marked with `TODO(deprecated)` comments for future removal:

1. **`UIWindow::sinkNames()` / `UIWindow::setSinkNames()`** (Dashboard.hpp:130-155)
   - Chart-specific code that shouldn't be in Dashboard
   - Exists temporarily for backward compatibility with 'sources:' in .grc files
   - Remove when charts define `data_sinks` directly in `parameters:` section

2. **'sources:' → 'data_sinks' transfer in `doLoad()`** (Dashboard.cpp:382-385)
   - Transfers 'sources:' section to chart's `data_sinks` property
   - Remove when .grc files migrate to defining sinks in `parameters:` section

3. **`loadUIWindowSources()` and `removeSinkFromPlots()`** (Dashboard.cpp)
   - Sink management helpers in Dashboard
   - Remove when sink management is fully encapsulated in charts

4. **Legacy `Plot` struct and `m_plots`** (Dashboard.hpp/cpp)
   - To be removed after migrating all callers to UIWindow API

5. **`invokeWork()` calls in charts** (XYChart.hpp, YYChart.hpp)
   - Remove after ImPlotSink refactoring (Step 7) is complete
   - Scheduler will handle data consumption; charts just read via SignalSink interface

### Future Considerations
- [ ] Evaluate localized axis settings in chart vs structured property_map conversion
- [ ] Remove `loadPlotSources()` once data_sinks property fully handles sink loading
- [ ] **Refactor chart transmutation callbacks**: Currently `g_requestChartTransmutation` and `g_findChartById` are global callbacks set by DashboardPage, which introduces chart-specific dependencies back to DashboardPage. Consider moving to:
  - A proper observer/listener pattern where charts can emit events
  - Passing a context/callback interface to charts via `draw()` config
  - Using gr::Block messaging infrastructure for UI events

---

## Chart Inheritance Pattern (CURRENT)

Simple mixin approach: Charts inherit from both `gr::Block<T>` and `Chart` mixin.

```cpp
// Mixin providing signal sink storage and management
struct Chart {
    virtual ~Chart() = default;

protected:
    std::vector<std::shared_ptr<SignalSink>> _signalSinks;

public:
    // Signal sink management
    void addSignalSink(std::shared_ptr<SignalSink> sink);
    void removeSignalSink(std::string_view name);
    void clearSignalSinks();
    [[nodiscard]] std::size_t signalSinkCount() const noexcept;
    [[nodiscard]] const std::vector<std::shared_ptr<SignalSink>>& signalSinks() const noexcept;

    // Sync sinks from data_sinks property
    void syncSinksFromNames(const std::vector<std::string>& sinkNames);
    [[nodiscard]] std::vector<std::string> getSinkNames() const;

    // Identity - must be provided by concrete chart
    [[nodiscard]] virtual std::string_view chartTypeName() const noexcept = 0;
    [[nodiscard]] virtual std::string_view uniqueId() const noexcept = 0;
};

// Concrete chart - inherits both gr::Block and Chart mixin
struct XYChart : gr::Block<XYChart, gr::BlockingIO<false>,
                           gr::Drawable<gr::UICategory::ChartPane, "ImGui">>,
                 Chart {
    // Block settings
    A<std::string, "chart name", gr::Visible> chart_name;
    A<std::vector<std::string>, "data sinks", gr::Visible> data_sinks = {};
    // ... other settings

    GR_MAKE_REFLECTABLE(XYChart, chart_name, data_sinks, ...);

    // Settings change handler - syncs sinks when data_sinks changes
    void settingsChanged(const gr::property_map&, const gr::property_map& newSettings) {
        if (newSettings.contains("data_sinks")) {
            syncSinksFromNames(data_sinks.value);
        }
    }

    // Identity
    [[nodiscard]] std::string_view chartTypeName() const noexcept override { return "XYChart"; }
    [[nodiscard]] std::string_view uniqueId() const noexcept override { return this->unique_name; }

    // Drawable interface - full draw implementation
    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        // Read axis config from uiConstraints()["axes"]
        // BeginZoomablePlot → setup axes → render signals → EndZoomablePlot
    }
};
```

**Benefits:**
- Simple, direct inheritance - no CRTP complexity
- `Chart*` for polymorphic access via `dynamic_cast`
- Signal sink storage and management in `Chart` mixin
- Block settings (`data_sinks`) drive sink loading via `settingsChanged()`
- Axis config in `uiConstraints()` - UI-specific, not serialized as block settings
- Chart reads what it needs from `uiConstraints()` in `draw()`

**Usage from Dashboard:**
```cpp
// At load time:
auto [blockPtr, chartPtr] = emplaceChartBlock(chartType, name, params);
blockPtr->uiConstraints()["axes"] = axesConfig;  // Set axis config

// At render time:
if (plot.hasChart()) {
    plot.chartBlock->draw(drawConfig);  // Chart reads from uiConstraints()
}
```

---

## Files Summary

### Core Chart Files
- `src/ui/charts/Chart.hpp` - Chart mixin with signal sink management
- `src/ui/charts/XYChart.hpp` - XY signal chart
- `src/ui/charts/YYChart.hpp` - Correlation chart
- `src/ui/charts/SignalSink.hpp` - Data access interface
- `src/ui/charts/SinkRegistry.hpp` - Global sink lookup

### Dashboard Files
- `src/ui/Dashboard.hpp` - Contains `m_uiGraph`, `Plot` struct
- `src/ui/Dashboard.cpp` - Chart creation via `emplaceChartBlock()`
- `src/ui/DashboardPage.hpp` - Render loop, layout
- `src/ui/DashboardPage.cpp` - Simplified render loop (legacy code removed)

### Supporting Files
- `src/ui/common/TouchHandler.hpp` - `BeginZoomablePlot()` / `EndZoomablePlot()`
- `src/ui/components/SignalLegend.hpp` - Global signal legend

### Test Files
- `src/ui/test/qa_ChartAbstraction.cpp` - Chart and UI Graph tests
- `src/ui/test/qa_SignalSink.cpp` - SignalSink interface tests

---

## Build & Test Commands

```bash
# Build
cmake --build build --target opendigitizer

# Run tests
./build/src/ui/test/qa_SignalSink           # SignalSink interface tests
./build/src/ui/test/qa_ChartAbstraction     # Chart and UI Graph tests

# Run application
./build/src/service/opendigitizer
```

---

## Migration Protocol

1. Make changes for one step
2. Build and run tests
3. Run application to verify UI visually
4. Signal **DONE** and wait for user's **CONTINUE** before next step
5. Fix any regressions before proceeding

---

## Notes

- Current GR4 version uses `UICategory::ChartPane`, future versions will use `UICategory::Content`
- `uiConstraints()` is available in `BlockModel` for layout hints and axis config
- Each chart implements full `draw()` - no default implementation in base class
- Keep `chartTypeName()` for backward compatibility with `.grc` configuration files
- Axis config stored as structured pmtv in `uiConstraints()["axes"]` for backward-compatible .grc format
