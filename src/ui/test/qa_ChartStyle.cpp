#include "ImGuiTestApp.hpp"
#include "TestSinks.hpp"

#include <boost/ut.hpp>

#include "charts/Charts.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <numbers>

using namespace boost;
using namespace boost::ut;

namespace {
using opendigitizer::test::TestStreamingSink;

constexpr ImVec2 kChartPos{50.f, 50.f};
constexpr ImVec2 kChartSize{640.f, 400.f};

using Mode = opendigitizer::charts::ChartMode;

struct TestState {
    std::shared_ptr<TestStreamingSink>              voltage;
    std::shared_ptr<TestStreamingSink>              current;
    std::unique_ptr<opendigitizer::charts::XYChart> singleAxis; // one sink, View mode
    std::unique_ptr<opendigitizer::charts::XYChart> twoAxes;    // two quantities, Interaction mode with legend
    opendigitizer::charts::XYChart*                 drawn = nullptr;
    Mode                                            mode  = Mode::View;
    std::optional<ImVec4>                           hostBackground;
};

std::shared_ptr<TestStreamingSink> makeSink(std::string name, std::uint32_t color, std::string quantity, std::string unit, double amplitude, double phase) {
    auto sink = std::make_shared<TestStreamingSink>(std::move(name), 512);
    sink->setColor(color);
    sink->setSignalQuantity(std::move(quantity));
    sink->setSignalUnit(std::move(unit));
    for (std::size_t i = 0UZ; i < 400UZ; ++i) {
        const double t = static_cast<double>(i) / 100.0;
        sink->pushSample(t, static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * t + phase)));
    }
    std::ignore = opendigitizer::charts::SinkRegistry::instance().registerSink(sink);
    return sink;
}

std::unique_ptr<opendigitizer::charts::XYChart> makeChart(std::string name, std::vector<std::string> sinks, bool showLegend) {
    auto chart               = std::make_unique<opendigitizer::charts::XYChart>();
    chart->chart_name.value  = std::move(name);
    chart->data_sinks.value  = std::move(sinks);
    chart->show_legend.value = showLegend;
    return chart;
}

TestState* g_state = nullptr;

constexpr ImVec4 kMagenta{1.f, 0.f, 1.f, 1.f};

struct Rgb {
    int r;
    int g;
    int b;
};

struct Region { // fractions of the capture
    float x0;
    float y0;
    float x1;
    float y1;
};

constexpr Region kWholeChart{0.f, 0.f, 1.f, 1.f};
constexpr Region kPlotInterior{.25f, .10f, .90f, .75f}; // grid lines and the signal, no labels
constexpr Region kLeftAxis{0.f, 0.f, .11f, .85f};       // tick labels and the axis label left of the plot
constexpr Region kRightAxis{.88f, 0.f, 1.f, .90f};      // second y axis, right of the plot
constexpr Region kTopLeft{.14f, .03f, .40f, .18f};      // ImPlot's default legend place
constexpr Region kTopRight{.55f, .03f, .85f, .18f};
constexpr Region kLegendInside{.16f, .06f, .34f, .15f};

bool isMagenta(Rgb c) { return c.r > 240 && c.g < 15 && c.b > 240; }
bool isVoltageGreen(Rgb c) { return c.g > 100 && c.r < 80 && c.b < 80; }               // 0x00C000, also blended into the dark background
bool isCurrentOrange(Rgb c) { return c.r > 150 && c.g > 20 && c.g < 110 && c.b < 50; } // 0xE04000
bool isWhite(Rgb c) { return c.r > 200 && c.g > 200 && c.b > 200; }
bool isGrey(Rgb c) { return std::abs(c.r - c.g) <= 2 && std::abs(c.g - c.b) <= 2; } // not the anti-aliased edges of a coloured line

class Pixels {
    ImGuiCaptureImageBuf _image;

public:
    explicit Pixels(ImGuiTestContext* ctx) {
        ctx->CaptureReset();
        ctx->CaptureArgs->InCaptureRect    = ImRect(kChartPos, kChartPos + kChartSize);
        ctx->CaptureArgs->InOutputImageBuf = std::addressof(_image);
        ctx->CaptureScreenshot(ImGuiCaptureFlags_Instant | ImGuiCaptureFlags_HideMouseCursor | ImGuiCaptureFlags_NoSave);
    }

    [[nodiscard]] Rgb at(int x, int y) const {
        const unsigned int pixel = _image.Data[y * _image.Width + x]; // RGBA8
        return {static_cast<int>(pixel & 0xFFU), static_cast<int>((pixel >> 8U) & 0xFFU), static_cast<int>((pixel >> 16U) & 0xFFU)};
    }

