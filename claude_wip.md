# Work In Progress: Chart Abstraction Layer Integration

## Issue Reference

GitHub Issue #250: Refactoring the plotting/charting system for opendigitizer

## Overview

This work implements a new chart abstraction layer that separates signal data management from rendering, enabling:

- Headless unit testing of chart logic
- Shared signal sinks across multiple charts
- Type-erased access to signal data via `SignalSinkBase`
- ImPlot-compatible callback-based rendering API
- Future-proof architecture for alternative rendering backends

## Design Decisions (from Q&A)

### 1. Replace vs Coexist with ImPlotSink

**Decision**: Replace ImPlotSink's public interface, but retain the same naming/config/settings layout for GRC yaml
script
compatibility.

### 2. Rendering Responsibility

**Decision**: Charts handle rendering using data from sinks. `draw()` method remains optional in ImPlotSink for backward
compatibility (part of GR4 Drawable NTTP).

### 3. Sink Types (Streaming vs DataSet)

**Decision**: One unified `SignalSinkBase` interface. Both streaming and DataSet behaviors are detected at compile-time
via `if constexpr` in `SinkWrapper`.

### 4. Data Access Pattern

**Decision**: Use indexed access (`size()`, `xAt(i)`, `yAt(i)`) instead of span-based API. Added `PlotData` struct with
getter callback compatible with ImPlot's `ImPlotGetter` pattern.

### 5. Manager Integration

**Decision**: Single `SignalSinkManager` replacing `ImPlotSinkManager`. During transition, dual registration is used -
`ImPlotSinkManager::registerPlotSink()` also registers with `SignalSinkManager`.

### 6. Implementation Principles

- Drop verbose documentation, follow "nomen-est-omen" (name-is-meaning) principle
- Keep code concise/terse
- Use mutex-protected `HistoryBuffer<T>` for thread-safety
- Retain `BlockingIO<false>` annotation (required for `invokeWork()` to work)

## Architecture

```
SignalSinkBase (type-erased interface with indexed access + PlotData)
    ^
    |
+---+---+
|       |
StreamingSignalSink<T>   DataSetSignalSink<T>
(standalone implementations for testing)

ImPlotSinkModel : SignalSinkBase
    ^
    |
SinkWrapper<TBlock> : ImPlotSinkModel
(wraps ImPlotSink<T> blocks)
```

## Simplified SignalSinkBase Interface

The new interface provides:

```cpp
// Identity
[[nodiscard]] virtual std::string_view uniqueName() const noexcept = 0;
[[nodiscard]] virtual std::string_view signalName() const noexcept = 0;

// Minimal metadata
[[nodiscard]] virtual std::uint32_t color() const noexcept = 0;
[[nodiscard]] virtual float sampleRate() const noexcept = 0;

// Indexed data access (primary API for rendering)
[[nodiscard]] virtual std::size_t size() const noexcept = 0;
[[nodiscard]] virtual double xAt(std::size_t i) const = 0;
[[nodiscard]] virtual float yAt(std::size_t i) const = 0;

// ImPlot-compatible data accessor
[[nodiscard]] virtual PlotData plotData() const = 0;

// DataSet support (for non-streaming signals)
[[nodiscard]] virtual bool hasDataSets() const noexcept = 0;
[[nodiscard]] virtual std::size_t dataSetCount() const noexcept = 0;
[[nodiscard]] virtual std::span<const gr::DataSet<float>> dataSets() const = 0;

// Time range
[[nodiscard]] virtual double timeFirst() const noexcept = 0;
[[nodiscard]] virtual double timeLast() const noexcept = 0;

// Buffer control
[[nodiscard]] virtual std::size_t bufferCapacity() const noexcept = 0;
virtual void requestCapacity(void* owner, std::size_t minSamples) = 0;
virtual void releaseCapacity(void* owner) = 0;
```

**Note**: Visibility is NOT part of `SignalSinkBase`. It's a UI concern handled as a public member
variable (`bool isVisible = true;`) in `ImPlotSinkModel`, following the "public members for config" principle.

## PlotData Struct

