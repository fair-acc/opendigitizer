#include "components/DashboardPreview.hpp"
#include <boost/ut.hpp>

#include <cmath>

int main() {
    using namespace boost::ut;
    using DigitizerUi::DashboardPreview;
    using enum DashboardPreview::ChartType;

    const auto expectRect = [](const std::optional<DashboardPreview::Rect>& rect, float x, float y, float w, float h) {
        expect(rect.has_value());
        if (!rect) {
            return;
        }
        expect(std::abs(rect->x - x) < 1e-5f) << "x" << rect->x;
        expect(std::abs(rect->y - y) < 1e-5f) << "y" << rect->y;
        expect(std::abs(rect->w - w) < 1e-5f) << "w" << rect->w;
        expect(std::abs(rect->h - h) < 1e-5f) << "h" << rect->h;
    };

    "splits in free layout"_test = [expectRect] {
        constexpr std::string_view yaml = R"(blocks:
  - id: opendigitizer::SineSource<float32>
    parameters:
      name: NotASink
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: SinkA
  - id: opendigitizer::ImPlotSink<gr::DataSet<float32>>
    parameters:
      name: SinkB
      color: 4660
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: SinkC
connections:
  - [NotASink, 0, SinkA, 0]
  - [NotASink, 0, SinkB, 0]
  - [NotASink, 0, SinkC, 0]
dashboard:
  layout: Free
  windowLayout:
    dockSpace:
      hsplit:
        ratio: 0.25
        first: PlotLeft
        second:
          vsplit:
            ratio: 0.25
            first: PlotTop
            second: PlotBottom
    floatingWindows: {}
  sources:
    - name: SrcA
      block: SinkA
    - name: SrcB
      block: SinkB
    - name: SrcC
      block: SinkC
  plots:
    - name: PlotLeft
      sources:
        - SrcA
        - SrcB
    - name: PlotBottom
      type: WaterfallPlot
      sources:
        - SrcC
    - name: PlotTop
      type: opendigitizer::charts::SpectrumView
      sources:
        - SrcA
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value());
        if (!preview) {
            return;
        }

        expect(eq(preview->charts.size(), 3UZ));
        expect(preview->charts.at(0).type == Chart2D) << "type defaults to XYChart";
        expect(preview->charts.at(1).type == WaterfallPlot);
        expect(preview->charts.at(2).type == SpectrumView);
        expect(!preview->charts.at(0).isFloating);

        expect(!preview->charts.at(0).signals.at(0).isDataSet);
        // this one had a color set so we can test that
        expect(preview->charts.at(0).signals.at(1) == DashboardPreview::Signal{.color = 4660U, .isDataSet = true});
        expect(!preview->charts.at(1).signals.at(0).isDataSet);

        // hsplit: first ie. PlotLeft gets ratio of the width from the left
        // vsplit: second ie. PlotBottom gets ratio of the height from the bottom
        expectRect(preview->charts.at(0).rect, 0.f, 0.f, 0.25f, 1.f);
        // these two to the right of charts[0], x is at its width
        expectRect(preview->charts.at(1).rect, 0.25f, 0.75f, 0.75f, 0.25f);
        expectRect(preview->charts.at(2).rect, 0.25f, 0.f, 0.75f, 0.75f);
    };

    "floating windows keep coordinates exactly"_test = [expectRect] {
        constexpr std::string_view yaml = R"(blocks: []
dashboard:
  layout: Free
  windowLayout:
    dockSpace:
      hsplit:
        ratio: 0.5
        first: PlotDocked
        second: PlotDocked2
    floatingWindows:
      PlotFloating:
        x: 128
        y: 72
        width: 640
        height: 360
  sources: []
  plots:
    - name: PlotDocked
      sources: []
    - name: PlotDocked2
      sources: []
    - name: PlotFloating
      type: SomeCustomChart
      sources: []
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value()) << fatal;
        expect(eq(preview->charts.size(), 3UZ));
        // two split evenly
        expectRect(preview->charts.at(0).rect, 0.f, 0.f, 0.5f, 1.f);
        expectRect(preview->charts.at(1).rect, 0.5f, 0.f, 0.5f, 1.f);
        expect(!preview->charts.at(0).isFloating);
        expect(!preview->charts.at(1).isFloating);
        // one floating with the exact numbers from the yaml
        expectRect(preview->charts.at(2).rect, 128.f, 72.f, 640.f, 360.f);
        expect(preview->charts.at(2).isFloating);
        expect(preview->charts.at(2).type == Other);
    };

    "use rect key for describing layout"_test = [expectRect] {
        constexpr std::string_view yaml = R"(blocks:
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: S1
connections:
  - [SomeSource, 0, S1, 0]
dashboard:
  layout: Free
  sources:
    - name: S1
      block: S1
  plots:
    - name: P1
      sources:
        - S1
      rect: [0, 0, 1, 2]
    - name: P2
      sources:
        - S1
      rect: [1, 0, 3, 2]
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value());
        if (!preview) {
            return;
        }

        expect(eq(preview->charts.size(), 2UZ));
        // split with the proper ratio
        expectRect(preview->charts.at(0).rect, 0.f, 0.f, 0.25f, 1.f);
        expectRect(preview->charts.at(1).rect, 0.25f, 0.f, 0.75f, 1.f);
        expect(!preview->charts.at(0).signals.at(0).isDataSet);
    };

    "implotsinks with no input draw without a signal shown"_test = [] {
        constexpr std::string_view yaml = R"(blocks:
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: ConnectedSink
  - id: opendigitizer::ImPlotSink<float32>
    parameters:
      name: OrphanSink
  - id: myproject::CustomSink<float32>
    parameters:
      name: UserSink
connections:
  - [SomeSource, 0, ConnectedSink, 0]
dashboard:
  sources:
    - name: ConnectedSink
      block: ConnectedSink
    - name: OrphanSink
      block: OrphanSink
    - name: UserSink
      block: UserSink
  plots:
    - name: P1
      sources:
        - ConnectedSink
        - OrphanSink
        - UserSink
        - MissingSink
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value());
        if (!preview) {
            return;
        }
        expect(eq(preview->charts.size(), 1UZ));
        expect(!preview->charts.at(0).signals.at(0).isDataSet);
        expect(preview->charts.at(0).signals.at(0).color != 0);

        expect(!preview->charts.at(0).signals.at(1).isDataSet);
        expect(preview->charts.at(0).signals.at(1).color == 0) << "expected 0 color for the sink with no inputs";

        expect(!preview->charts.at(0).signals.at(2).isDataSet);
        expect(preview->charts.at(0).signals.at(2).color == 0) << "expected 0 color for user defined block, which doesnt use ColourManager";
    };

    "row layout"_test = [expectRect] {
        constexpr std::string_view yaml = R"(blocks: []
dashboard:
  layout: Row
  sources: []
  plots:
    - name: P1
      sources: []
    - name: P2
      sources: []
    - name: P3
      sources: []
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value());
        if (!preview) {
            return;
        }
        expect(eq(preview->charts.size(), 3UZ));
        expectRect(preview->charts.at(0).rect, 0.f, 0.f, 1.f / 3.f, 1.f);
        expectRect(preview->charts.at(1).rect, 1.f / 3.f, 0.f, 1.f / 3.f, 1.f);
        expectRect(preview->charts.at(2).rect, 2.f / 3.f, 0.f, 1.f / 3.f, 1.f);
    };

    "fall back to grid if things are missing"_test = [expectRect] {
        constexpr std::string_view yaml = R"(blocks: []
dashboard:
  sources: []
  plots:
    - name: P1
      sources: []
    - name: P2
      sources: []
    - name: P3
      sources: []
)";

        const auto preview = DashboardPreview::fromFlowgraphYaml(yaml);
        expect(preview.has_value());
        if (!preview) {
            return;
        }
        expect(eq(preview->charts.size(), 3UZ));
        expectRect(preview->charts.at(0).rect, 0.f, 0.f, 0.5f, 0.5f);
        expectRect(preview->charts.at(1).rect, 0.5f, 0.f, 0.5f, 0.5f);
        // like DockSpace::layoutInGrid(), the last chart occupies the rest of its row
        expectRect(preview->charts.at(2).rect, 0.f, 0.5f, 1.f, 0.5f);
    };

    "invalid input yields no preview"_test = [] {
        expect(!DashboardPreview::fromFlowgraphYaml("{ not yaml").has_value());
        expect(!DashboardPreview::fromFlowgraphYaml("blocks: []\n").has_value()) << "no dashboard section";
        expect(!DashboardPreview::fromFlowgraphYaml("dashboard:\n  sources: []\n  plots: []\n").has_value()) << "no charts";
    };
}