    [[nodiscard]] std::size_t count(Region region, auto predicate) const {
        std::size_t n = 0UZ;
        for (int y = static_cast<int>(region.y0 * static_cast<float>(_image.Height)); y < static_cast<int>(region.y1 * static_cast<float>(_image.Height)); ++y) {
            for (int x = static_cast<int>(region.x0 * static_cast<float>(_image.Width)); x < static_cast<int>(region.x1 * static_cast<float>(_image.Width)); ++x) {
                n += predicate(at(x, y)) ? 1UZ : 0UZ;
            }
        }
        return n;
    }

    [[nodiscard]] std::size_t area(Region region) const {
        return count(region, [](Rgb) { return true; });
    }

    // grey pixels that differ from the region's most frequent colour, i.e. grid lines over the plot background
    [[nodiscard]] std::size_t greyLines(Region region) const { return greyLinePixels(region).size(); }

    [[nodiscard]] double meanGreyLineBrightness(Region region) const {
        const std::vector<Rgb> pixels = greyLinePixels(region);
        double                 sum    = 0.0;
        for (const Rgb& c : pixels) {
            sum += static_cast<double>(c.g);
        }
        return pixels.empty() ? 0.0 : sum / static_cast<double>(pixels.size());
    }

    [[nodiscard]] std::vector<Rgb> greyLinePixels(Region region) const {
        std::map<int, std::size_t> histogram;
        std::ignore                 = count(region, [&](Rgb c) { return ++histogram[(c.r << 16) | (c.g << 8) | c.b] > 0UZ; });
        const int        background = std::ranges::max_element(histogram, {}, [](const auto& entry) { return entry.second; })->first;
        const Rgb        bg{background >> 16, (background >> 8) & 0xFF, background & 0xFF};
        std::vector<Rgb> lines;
        std::ignore = count(region, [&](Rgb c) {
            if (isGrey(c) && (std::abs(c.r - bg.r) > 4 || std::abs(c.g - bg.g) > 4 || std::abs(c.b - bg.b) > 4)) {
                lines.push_back(c);
            }
            return false;
        });
        return lines;
    }
};

DigitizerUi::ChartStyle chartStyle(auto configure) { // designated initialisers would have to name every field (Clang)
    DigitizerUi::ChartStyle style;
    configure(style);
    return style;
}

