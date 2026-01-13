# Work In Progress: Chart Abstraction Layer Integration

## Issue Reference

GitHub Issue #250: Refactoring the plotting/charting system for opendigitizer
https://github.com/fair-acc/opendigitizer/issues/250

**Branch**: `250_refactorChartPlotAPI`

---

## Current Status: API CLEANUP IN PROGRESS

**Completed Phases:**

- ✅ PHASE 1: Basic chart abstraction (SignalSink, SinkWrapper, XYChart, YYChart)
- ✅ PHASE 2: Manager consolidation (eliminated ImPlotSinkManager, SinkRegistry only)
- ✅ PHASE 3: Simplified SignalSink architecture (pure interface + non-owning adapter)
- ✅ Basic D&D: Legend ↔ Chart (visibility toggle, add/remove signals)

**Current Work: Priority 1 - Consolidate to Single Sink List**

The immediate goal is to eliminate the dual sink list problem:

- `plot.plotSinkBlocks` (Dashboard's list) - TO BE REMOVED
- `plot.chart->_signalSinks` (Chart's internal list) - SINGLE SOURCE OF TRUTH

**Key Architecture Decisions (Session 8):**

1. **Single source of truth**: `chart->_signalSinks` is the authoritative list
2. **D&D payload**: `shared_ptr<SignalSink>` + `bool* accepted` flag (source handles own removal)
3. **SignalLegend component**: Standalone struct in `src/ui/components/SignalLegend.hpp`
4. **Chart ownership**: Charts created by YAML loading or "create new chart" button (DashboardPage triggers)
5. **SinkRegistry role**: Global catalog of all available sinks (independent of UI), used by SignalLegend
6. **Sinks can be in multiple charts**: A sink can exist in multiple charts simultaneously
7. **Complete cutover**: No backward compatibility period, migrate fully to new API

**D&D Strategy (Clarified):**

```cpp
struct DndPayload {
    std::shared_ptr<SignalSink> sink;  // The sink being dragged
    bool* accepted;                     // Source sets address, target sets *accepted = true
};
```

**D&D Flow:**

| Action                | Source Behavior                           | Target Behavior                             |
|-----------------------|-------------------------------------------|---------------------------------------------|
| **Legend → Chart**    | Legend: no removal needed                 | Chart: adds sink, sets `*accepted = true`   |
| **Chart A → Chart B** | Chart A: if `_dragAccepted`, removes sink | Chart B: adds sink, sets `*accepted = true` |
| **Chart A → Legend**  | Chart A: if `_dragAccepted`, removes sink | Legend: sets `*accepted = true` (no add)    |

**Source (Chart) Implementation:**

1. `BeginDragDropSourceItem()` succeeds → store `_draggingSink`, set `_dragAccepted = false`
2. Create payload with `sink = _draggingSink`, `accepted = &_dragAccepted`
3. After `EndDragDropSource()`: if `_dragAccepted == true`, call `removeSignalSink(_draggingSink)`

**Source (Legend) Implementation:**

- No removal logic needed - legend displays all sinks from SinkRegistry

**Implementation Summary (Sessions 1-7):**

1. **SignalSink as pure interface** - Does NOT extend BlockModel (GR4 blocks can't have virtual inheritance)
2. **SinkAdapter<T>** - Non-owning adapter that holds pointer to block, implements SignalSink by delegation
3. **Removed ImPlotSinkModel/ImPlotSinkWrapper** - Replaced with SinkAdapter<ImPlotSink<T>>
4. **Self-registration** - ImPlotSink registers SinkAdapter with SinkRegistry in settingsChanged()
5. **Updated tests** - Test sinks are now local implementations in test files (not GR4 blocks)

---

## Quick Start for New Session

```bash
# View current changes
git status
git diff HEAD

# Build and test
cd cmake-build-debug-gcc15
cmake --build . --target opendigitizer-ui qa_SignalSink qa_ChartAbstraction

# Run tests
./src/ui/test/qa_SignalSink
./src/ui/test/qa_ChartAbstraction

# Run the app to verify UI
./src/ui/opendigitizer-ui
```

**Current UI State**: All charts render correctly using chart-based rendering.
UI is stable - ready for PHASE 3 refactoring.

---

## Design Decisions (MUST FOLLOW)

### 1. SignalSink extends gr::BlockModel

**Decision**: `SignalSink` must extend `gr::BlockModel` (virtual inheritance). This eliminates duplicate methods
(`uniqueName()`, `name()`, `settings()`, etc.) and leverages existing GR4 infrastructure.

- Remove `getSinkBlockModel()`, `blockName()`, `uniqueName()` from SignalSink (use BlockModel's)
- SignalSink only contains sink-specific data access methods

### 2. Single SinkWrapper<T> Template

**Decision**: Use `SinkWrapper<T>` (which extends `gr::BlockWrapper<T>`) for ALL sink blocks.

- Eliminate `ImPlotSinkModel` intermediate class
- Eliminate `ImPlotSinkWrapper<T>` - use `SinkWrapper<ImPlotSink<T>>` instead
- UI-specific methods (`drawTags()`, `invokeWork()`, etc.) stay on `ImPlotSink<T>` block, accessed via `blockRef()`

### 3. Namespace Structure

**Decision**:

- `opendigitizer::SignalSink` - base interface (in root namespace, used by non-chart UI elements too)
- `opendigitizer::SinkWrapper<T>` - wrapper template
- `opendigitizer::charts::XYChart`, `opendigitizer::charts::YYChart` - chart implementations

### 4. Test Sinks

**Decision**: Test sinks are proper GR4 blocks (not standalone classes).

- Create minimal `TestStreamingSink<T>` and `TestDataSetSink<T>` blocks
- Remove standalone `StreamingSignalSink<T>` and `DataSetSignalSink<T>` from DataSinks.hpp
- Test blocks wrapped with `SinkWrapper<T>` like any other sink

### 5. Registration

**Decision**: Sinks register themselves with SinkRegistry in `settingsChanged()` (option A).

- ImPlotSink creates `std::make_shared<SinkWrapper<ImPlotSink<T>>>(...)` and registers
- Unregisters in destructor

### 6. Data Access Pattern

**Decision**: Use indexed access (`size()`, `xAt(i)`, `yAt(i)`) + `PlotData` struct with ImPlot-compatible getter.

### 7. Implementation Principles

- **"nomen-est-omen"** (name-is-meaning) - drop verbose documentation
- Keep code **concise/terse**
- Use mutex-protected `HistoryBuffer<T>` for thread-safety
- Retain `BlockingIO<false>` annotation (required for `invokeWork()`)

---

## Target Architecture (IMPLEMENTED)

```
opendigitizer::SignalSink (pure abstract interface - NO BlockModel inheritance)
    - name(), uniqueName() (block identity)
    - signalName(), color(), sampleRate() (sink identity)
    - size(), xAt(), yAt(), plotData() (data access)
    - hasDataSets(), dataSetCount(), dataSets()
    - timeFirst(), timeLast()
    - bufferCapacity(), requestCapacity(), releaseCapacity()
    - acquireDataLock()
    - invokeWork(), draw() (work/UI processing)
    - signalQuantity/Unit, abscissaQuantity/Unit, signalMin/Max
    - Tag support methods

opendigitizer::SinkAdapter<T> : public SignalSink
    - Non-owning adapter (holds T* to existing block)
    - Implements SignalSink by delegating to block's methods
    - Block owns data, adapter provides polymorphic access
    - Used for registration with SinkRegistry

Concrete GR4 sink blocks (following ImChartMonitor pattern):
    - ImPlotSink<T> : ImPlotSinkBase<...> (Drawable, BlockingIO<false>)
      - Owns HistoryBuffer for data storage
      - Has draw(), processBulk(), settingsChanged()
      - Creates SinkAdapter<ImPlotSink<T>> in settingsChanged()
      - Registers adapter with SinkRegistry
```

---

## SignalSink Interface (IMPLEMENTED)

**Final Design**: SignalSink is a pure abstract interface (does NOT extend BlockModel).
This avoids GR4 virtual inheritance issues while providing polymorphic access to sink blocks.

```cpp
namespace opendigitizer {

class SignalSink {  // Pure interface - NO BlockModel inheritance
public:
    virtual ~SignalSink() = default;

    // --- Block identity (delegated to underlying block) ---
    [[nodiscard]] virtual std::string_view name() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view uniqueName() const noexcept = 0;

    // --- Sink-specific identity ---
    [[nodiscard]] virtual std::string_view signalName() const noexcept  = 0;
    [[nodiscard]] virtual std::uint32_t    color() const noexcept       = 0;
    [[nodiscard]] virtual float            sampleRate() const noexcept  = 0;

    // --- Indexed data access ---
    [[nodiscard]] virtual std::size_t size() const noexcept        = 0;
    [[nodiscard]] virtual double      xAt(std::size_t index) const = 0;
    [[nodiscard]] virtual float       yAt(std::size_t index) const = 0;
    [[nodiscard]] virtual PlotData plotData() const = 0;

    // --- DataSet support ---
    [[nodiscard]] virtual bool                                hasDataSets() const noexcept  = 0;
    [[nodiscard]] virtual std::size_t                         dataSetCount() const noexcept = 0;
    [[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const              = 0;

    // --- Tag support ---
    [[nodiscard]] virtual bool                      hasStreamingTags() const noexcept = 0;
    [[nodiscard]] virtual std::pair<double, double> tagTimeRange() const noexcept     = 0;
    virtual void forEachTag(std::function<void(double, const gr::property_map&)> cb) const = 0;

    // --- Time range ---
    [[nodiscard]] virtual double timeFirst() const noexcept = 0;
    [[nodiscard]] virtual double timeLast() const noexcept  = 0;

    // --- Buffer control ---
    [[nodiscard]] virtual std::size_t bufferCapacity() const noexcept                       = 0;
    virtual void                      requestCapacity(void* consumer, std::size_t capacity) = 0;
    virtual void                      releaseCapacity(void* consumer)                       = 0;

    // --- Thread-safe data access ---
    [[nodiscard]] virtual std::unique_lock<std::mutex> acquireDataLock() const = 0;

    // --- Work processing ---
    virtual gr::work::Status invokeWork() = 0;
    virtual gr::work::Status draw(const gr::property_map& config = {}) = 0;

    // --- Axis metadata ---
    [[nodiscard]] virtual std::string_view signalQuantity() const noexcept   = 0;
    [[nodiscard]] virtual std::string_view signalUnit() const noexcept       = 0;
    [[nodiscard]] virtual std::string_view abscissaQuantity() const noexcept = 0;
    [[nodiscard]] virtual std::string_view abscissaUnit() const noexcept     = 0;
    [[nodiscard]] virtual float            signalMin() const noexcept        = 0;
    [[nodiscard]] virtual float            signalMax() const noexcept        = 0;
};

} // namespace opendigitizer
```

**Key design decisions:**

- Extends `gr::BlockModel` via virtual inheritance
- Removed: `getSinkBlockModel()`, `uniqueName()`, `blockName()` (use BlockModel's)
- Namespace: `opendigitizer::` (not `sinks::` or `charts::`)

## PlotData Struct (IMPLEMENTED in DataSinks.hpp)

```cpp
struct PlotPoint {
    double x;
    double y;
};

using PlotGetter = PlotPoint (*)(int idx, void* userData);

struct PlotData {
    PlotGetter getter;
    void* userData;
    int count;

    [[nodiscard]] bool empty() const noexcept { return count <= 0 || getter == nullptr; }
};

// Usage with ImPlot:
auto pd = sink->plotData();
if (!pd.empty()) {
    ImPlot::PlotLineG(label, reinterpret_cast<ImPlotGetter>(pd.getter), pd.userData, pd.count);
}
```

---

## Current State vs Target

### SignalSink Interface Methods

| Target Spec         | Current Implementation | Status        |
|---------------------|------------------------|---------------|
| `uniqueName()`      | `uniqueName()`         | ✅ Implemented |
| `blockName()`       | `blockName()`          | ✅ Implemented |
| `signalName()`      | `signalName()`         | ✅ Implemented |
| `color()`           | `color()`              | ✅ Implemented |
| `sampleRate()`      | `sampleRate()`         | ✅ Implemented |
| `size()`            | `size()`               | ✅ Implemented |
| `xAt(i)`            | `xAt(i)`               | ✅ Implemented |
| `yAt(i)`            | `yAt(i)`               | ✅ Implemented |
| `plotData()`        | `plotData()`           | ✅ Implemented |
| `hasDataSets()`     | `hasDataSets()`        | ✅ Implemented |
| `dataSetCount()`    | `dataSetCount()`       | ✅ Implemented |
| `dataSets()`        | `dataSets()`           | ✅ Implemented |
| `timeFirst()`       | `timeFirst()`          | ✅ Implemented |
| `timeLast()`        | `timeLast()`           | ✅ Implemented |
| `bufferCapacity()`  | `bufferCapacity()`     | ✅ Implemented |
| `requestCapacity()` | `requestCapacity()`    | ✅ Implemented |
| `releaseCapacity()` | `releaseCapacity()`    | ✅ Implemented |

### Deprecated Aliases - REMOVED

The deprecated aliases (`sinkUniqueName`, `signalColor`, `dataSize`) have been **removed** from the codebase.
All code now uses the standard methods: `uniqueName()`, `color()`, `size()`.

### Missing Components - ALL COMPLETE

- [x] `blockName()` method in SignalSink ✅ Added
- [x] `SignalSinkManager` migration ✅ Complete - SinkRegistry is the single source of truth
- [x] `isVisible` removal ✅ Complete - feature removed as it was inconsistent with chart-based rendering

---

## Completed Work

### Session 1 (Previous - crashed)

1. Created directory structure `src/ui/charts/`
2. Created `SignalSink.hpp` with basic interface
3. Created `ChartInterface.hpp` with chart abstraction
4. Created `XYChart.hpp` implementation
5. Created `YYChart.hpp` for correlation plots
6. Added `Charts.hpp` factory functions
7. Updated `Dashboard.hpp` with chart storage in `Plot` struct
8. Added headless unit tests `qa_SignalSink`, `qa_ChartAbstraction`

### Session 2

1. **Fixed YYChart rendering** - Center Lissajous chart now works
   - Added `invokeWork()` call before drawing
   - Changed from failed `dynamic_cast<BlockModel*>` to direct `drawContent()` call
   - Location: `DashboardPage.cpp:691-702`

2. **Disabled XYChart chart-based rendering** - Bottom chart works via legacy path
   - Chart-based rendering had issues with multi-axis and Time scale
   - Reverted to `plotSinkBlock->draw()` for all XYChart plots
   - Location: `DashboardPage.cpp:432-435`

3. **Added `drawTags()` method** to `ImPlotSinkModel`
   - For future use when chart-based rendering is fixed
   - Location: `ImPlotSink.hpp:235-245` (interface), `ImPlotSink.hpp:289-302` (implementation)

4. **Code cleanup**
   - Moved formatter functions to `axisUtils::` namespace in `ChartUtils.hpp`
   - Simplified `buildLabel()` to use `AxisCategory::buildLabel()`
   - Changed `chartHandlesOwnRendering` from string check to `handlesOwnPlotContext()`

### Session 3

1. **Aligned SignalSink interface to target spec**
   - Renamed methods: `sinkUniqueName()` → `uniqueName()`, `signalColor()` → `color()`, `dataSize()` → `size()`
   - Added all missing methods: `plotData()`, `hasDataSets()`, `dataSetCount()`, `dataSets()`, `timeFirst()`,
     `timeLast()`
   - Added deprecated aliases for backward compatibility

2. **Updated all implementations**
   - Updated `SignalSink.hpp` with new interface + SinkWrapper implementation
   - Updated `DataSinks.hpp` - StreamingSignalSink and DataSetSignalSink
   - Updated `ImPlotSink.hpp` - ImPlotSinkModel and SinkWrapper

3. **Updated all call sites**
   - `XYChart.hpp` - uses new method names
   - `YYChart.hpp` - uses new method names
   - `ChartUtils.hpp` - uses new method names
   - `SinkRegistry.hpp` - uses new method names
   - `DashboardPage.cpp` - uses new method names
   - `Dashboard.cpp` - uses new method names
   - `qa_SignalSink.cpp` - test file updated
   - `qa_ChartAbstraction.cpp` - test file updated

### Session 4

1. **Fixed headless test builds**
   - Removed unnecessary `#include "ChartUtils.hpp"` from `DataSinks.hpp` (was pulling in ImGui dependency)
   - Tests now build without ImGui/ImPlot libraries

2. **Cleaned up debug output**
   - Removed `fmt/format.h` includes and `fmt::print` calls from XYChart.hpp and DashboardPage.cpp
   - Debug logging was preventing builds since fmt is not linked in all targets

3. **Chart-based rendering path prepared**
   - Set `useChartBasedRendering = false` in `DashboardPage.cpp:434`
   - Legacy `plotSinkBlock->draw()` path remains active
   - Ready to enable when XYChart::drawContent() bugs are fixed

4. **Build status**
   - ✅ `qa_SignalSink`: 48 asserts in 12 tests pass
   - ✅ `qa_ChartAbstraction`: 49 asserts in 13 tests pass
   - ✅ `opendigitizer-uilib` builds successfully
   - ❌ `opendigitizer` (main app) - fails due to missing Picoscope SDK (external dependency)

### Session 5 (Current)

1. **Added diagnostic logging for chart-based rendering**
   - Enabled chart-based rendering with `useChartBasedRendering = true`
   - Added `std::print` debug output every ~5 seconds showing:
      - `plot.name`
      - `plotSinkBlocks.size()` (legacy path sink count)
      - `chartSinks.size()` (chart-based path sink count)
      - For each chart sink: `uniqueName()` and `size()`
   - Location: `DashboardPage.cpp:434-456`

2. **Analysis of rendering flow**
   - YYChart works because `handlesOwnPlotContext() = true` - creates own BeginPlot/EndPlot
   - XYChart uses `handlesOwnPlotContext() = false` (default) - expects caller to set up ImPlot
   - DashboardPage calls `TouchHandler<>::BeginZoomablePlot()` before `drawPlot()` for XYChart
   - XYChart::drawContent() calls drawSignals() which uses chart's internal `_signalSinks`

3. **Suspected issue areas**
   - Chart's `_signalSinks` populated via `addChartSinkRef()` → verified correct
   - `xAt()`/`yAt()` in SinkWrapper delegate to `block->_xValues`/`_yValues` → verified correct
   - Axis groups populated via `buildAxisCategoriesWithFallback()` → needs runtime verification

4. **Build status**
   - ✅ `qa_SignalSink`: 48 asserts in 12 tests pass
   - ✅ `qa_ChartAbstraction`: 49 asserts in 13 tests pass
   - ✅ `opendigitizer-uilib` builds successfully

5. **Fixed axis setup regression**
   - **Root cause**: Chart-based axis setup was used even when chart's `_signalSinks` was empty
   - When chart sinks are empty, `buildAxisCategoriesWithFallback()` creates no categories
   - `setupChartAllAxes()` then sets up no ImPlot axes → nothing renders
   - **Fix**: Check `!plot.chart->signalSinks().empty()` before using chart-based axis setup
   - Falls back to legacy `assignSourcesToAxes()` + `setupPlotAxes()` when chart sinks empty
   - Location: `DashboardPage.cpp:396-414`

6. **Build status**
   - ✅ `qa_SignalSink`: 48 asserts in 12 tests pass
   - ✅ `qa_ChartAbstraction`: 49 asserts in 13 tests pass
   - ✅ `opendigitizer-uilib` builds successfully

7. **Fixed axis scale mismatch bug** (Session continuation)
   - **Root cause**: Dashboard configured X-axis with `scale: Time` (expects Unix timestamps)
   - ImPlot axis was correctly set up with `ImPlotScale_Time`
   - BUT `_xCategories[0]->scale` remained at default `Linear`
   - In `drawStreamingSignal()`: `xAxisScale = _xCategories[0]->scale` (Linear)
   - With Linear scale, data was transformed: `xVal = xVal - xMin` → range 0 to ~2
   - But Time axis expected Unix timestamps (~1.77 billion) → data off-screen!
   - **Fix**: In `setupAxes()`, update `_xCategories[i]->scale` from dashboard config
   - Location: `XYChart.hpp:238-241` and `XYChart.hpp:255-258`
   - Now scale flows: dashboard config → `_dashboardAxisConfig` → `_xCategories[i]->scale` → `drawStreamingSignal()`

8. **Implemented DataSet sink support**
    - **Problem**: FFT Spectrum plot showed `size=0` because `SinkWrapper::size()` returned 0 for DataSet sinks
    - **Fix**: Updated `SinkWrapper` in `ImPlotSink.hpp` to handle DataSet sinks:
        - `size()` returns `ds.axis_values[0].size()` (x-axis point count)
        - `xAt(i)` returns `ds.axis_values[0][i]`
        - `yAt(i)` returns `ds.signalValues(dataset_index)[i]`
        - `plotData()` provides getter for DataSet indexed access
    - **Note**: DataSets use absolute x values (e.g., frequency), no transformation needed

9. **Added separate DataSet rendering path**
    - **Problem**: `drawStreamingSignal()` applied x transformation that's wrong for DataSets
    - **Fix**: Added `drawDataSetSignal()` in `XYChart.hpp` that:
        - Checks `sink->hasDataSets()` to detect DataSet type
        - Uses absolute x values (no transformation)
    - DataSets represent spectral/domain data, not time series

10. **Fixed duplicate tag rendering**
    - **Problem**: Tags were drawn for all sinks in chart-based path (legacy draws only first)
    - **Fix**: Only draw tags for first sink in `DashboardPage.cpp:462-465`
    - Matches legacy behavior where `drawTag = false` after first sink

11. **Fixed LinearReverse scale handling**
    - **Problem**: LinearReverse uses `xVal - xMax` (not `xMin`) for relative x
    - **Fix**: Updated `drawStreamingSignal()` to handle all scale types:
        - `Time`: absolute timestamps (no transform)
        - `LinearReverse`: `xVal - xMax` (0 to negative range)
        - `Linear`, `Log10`, `SymLog`: `xVal - xMin` (0 to positive range)

### Session 6

1. **Added `blockName()` to SignalSink interface**
    - Returns the block's user-visible name (vs `uniqueName()` which is system-generated unique ID)
    - Implemented in all SignalSink implementations:
        - `SignalSink.hpp` - interface + SinkWrapper
        - `DataSinks.hpp` - StreamingSignalSink, DataSetSignalSink (returns `uniqueName()`)
        - `ImPlotSink.hpp` - ImPlotSinkModel (delegates to `name()`)

2. **Implemented dual registration in ImPlotSinkManager**
    - When ImPlotSink blocks register with ImPlotSinkManager, they also register with SinkRegistry
    - Uses non-owning `shared_ptr` with no-op deleter for SinkRegistry registration
    - Location: `ImPlotSink.hpp` `registerPlotSink()`

3. **Migrated Dashboard.cpp to use SinkRegistry**
    - `forEach` at line 466 now uses `SinkRegistry::forEach()` with `blockName()` and `color()`
    - Other usages remain on ImPlotSinkManager (need ImPlotSinkModel-specific methods)

4. **Migrated DashboardPage.cpp listener to use SinkRegistry**
    - `addListener()` in constructor uses `SinkRegistry::addListener()`
    - `removeListener()` in destructor uses `SinkRegistry::removeListener()`
    - Callback uses SignalSink interface methods: `signalName()`, `uniqueName()`

5. **Implemented DataSet history rendering in XYChart**
   - Added `DataSetPlotContext` struct for direct DataSet access in plot callbacks
   - Updated `drawDataSetSignal()` to render multiple DataSets with fading opacity:
      - Gets all DataSets via `sink.dataSets()`
      - Draws from oldest to newest (newest renders on top)
      - Opacity fades from 1.0 (newest) to 0.2 (oldest)
      - Only newest DataSet shows in legend (others prefixed with `##`)
   - Respects `max_history_count` setting (default 3)
   - Location: `XYChart.hpp:338-390`

6. **Moved tag rendering to XYChart**
   - Added tag access methods to SignalSink interface:
      - `hasStreamingTags()` - check if sink has streaming tags
      - `tagTimeRange()` - get min/max timestamp of tags
      - `forEachTag(callback)` - iterate over tags with callback
   - Added tag rendering helpers to `ChartUtils.hpp` (`tags::` namespace):
      - `drawStreamingTags()` - render streaming tags with vertical lines + labels
      - `drawDataSetTimingEvents()` - render DataSet timing_events
   - XYChart now handles all tag rendering in `drawSignals()`
   - Removed tag rendering from DashboardPage.cpp (chart-based path)

### Session 7 (Current)

1. **Completed shared ownership model** ✅
   - **ImPlotSinkManager**: Changed `_knownSinks` from `unique_ptr` to `shared_ptr`
   - **Dual ownership**: Both ImPlotSinkManager and SinkRegistry now share ownership of sinks
   - **Charts share ownership**: Charts (via ChartInterface) store `vector<shared_ptr<SignalSink>>`
   - Location: `ImPlotSink.hpp:270` and `ImPlotSink.hpp:534-544`

2. **Updated loadPlotSourcesFor() for proper ownership**
   - Now uses SinkRegistry to get shared_ptr (proper ownership)
   - Falls back to ImPlotSinkManager for legacy path
   - Charts get proper shared ownership (not non-owning wrappers)
   - Location: `Dashboard.cpp:605-644`

3. **Build status**
   - ✅ `qa_SignalSink`: 48 asserts in 12 tests pass
   - ✅ `qa_ChartAbstraction`: 49 asserts in 13 tests pass
   - ✅ `opendigitizer-ui` builds and links successfully

4. **Cleaned up legacy ownership code**
   - Removed `addChartSinkRef()` method from `Dashboard.hpp` Plot struct
   - Removed `_sinkAdapters` member (non-owning shared_ptr storage)
   - Simplified `clearChartSinks()` to just call `chart->clearSignalSinks()`
   - Removed fallback path in `loadPlotSourcesFor()` (no longer needed)
   - Location: `Dashboard.hpp:119-127`, `Dashboard.cpp`

5. **Eliminated ImPlotSinkManager - SinkRegistry is the single source of truth** ✅
   - Removed deprecated aliases from SignalSink: `sinkUniqueName()`, `signalColor()`, `dataSize()`
   - Renamed SinkWrapper methods: `signalColor()` → `color()`, `dataSize()` → `size()`
   - Moved `SinkWrapper` out of ImPlotSinkManager → `ImPlotSinkWrapper` standalone template
   - Updated ImPlotSink to register directly with SinkRegistry (no intermediate manager)
   - Removed ImPlotSinkManager class entirely
   - Updated all usages in Dashboard.cpp, DashboardPage.cpp, and test files to use SinkRegistry

6. **Removed `isVisible` feature** ✅
   - Removed `isVisible` member from `ImPlotSinkModel` (ImPlotSink.hpp:220)
   - Removed visibility check from legacy rendering path (DashboardPage.cpp:459)
   - Removed visibility toggle from global legend (DashboardPage.cpp:833-839)
   - Reason: Feature was inconsistent - chart-based rendering (XYChart) didn't check visibility,
     so the toggle had no effect on most charts

7. **Legacy rendering path analysis**
   - The `plotSinkBlock->draw()` callback at DashboardPage.cpp:487 is still used as fallback
   - Runs when chart's `signalSinks()` is empty (during initial loading before sinks are connected)
   - Chart-based rendering path (lines 439-453) handles all rendering when chart has sinks
   - Legacy path is necessary to maintain backwards compatibility during load transitions

---

## Known Issues / Bugs

### ~~XYChart::drawContent() Does Not Work~~ FIXED

**Symptom**: When chart-based rendering is enabled, bottom chart shows no data
**Root cause**: Axis scale mismatch between ImPlot axis setup and data transformation

The dashboard YAML specifies `scale: Time` for X-axis:

```yaml
axes:
  - axis: X
    scale: Time
```

This caused:

1. `axis::setupAxis()` set ImPlot axis to `ImPlotScale_Time` (expects Unix timestamps)
2. But `_xCategories[0]->scale` stayed at default `Linear`
3. `drawStreamingSignal()` read `Linear` scale and transformed data: `x = x - xMin`
4. Data range became 0-2 while axis expected ~1.77 billion → nothing visible

**Fix**: Update category scale from dashboard config in `setupAxes()`:

```cpp
if (_xCategories[i].has_value()) {
    _xCategories[i]->scale = scale;  // From dashboard config
}
```

**Status**: ✅ Fixed - chart-based rendering now enabled

### ~~Signal Sink Synchronization~~ ✅ FIXED

**Issue**: `plot.plotSinkBlocks` (ImPlotSinkModel*) and `plot.chart->signalSinks()` (shared_ptr<SignalSink>) should
point to same objects but may get out of sync.

**Fix**: `loadPlotSourcesFor()` now uses SinkRegistry to get proper `shared_ptr<SignalSink>`:

```cpp
auto sinkSharedPtr = SinkRegistry::instance().findSink([&name](const auto& sink) {
    return sink.signalName() == name || sink.blockName() == name;
});
if (sinkSharedPtr) {
    // Proper shared ownership - chart and SinkRegistry both own the sink
    plot.chart->addSignalSink(sinkSharedPtr);
    // For legacy path, get raw pointer from shared_ptr
    auto* imPlotSink = dynamic_cast<ImPlotSinkModel*>(sinkSharedPtr.get());
    if (imPlotSink) plot.plotSinkBlocks.push_back(imPlotSink);
}
```

**Status**: Ownership model now ensures charts share ownership with SinkRegistry, preventing dangling pointers.

---

## File Locations

### Core Files (PHASE 3 changes noted)

- `/src/ui/charts/SignalSink.hpp` - SignalSink interface
   - **PHASE 3**: Move to `opendigitizer::`, extend `virtual gr::BlockModel`
- `/src/ui/charts/DataSinks.hpp` - PlotPoint, PlotData structs
   - **PHASE 3**: Remove standalone sink classes, keep only structs
- `/src/ui/charts/ChartInterface.hpp` - Chart base interface
- `/src/ui/charts/XYChart.hpp` - Time-series chart
- `/src/ui/charts/YYChart.hpp` - Correlation chart
- `/src/ui/charts/Charts.hpp` - Factory functions
- `/src/ui/charts/ChartUtils.hpp` - Axis utilities, formatters
- `/src/ui/charts/SinkRegistry.hpp` - Sink registry

### Integration Files

- `/src/ui/blocks/ImPlotSink.hpp` - ImPlotSink<T> block
   - **PHASE 3**: Remove ImPlotSinkModel, ImPlotSinkWrapper; use SinkWrapper<ImPlotSink<T>>
- `/src/ui/Dashboard.hpp` - Plot struct with chart storage
- `/src/ui/Dashboard.cpp` - Chart creation, sink loading
- `/src/ui/DashboardPage.cpp` - Rendering dispatch

### Test Files

- `/src/ui/test/qa_SignalSink.cpp`
   - **PHASE 3**: Update to use new test sink blocks
- `/src/ui/test/qa_ChartAbstraction.cpp`
   - **PHASE 3**: Update to use new test sink blocks

### New Files (PHASE 3)

- `/src/ui/blocks/TestSinks.hpp` - Minimal test sink blocks (TestStreamingSink, TestDataSetSink)

### Sample Dashboard

- `/src/ui/assets/sampleDashboards/DemoDashboard.grc` - Contains YYChart example

---

## What's Working (Completed)

- ✅ XYChart rendering with streaming data and DataSets
- ✅ YYChart correlation plots (Y1 vs Y2)
- ✅ Visibility toggle (`drawEnabled()`) on legend items
- ✅ D&D from global legend to chart (add signal)
- ✅ D&D from chart legend to chart (move signal)
- ✅ D&D from chart to legend (remove signal)
- ✅ Tag/timing event rendering
- ✅ DataSet history with fading opacity
- ✅ Axis auto-grouping by unit/quantity
- ✅ Multiple X-axis modes (UTC, relative, sample index)
- ✅ Chart legend hidden in View mode, visible in Layout mode

---

## What's Still Missing (from Issue #250)

### SignalSink Enhancements

- [ ] `signal_min`/`signal_max` bounds tracking
- [ ] `sample_rate` field tracking
- [ ] `time_first`/`time_last` timestamps
- [ ] `total_sample_count`
- [ ] `min_history` mechanics with per-chart size requirements and timeouts
- [ ] Range retrieval: `getX(t_min, t_max)`, `getY(t_min, t_max)`, `getTags(t_min, t_max)`

### Chart Features

- [ ] Context menus for adjusting signal plot styles (line/scatter/bars)
- [ ] Context menus for moving signals to different Y-axis
- [ ] Ridgeline plots
- [ ] Waterfall plots
- [ ] Error bar/whisker support
- [ ] Custom annotated plots

### Dashboard Features

- [ ] Chart transmutation (convert XYChart ↔ YYChart preserving signals)
- [ ] Chart copying with shared sinks
- [ ] "Create new chart" button with type selector

### Data Handling

- [ ] Signal synchronization for correlation plots (YYChart)
- [ ] Multi-rate signal handling

### API Cleanup

- [ ] Eliminate `plot.plotSinkBlocks` dual list
- [ ] Remove chart/plot-specific logic from DashboardPage
- [ ] All sink APIs use `shared_ptr<SignalSink>` (no name lookups)
- [ ] Migrate ImPlotSink to single SignalSink path

---

## Next Steps (Priority Order)

### Priority 1: Consolidate to Single Sink List (IMMEDIATE)

**Goal**: `chart->_signalSinks` becomes the single source of truth. Eliminate `plot.plotSinkBlocks`.
No chart/plot-specific logic in DashboardPage. All sink APIs use `shared_ptr<SignalSink>`.

#### P1.1: Remove `plotSinkBlocks` from `Dashboard::Plot`

- [ ] Remove `std::vector<SignalSink*> plotSinkBlocks` member from `Dashboard.hpp`
- [ ] Update all code that reads `plotSinkBlocks` to use `plot.chart->signalSinks()` instead
- [ ] Update all code that writes to `plotSinkBlocks` to use `plot.chart->addSignalSink()`/`removeSignalSink()`

#### P1.2: Create `DndPayload` struct with acceptance flag

- [ ] Define `DndPayload { shared_ptr<SignalSink> sink; bool* accepted; }`
- [ ] Replace `DndItem` with `DndPayload` throughout codebase
- [ ] Remove name-based SinkRegistry lookups in drop callbacks
- [ ] Document void* casting contract for ImGui payload

#### P1.3: Implement source-side removal logic in Charts

- [ ] Add `_draggingSink` and `_dragAccepted` members to XYChart/YYChart
- [ ] On `BeginDragDropSourceItem()` success: store sink, set `_dragAccepted = false`
- [ ] Create payload with `accepted = &_dragAccepted`
- [ ] After `EndDragDropSource()`: if `_dragAccepted`, call `removeSignalSink(_draggingSink)`
- [ ] Charts handle their own `BeginDragDropTargetPlot()` + set `*payload.accepted = true`

#### P1.4: Create `SignalLegend` component

- [ ] Create `src/ui/components/SignalLegend.hpp` as standalone struct
- [ ] `draw()` method renders legend items from SinkRegistry
- [ ] D&D source: iterates sinks, sets up drag sources (no removal needed)
- [ ] D&D target: accepts drops, sets `*payload.accepted = true` (sink removed by source chart)
- [ ] SinkRegistry remains independent global class (no UI dependency)

#### P1.5: Clean up DashboardPage

- [ ] Remove D&D handling logic from DashboardPage
- [ ] DashboardPage calls `SignalLegend::draw()` and `chart->drawContent()`
- [ ] DashboardPage only orchestrates layout
- [ ] Signal sink APIs use `shared_ptr<SignalSink>` throughout

---

### Priority 2: Migrate ImPlotSink to Single SignalSink Path (TOP PRIORITY AFTER P1)

**Goal**: Eliminate legacy `ImPlotSinkModel` path. All sinks use the `SignalSink` interface.

#### P2.1: Consolidate ImPlotSink to SignalSink

- [ ] Remove `ImPlotSinkModel` class
- [ ] `ImPlotSink<T>` directly implements/provides SignalSink interface
- [ ] UI-specific methods (`drawTags()`, `invokeWork()`) stay on `ImPlotSink<T>`
- [ ] Access via `SinkAdapter<ImPlotSink<T>>` or direct interface

#### P2.2: Update rendering paths

- [ ] Remove legacy `plotSinkBlock->draw()` callback path in DashboardPage
- [ ] All rendering goes through chart's `drawContent()` method
- [ ] Charts iterate `_signalSinks` (shared_ptr<SignalSink>)

#### P2.3: Clean up SinkRegistry

- [ ] SinkRegistry stores `shared_ptr<SignalSink>` only
- [ ] Remove any ImPlotSinkModel-specific code
- [ ] Ensure proper registration/unregistration lifecycle

---

### Priority 3: SignalSink History Management (AFTER P2)

**Goal**: Implement `min_history` mechanics per issue #250 spec.

#### P3.1: Add history management to SignalSink interface

- [ ] `signal_min`/`signal_max` bounds tracking
- [ ] `sample_rate` tracking
- [ ] `time_first`/`time_last` timestamps
- [ ] `total_sample_count`

#### P3.2: Implement `min_history` mechanics

- [ ] `MinHistoryEntry { size, last_update, timeout }` struct
- [ ] `std::vector<MinHistoryEntry> min_history` per sink
- [ ] Auto-resize history buffer based on max of min_history sizes
- [ ] Expire entries that haven't been updated within timeout

#### P3.3: Range retrieval methods

- [ ] `getX(t_min, t_max)` - return x values in time range
- [ ] `getY(t_min, t_max)` - return y values in time range
- [ ] `getTags(t_min, t_max)` - return tags in time range
- [ ] Consider lazy evaluation via ranges

---

### Priority 4: Chart Type Selection UI (AFTER P3)

**Goal**: Allow users to select chart type and transmute existing charts.

#### P4.1: Chart creation UI

- [ ] "Create new chart" button in layout view shows chart type selector
- [ ] Support XYChart, YYChart (and future types)
- [ ] New chart created via Dashboard/Chart factory

#### P4.2: Chart transmutation

- [ ] "Change chart type" option in chart context menu
- [ ] `transmuteChart(oldChart, newChartType)` preserves signal sinks
- [ ] Move sinks to new chart, replace old chart in Dashboard

#### P4.3: YAML support

- [ ] Chart type stored in dashboard YAML
- [ ] Load correct chart type from YAML

---

### Priority 5: Additional Features (AFTER P4)

#### P5.1: Context menus

- [ ] Right-click on signal → adjust plot style (line/scatter/bars)
- [ ] Right-click on signal → move to different Y-axis
- [ ] Right-click on chart → configure settings

#### P5.2: Additional chart types

- [ ] Ridgeline plots
- [ ] Waterfall plots
- [ ] Custom annotated plots

#### P5.3: Error bar support

- [ ] Add error/uncertainty fields to SignalSink
- [ ] Render error bars/whiskers in charts

---

### DEPRECATED: PHASE 3: SignalSink extends BlockModel

**Note**: This was superseded by the above priority list. The key insight is that SignalSink
should remain a pure interface (NOT extend BlockModel) to avoid GR4 virtual inheritance issues.
Use `SinkAdapter<T>` pattern instead.

~~Major refactoring to simplify architecture by having SignalSink extend gr::BlockModel.~~

#### Action Item 1: Refactor SignalSink.hpp

- [ ] Move to `opendigitizer::` namespace (from `sinks::`)
- [ ] Make `SignalSink` extend `virtual gr::BlockModel`
- [ ] Remove `getSinkBlockModel()` - no longer needed
- [ ] Remove `uniqueName()` - use BlockModel's
- [ ] Remove `blockName()` - use BlockModel's `name()`
- [ ] Keep only sink-specific methods (signalName, color, sampleRate, data access, etc.)

#### Action Item 2: Refactor SinkWrapper<T>

- [ ] Move to `opendigitizer::` namespace
- [ ] Change inheritance: `SinkWrapper<T> : public gr::BlockWrapper<T>, public SignalSink`
- [ ] Diamond inheritance resolved via virtual (BlockWrapper→BlockModel, SignalSink→BlockModel)
- [ ] Implement SignalSink methods by delegating to `this->blockRef()`
- [ ] BlockModel methods inherited from BlockWrapper (no duplication)

#### Action Item 3: Eliminate ImPlotSinkModel

- [ ] Remove `ImPlotSinkModel` class entirely
- [ ] UI-specific methods stay on `ImPlotSink<T>` block:
   - `drawTags(AxisScale)` - accessed via `blockRef().drawTags()`
   - `invokeWorkWithGuard()` - accessed via `blockRef().invokeWorkWithGuard()`
   - `setSignalName(string)` - accessed via `blockRef().signal_name = ...`
- [ ] Update all call sites that used `dynamic_cast<ImPlotSinkModel*>`

#### Action Item 4: Eliminate ImPlotSinkWrapper<T>

- [ ] Remove `ImPlotSinkWrapper<T>` template
- [ ] ImPlotSink<T> creates `SinkWrapper<ImPlotSink<T>>` directly in `settingsChanged()`
- [ ] Verify all SignalSink methods can be satisfied via blockRef() delegation

#### Action Item 5: Update DataSinks.hpp

- [ ] Remove standalone `StreamingSignalSink<T>` class
- [ ] Remove standalone `DataSetSignalSink<T>` class
- [ ] Keep `PlotPoint`, `PlotData`, `PlotGetter` structs
- [ ] Create minimal test blocks (see Action Item 6)

#### Action Item 6: Create Test Sink Blocks

- [ ] Create `TestStreamingSink<T> : gr::Block<TestStreamingSink<T>, ...>`
   - Minimal processBulk() (can be empty for UI tests)
   - Mock data interface for pushing test data
   - Wrapped with `SinkWrapper<TestStreamingSink<T>>`
- [ ] Create `TestDataSetSink<T> : gr::Block<TestDataSetSink<T>, ...>`
   - Minimal processBulk()
   - Mock data interface for pushing test DataSets
   - Wrapped with `SinkWrapper<TestDataSetSink<T>>`

#### Action Item 7: Update SinkRegistry

- [ ] Stores `shared_ptr<SignalSink>` (SignalSink is now a BlockModel)
- [ ] Update any methods that assumed SignalSink was not a BlockModel
- [ ] Verify registration/unregistration still works

#### Action Item 8: Update Call Sites

- [ ] Dashboard.cpp - update sink lookups and usage
- [ ] DashboardPage.cpp - update legend, drag-drop, rendering
- [ ] XYChart.hpp - update SignalSink usage
- [ ] YYChart.hpp - update SignalSink usage
- [ ] ChartUtils.hpp - update any SignalSink usage
- [ ] Test files - update to use new test sink blocks

#### Action Item 9: Update Namespaces

- [ ] `opendigitizer::SignalSink` (was `sinks::SignalSink`)
- [ ] `opendigitizer::SinkWrapper<T>` (was `sinks::SinkWrapper`)
- [ ] `opendigitizer::charts::XYChart` (keep in charts)
- [ ] `opendigitizer::charts::YYChart` (keep in charts)
- [ ] Remove backward compatibility aliases if no longer needed

#### Action Item 10: Verify and Test

- [ ] Build successfully
- [ ] Run qa_SignalSink tests (update as needed)
- [ ] Run qa_ChartAbstraction tests (update as needed)
- [ ] Run opendigitizer-ui and verify:
   - Charts render correctly
   - Drag-drop works
   - Legend displays correctly
   - No regressions

---

### Completed Work

#### ~~PHASE 1: Basic Chart Abstraction~~ ✅ DONE

- Created SignalSink interface
- Created SinkWrapper<T> template
- Created XYChart and YYChart
- Implemented chart-based rendering

#### ~~PHASE 2: Manager Consolidation~~ ✅ DONE

- Eliminated ImPlotSinkManager
- SinkRegistry is single source of truth
- Removed isVisible feature
- Removed deprecated aliases

---

## Testing Commands

```bash
# Build tests
cd cmake-build-debug-gcc15
cmake --build . --target qa_SignalSink qa_ChartAbstraction

# Run tests
./src/ui/test/qa_SignalSink
./src/ui/test/qa_ChartAbstraction

# Build and run UI
cmake --build . --target opendigitizer
./src/service/opendigitizer
```

---

## Git Commands

```bash
# View uncommitted changes
git diff HEAD

# View change summary
git diff HEAD --stat

# Commit PHASE 3 progress
git add -A
git commit -m "refactor(sinks): SignalSink extends BlockModel

- SignalSink now extends virtual gr::BlockModel
- Eliminated ImPlotSinkModel and ImPlotSinkWrapper
- Use SinkWrapper<T> for all sink blocks
- Moved to opendigitizer:: namespace
- Created minimal test sink blocks"
```

---

## Contact / Context

This refactoring is for the opendigitizer project, a signal visualization tool built on:

- **GR4** (GNU Radio 4) for signal processing
- **ImGui/ImPlot** for UI rendering
- **OpenCMW** for data transport

The goal is to separate signal data management from rendering to enable:

- Headless unit testing
- Shared sinks across multiple charts
- Future alternative rendering backends