ImPlot-independent struct that can be directly passed to `ImPlot::PlotLineG`:

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
```

Usage with ImPlot:

```cpp
auto pd = sink->plotData();
if (!pd.empty()) {
    // PlotGetter is compatible with ImPlotGetter
    ImPlot::PlotLineG(label, reinterpret_cast<ImPlotGetter>(pd.getter), pd.userData, pd.count);
}
```

## Files Modified

### Core Changes

#### `/src/ui/charts/SignalSink.hpp`

- Simplified `SignalSinkBase` - type-erased interface with indexed access
- Added `PlotPoint`, `PlotGetter`, `PlotData` types for ImPlot compatibility
- Removed `SignalSink<T>` template class (no longer needed)
- Removed `SignalMetadata` struct (replaced with `color()` and `sampleRate()` methods)
- Kept `SignalSinkManager`, `TagData`, `HistoryRequirement`

#### `/src/ui/charts/SignalSinkImpl.hpp`

- `StreamingSignalSink<T>` - standalone streaming data implementation with new API
- `DataSetSignalSink<T>` - standalone DataSet implementation with new API
- Both implement indexed access via `size()`, `xAt()`, `yAt()`
- Both provide `plotData()` method returning callback struct

#### `/src/ui/charts/XYChart.hpp`

- Updated `drawStreamingSignal()` to use indexed access via `sink->xAt()`, `sink->yAt()`
- Updated `drawDataSetSignal()` to use `sink->dataSets()` span
- Simplified `buildAxisCategories()` (metadata no longer has quantity/unit)
- Updated `handleInteractions()` to use `sink->color()` instead of `sink->metadata().color`

#### `/src/ui/blocks/ImPlotSink.hpp`

Key changes to integrate with SignalSinkBase:

1. **ImPlotSinkModel visibility** - public member variable (no getter/setter):
   ```cpp
   bool isVisible = true;  // Direct access, config-style
   ```

2. **SinkWrapper implements new interface**:
   - `size()`, `xAt(i)`, `yAt(i)` for indexed access
   - `color()`, `sampleRate()` for minimal metadata
   - `plotData()` returning callback struct

#### `/src/ui/DashboardPage.cpp`

- No changes required. Visibility is accessed directly as `plotSinkBlock->isVisible` (public member).

## Build & Test

```bash
# Build main application
cd cmake-build-release-gcc15
make opendigitizer-ui

# Build and run tests
make qa_SignalSink && ./src/ui/test/qa_SignalSink
make qa_ChartAbstraction && ./src/ui/test/qa_ChartAbstraction
```

## Test Results

- **qa_SignalSink**: 12 tests - all passed
- **qa_ChartAbstraction**: 13 tests - all passed

## Current Status

### Completed

1. Directory structure `src/ui/charts/`
2. Simplified `SignalSinkBase` interface with indexed access
3. `PlotData` struct for ImPlot-compatible callback rendering
4. `Chart` base class and `ChartManager`
5. `XYChart` concrete implementation (updated for new API)
6. Headless unit tests (updated for new API)
7. `ImPlotSink` integration with `SignalSinkBase` (SinkWrapper updated)
8. `SignalSinkManager` (dual registration with `ImPlotSinkManager`)

### Design Decision: DashboardPage Integration

The DashboardPage continues to use `plotSinkBlock->draw()` rather than XYChart for rendering. This is intentional:

1. **GR4 Drawable pattern**: The `draw()` method on ImPlotSink integrates with GR4's Drawable NTTP, which couples
   data consumption (`invokeWork()`) with rendering. Separating these would require significant GR4-level changes.

2. **XYChart purpose**: XYChart demonstrates the chart abstraction for headless testing and serves as a reference
   implementation. It's available for new code paths but doesn't need to replace the existing DashboardPage rendering.

3. **SignalSinkBase benefit**: The key achievement is that ImPlotSink blocks now implement SignalSinkBase, making
   signal data accessible via a type-erased interface for any external code that needs it.

### Optional Future Work

1. **Transition ImPlotSinkManager usage** - Gradually migrate all `ImPlotSinkManager` usage to `SignalSinkManager`

2. **Consider removing BlockingIO** - User originally requested dropping `BlockingIO<false>` but it's required for
   `invokeWork()`. May need GR4 investigation.

## Branch

`addDiagnostics` (current working branch)

## Related Files to Review

- `/src/ui/Dashboard.hpp` - Dashboard::Plot structure
- `/src/ui/DashboardPage.hpp` - DashboardPage class
- `/src/ui/DashboardPage.cpp` - Main rendering loop using ImPlotSinkManager