std::unique_ptr<Pixels> drawAndCapture(ImGuiTestContext* ctx, opendigitizer::charts::XYChart& chart, Mode mode, const DigitizerUi::ChartStyle& style) {
    g_state->drawn                                         = std::addressof(chart);
    g_state->mode                                          = mode;
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle = style;
    ctx->Yield(3);
    auto pixels                                            = std::make_unique<Pixels>(ctx);
    DigitizerUi::LookAndFeel::mutableInstance().chartStyle = {};
    return pixels;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chartstyle", "chart over fixed data");

        t->GuiFunc = [](ImGuiTestContext*) {
            ImGui::SetNextWindowPos(kChartPos);
            ImGui::SetNextWindowSize(kChartSize);
            if (g_state->hostBackground) {
                ImGui::PushStyleColor(ImGuiCol_WindowBg, *g_state->hostBackground);
            }
            IMW::Window window("Chart", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
            if (g_state->hostBackground) {
                ImGui::PopStyleColor();
            }
            if (!g_state->drawn) {
                return;
            }
            std::ignore = g_state->drawn->draw(opendigitizer::charts::chartDrawConfig(g_state->mode));
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ctx->MouseMoveToPos(ImVec2(5.f, 5.f)); // no hover tooltip over the chart
            ctx->Yield(10);

            auto& state                                           = *g_state;
            "the default style draws the reference captures"_test = [&] {
                state.drawn = state.singleAxis.get();
                state.mode  = Mode::View;
                ctx->Yield(5);
                captureScreenshot(*ctx, ImRect(kChartPos, kChartPos + kChartSize));
                state.drawn = state.twoAxes.get();
                state.mode  = Mode::Interaction;
                ctx->Yield(5);
                captureScreenshot(*ctx, ImRect(kChartPos, kChartPos + kChartSize));
            };

            "an opaque plot background fills the plot area"_test = [&] {
                const auto plain   = drawAndCapture(ctx, *state.singleAxis, Mode::View, {});
                const auto magenta = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.plotBackground = kMagenta; }));
                expect(eq(plain->count(kWholeChart, isMagenta), 0UZ));
                expect(magenta->count(kWholeChart, isMagenta) > magenta->area(kWholeChart) / 2UZ) << "more than half of the chart is plot area";
            };

            "a transparent plot background shows the host's background"_test = [&] {
                state.hostBackground   = kMagenta;
                const auto plain       = drawAndCapture(ctx, *state.singleAxis, Mode::View, {});
                const auto transparent = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.plotBackground = ImVec4(0.f, 0.f, 0.f, 0.f); }));
                state.hostBackground.reset();
                expect(plain->count(kPlotInterior, isMagenta) < plain->area(kPlotInterior) / 10UZ) << "by default the plot covers the host";
                expect(transparent->count(kPlotInterior, isMagenta) > transparent->area(kPlotInterior) / 2UZ);
            };

            "a wider line covers proportionally more pixels"_test = [&] {
                const auto thin  = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.lineWidth = 1.f; }));
                const auto thick = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.lineWidth = 4.f; }));
                const auto nThin = thin->count(kPlotInterior, isVoltageGreen);
                expect(nThin > 500UZ) << fatal;
                expect(thick->count(kPlotInterior, isVoltageGreen) > 5UZ * nThin / 2UZ) << std::format("4 px line: {} green pixels, 1 px line: {}", thick->count(kPlotInterior, isVoltageGreen), nThin);
            };

            "grid alpha 0 removes the grid lines, alpha 1 makes them stronger"_test = [&] {
                const auto plain  = drawAndCapture(ctx, *state.singleAxis, Mode::View, {});
                const auto hidden = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.gridAlpha = 0.f; }));
                const auto opaque = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.gridAlpha = 1.f; }));
                expect(plain->greyLines(kPlotInterior) > 500UZ) << fatal << "the default grid is visible";
                expect(eq(hidden->greyLines(kPlotInterior), 0UZ));
                // ImPlot's grid is the axis text colour at alpha 0.25: at alpha 1 the lines stand out at least twice as far from the background
                expect(opaque->meanGreyLineBrightness(kPlotInterior) - 15.0 > 2.0 * (plain->meanGreyLineBrightness(kPlotInterior) - 15.0)) << std::format("mean grid brightness {} at alpha 1, {} by default (background 15)", opaque->meanGreyLineBrightness(kPlotInterior), plain->meanGreyLineBrightness(kPlotInterior));
            };

            "axis alpha 0 hides the tick labels and the axis label"_test = [&] {
                const auto plain  = drawAndCapture(ctx, *state.singleAxis, Mode::View, {});
                const auto hidden = drawAndCapture(ctx, *state.singleAxis, Mode::View, chartStyle([](DigitizerUi::ChartStyle& s) { s.axisAlpha = 0.f; }));
                expect(plain->count(kLeftAxis, isWhite) > 100UZ) << fatal;
                expect(eq(hidden->count(kLeftAxis, isWhite), 0UZ));
            };

            "without axis colouring the second y axis is no longer drawn in its signal's colour"_test = [&] {
                const auto coloured = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, {});
                const auto neutral  = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, chartStyle([](DigitizerUi::ChartStyle& s) { s.colourAxesBySignal = false; }));
                expect(coloured->count(kRightAxis, isCurrentOrange) > 100UZ) << fatal;
                expect(eq(neutral->count(kRightAxis, isCurrentOrange), 0UZ));
                expect(neutral->count(kRightAxis, isWhite) > 100UZ) << "the labels are still drawn";
            };

            "the legend moves to the requested corner"_test = [&] {
                const auto northWest = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, {});
                const auto northEast = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, chartStyle([](DigitizerUi::ChartStyle& s) { s.legendLocation = ImPlotLocation_NorthEast; }));
                expect(northWest->count(kTopLeft, isWhite) > 100UZ) << fatal << "the legend's white text, top left by default";
                expect(northWest->count(kTopRight, isWhite) < 20UZ);
                expect(northEast->count(kTopLeft, isWhite) < 20UZ);
                expect(northEast->count(kTopRight, isWhite) > 100UZ);
            };

            "the legend panel is see-through at alpha 0 and opaque at alpha 1"_test = [&] {
                const auto seeThrough = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, chartStyle([](DigitizerUi::ChartStyle& s) {
                    s.plotBackground = kMagenta;
                    s.legendAlpha    = 0.f;
                }));
                const auto opaque     = drawAndCapture(ctx, *state.twoAxes, Mode::Interaction, chartStyle([](DigitizerUi::ChartStyle& s) {
                    s.plotBackground = kMagenta;
                    s.legendAlpha    = 1.f;
                }));
                expect(seeThrough->count(kLegendInside, isMagenta) > 100UZ);
                expect(eq(opaque->count(kLegendInside, isMagenta), 0UZ));
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    g_state = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart_style";
    TestApp app(options);
    app.initImGui();

    state.voltage    = makeSink("style_voltage", 0x00C000, "voltage", "V", 1.0, 0.0);
    state.current    = makeSink("style_current", 0xE04000, "current", "A", 2.0, 1.0);
    state.singleAxis = makeChart("single axis", {"style_voltage"}, false);
    state.twoAxes    = makeChart("two axes", {"style_voltage", "style_current"}, true);

    const bool result = app.runTests();
    state.singleAxis.reset();
    state.twoAxes.reset();
    std::ignore = opendigitizer::charts::SinkRegistry::instance().unregisterSink("style_voltage");
    std::ignore = opendigitizer::charts::SinkRegistry::instance().unregisterSink("style_current");
    g_state     = nullptr;
    return result ? 0 : 1;
}
