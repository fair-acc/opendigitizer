# Extensibility

OpenDigitizer's design is driven with both the goal of being useful for FAIR (see `doc/personas.md`) and for the open source community. For FAIR, there is a need to work well for both the Paul and Susan personas, a constraint which is a core part of the UI/UX of OpenDigitizer. However, in the interest of being a general purpose signal processing UI, OpenDigitizer makes an effort to expose only a core set of features, and allow for extension via the gnuradio4 plugin system. This plugin system will be used by FAIR to implement particularly FAIR-specific functionality, but also by the open source community to fit OpenDigitizer to their applications.

## `gr::Block`

Blocks are connecting a graph, which is editable in OpenDigitizer via a node graph editor. The UI for placing a new block inspects the block registry (which includes blocks defined by user plugins). After placement, the flowgraph will contain a nodes which shows input ports, output ports, and properties of the block.

## Signal Processing Blocks

These are the default type of block. They implement member functions such as `processOne()` and `processBulk()` to perform transformations on signals. They are intended to be entirely implemented using the gnuradio4 API and would also work offscreen, without OpenDigitizer.

## Charts

Charts are `gr::Block`s in OpenDigitizer which use a type annotation `gr::Drawable` to indicate that they are not supposed to process data, but rather implement a function `draw()`. `draw()` can be implemented by a third party using utilities from the CRTP mixin/helper class `opendigitizer::charts::Chart`, and any arbitrary ImGui or ImPlot rendering. For reference, the declaration for our XY Chart looks like this:

```cpp
struct XYChart : gr::Block<XYChart, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart { /* ... */ };
```

New charts would not be added to the flowgraph but rather would appear in the options for the "Add chart" button on the dashboard and the "Change type" chart context menu option. Signal sinks from the flowgraph (visible in the global signal legend) can then be attached to the chart with drag and drop.

## Toolbar UI Elements

The toolbar is the area at the bottom of OpenDigitizer where the global signal legend is located. Toolbar blocks, unlike charts, *do* appear in the flowgraph, but are marked `Drawable` and implement `draw()`. In this way, a toolbar block is able to draw a control which may send data or messages to connected blocks when interacted with. For reference, the declaration of the global signal legend looks like this:

```cpp
struct GlobalSignalLegend : gr::Block<GlobalSignalLegend, gr::Drawable<gr::UICategory::Toolbar, "ImGui">> { /* ... */ };
```

Note that the global signal legend is a special case, in that it does not appear in the flowgraph.

Also note that, despite appearing in the flowgraph, drawable blocks like toolbar UI blocks do not execute on a background thread or do any processing work. They are more like a way for the user to write a plugin to inject some ImGui drawing code into the toolbar and then use gnuradio to send messages to downstream blocks in response to user interaction.
