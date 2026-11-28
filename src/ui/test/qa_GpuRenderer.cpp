#include "ImGuiTestApp.hpp"
#include "TestSinks.hpp"

#include <boost/ut.hpp>

#include "charts/Charts.hpp"

#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace gr::od;

using namespace boost::ut;

namespace {
using charts::XYChart;

constexpr ImVec2 kGpuPos{50.f, 50.f};
constexpr ImVec2 kCpuPos{750.f, 50.f};
constexpr ImVec2 kChartSize{640.f, 400.f};

struct TestState {
    std::shared_ptr<SignalSink> sink;
    std::unique_ptr<XYChart>    gpu;
    std::unique_ptr<XYChart>    cpu;

    std::shared_ptr<SignalSink> errorSink;
    std::unique_ptr<XYChart>    withBand;
    std::unique_ptr<XYChart>    withoutBand;

    std::shared_ptr<SignalSink> sparseErrorSink;
    std::unique_ptr<XYChart>    comboDense;
    std::unique_ptr<XYChart>    comboSparse;
    std::unique_ptr<XYChart>    sparseWithoutBand;

    std::shared_ptr<SignalSink>          surfaceSink;
    std::unique_ptr<charts::SurfacePlot> surface;

    std::shared_ptr<SignalSink>      correlationX;
    std::shared_ptr<SignalSink>      correlationY;
    std::unique_ptr<charts::YYChart> withBars;
    std::unique_ptr<charts::YYChart> withoutBars;

    bool                  densityProgramBuilt = false;
    std::vector<uint32_t> densityPixels; // [rows, columns] as drawn
    uint32_t              densityFullScaleColour = 0U;
};

TestState* gState = nullptr;

std::unique_ptr<XYChart> makeChart(std::string name, bool gpuAcceleration) {
    auto chart                                     = std::make_unique<XYChart>();
    chart->chart_name.value                        = std::move(name);
    chart->data_sinks.value                        = {"gpu_line"};
    chart->show_legend.value                       = false;
    chart->n_history.value                         = 500U;
    chart->update_rate.value                       = 0.f;
    chart->gpu_acceleration.value                  = gpuAcceleration;
    chart->_gpuLineProgram.allowSoftwareRasteriser = true; // compares both paths on CI's llvmpipe too
    return chart;
}

void drawChart(const char* name, ImVec2 pos, auto& chart) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(kChartSize);
    imw::Window window(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    std::ignore = chart.draw(charts::chartDrawConfig(charts::ChartMode::View));
}

void push(std::size_t to) {
    for (std::size_t i = gState->sink->totalSampleCount(); i < to; ++i) {
        const auto t = static_cast<double>(i);
        gState->sink->pushSample(1e-3 * t, static_cast<float>((0.3 + 0.6 * t / 1100.0) * std::sin(std::numbers::pi * t / 100.0)));
    }
}

using Rows = std::vector<std::optional<std::pair<int, int>>>;

Rows lineRows(const test::ImGuiTestApp::CapturedPixels& image, int left) {
    Rows rows(static_cast<std::size_t>(kChartSize.x));
    for (int y = 0; y < image.height; ++y) {
        for (std::size_t column = 0UZ; column < rows.size(); ++column) {
            const auto [red, green, blue] = test::ImGuiTestApp::CapturedPixels::rgb(image.rgba[static_cast<std::size_t>(y * image.width + left) + column]);
            if (green > 100 && red < 80 && blue < 80) {
                rows[column] = std::pair{rows[column] ? rows[column]->first : y, y};
            }
        }
    }
    return rows;
}

test::ImGuiTestApp::CapturedPixels settledCapture(ImGuiTestContext* ctx) {
    const auto capture = [ctx] { return test::ImGuiTestApp::capturePixels(*ctx, ImRect(kGpuPos, kCpuPos + kChartSize)); };
    auto       image   = capture();
    for (auto next = capture(); next.rgba != image.rgba; next = capture()) {
        image = std::move(next);
    }
    return image;
}

void expectMatch(ImGuiTestContext* ctx, std::size_t total) {
    const auto image = settledCapture(ctx);
    const Rows gpu   = lineRows(image, 0);
    const Rows cpu   = lineRows(image, static_cast<int>(kCpuPos.x - kGpuPos.x));

    std::size_t              compared = 0UZ;
    std::vector<std::string> differences;
    for (std::size_t x = 0UZ; x < gpu.size(); ++x) {
        if (gpu[x].has_value() != cpu[x].has_value()) {
            differences.push_back(std::format("column {}: line on the {} only", x, gpu[x] ? "GPU" : "CPU"));
        } else if (gpu[x]) {
            ++compared;
            if (std::abs(gpu[x]->first - cpu[x]->first) > 1 || std::abs(gpu[x]->second - cpu[x]->second) > 1) {
                differences.push_back(std::format("column {}: rows {}-{} on the GPU, {}-{} on the CPU", x, gpu[x]->first, gpu[x]->second, cpu[x]->first, cpu[x]->second));
            }
        }
    }
    if (compared <= 300UZ || !differences.empty()) {
        test::ImGuiTestApp::captureScreenshot(*ctx, ImRect(kGpuPos, kCpuPos + kChartSize));
    }
    expect(differences.empty()) << std::format("{} samples: {} columns differ, first: {}", total, differences.size(), differences.empty() ? std::string() : differences.front());
    expect(compared > 300UZ) << std::format("{} samples: {} columns with a line", total, compared) << fatal;
}
// green: the colour of the test signal's line and surface
std::size_t tintedPixels(const test::ImGuiTestApp::CapturedPixels& image, int left) {
    std::size_t tinted = 0UZ;
    for (int y = 0; y < image.height; ++y) {
        for (int column = 0; column < static_cast<int>(kChartSize.x); ++column) {
            const auto [red, green, blue] = test::ImGuiTestApp::CapturedPixels::rgb(image.rgba[static_cast<std::size_t>(y * image.width + left + column)]);
            tinted += green > red + 20 && green > blue + 20 ? 1UZ : 0UZ;
        }
    }
    return tinted;
}

constexpr std::size_t kPlotPixels = static_cast<std::size_t>(kChartSize.x * kChartSize.y);

/// the tinted pixels of the chart at kGpuPos and of its twin at kCpuPos, from one capture of both, which a failed `holds` keeps as a screenshot
void expectTinted(ImGuiTestContext* ctx, std::string_view what, auto holds) {
    ctx->MouseMoveToPos(ImVec2(5.f, 5.f));
    const ImRect      both(kGpuPos, kCpuPos + kChartSize);
    const auto        image   = test::ImGuiTestApp::captureSettledPixels(*ctx, both);
    const std::size_t with    = tintedPixels(image, 0);
    const std::size_t without = tintedPixels(image, static_cast<int>(kCpuPos.x - kGpuPos.x));
    const bool        held    = holds(with, without);
    if (!held) {
        test::ImGuiTestApp::captureScreenshot(*ctx, both);
    }
    expect(held) << std::format("{}: tinted pixels {} with, {} without", what, with, without);
}

// {lowest, highest}: Viridis' dark violet and yellow
std::pair<std::size_t, std::size_t> colormapEnds(const test::ImGuiTestApp::CapturedPixels& image, int left) {
    std::pair<std::size_t, std::size_t> ends{0UZ, 0UZ};
    for (int y = 0; y < image.height; ++y) {
        for (int column = 0; column < static_cast<int>(kChartSize.x); ++column) {
            const auto [red, green, blue] = test::ImGuiTestApp::CapturedPixels::rgb(image.rgba[static_cast<std::size_t>(y * image.width + left + column)]);
            ends.first += red < 140 && blue > green + 30 && red > green ? 1UZ : 0UZ;
            ends.second += red > 200 && green > 170 && blue < 110 ? 1UZ : 0UZ;
        }
    }
    return ends;
}

// ground truth by construction: a constant spectrum at 5 in rows of 1 unit from yMax = 10 hits row 5 of every column;
// with peak normalisation that cell is at full scale (the colormap's last entry) and every other cell is empty (transparent)
void colourDensityOnce() {
    if (!gState->densityPixels.empty()) {
        return;
    }
    constexpr std::size_t             kColumns = 4UZ;
    constexpr std::size_t             kRows    = 10UZ;
    charts::DensityHistogram          density;
    const std::array<float, kColumns> spectrum{5.f, 5.f, 5.f, 5.f};
    for (int n = 0; n < 6; ++n) {
        density.update(spectrum, kRows, 4.0, 0.0, 10.0, ImPlotColormap_Viridis);
    }
    gState->densityProgramBuilt = density.initProgram();
    if (!gState->densityProgramBuilt) {
        gState->densityPixels.assign(1UZ, 0U);
        return;
    }
    density.colour(density._grid.fullScale(charts::DensityNormalisation::peak));
    GLint previousFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, density._imageFBO);
    gState->densityPixels.resize(kColumns * kRows);
    glReadPixels(0, 0, static_cast<GLsizei>(kColumns), static_cast<GLsizei>(kRows), GL_RGBA, GL_UNSIGNED_BYTE, gState->densityPixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFBO));
    gState->densityFullScaleColour = charts::buildColormapLut(ImPlotColormap_Viridis).back();
}
} // namespace

struct TestApp : public test::ImGuiTestApp {
    using test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "gpurenderer", "gpu_vs_cpu_render");

        t->GuiFunc = [](ImGuiTestContext*) {
            drawChart("GPU", kGpuPos, *gState->gpu);
            drawChart("CPU", kCpuPos, *gState->cpu);
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ctx->MouseMoveToPos(ImVec2(5.f, 5.f));

            "GPU and CPU lines match"_test = [&] {
                const XYChart& gpu = *gState->gpu;
                for (const std::size_t total : {300UZ, 700UZ, 1100UZ}) {
                    push(total);
                    test::waitUntil(ctx, "the GPU lines are uploaded or the line shader is unavailable", [&] { return (gpu._gpuLineProgram._compileAttempted && gpu._gpuLineProgram.handles.program == 0) || (!gpu._gpuLines.empty() && gpu._gpuLines.begin()->second._uploadedEnd == total); });
                    expect(!gpu._gpuLines.empty()) << "the GPU path draws" << fatal;
                    expectMatch(ctx, total);
                }
            };
        };

        ImGuiTest* band = IM_REGISTER_TEST(engine(), "gpurenderer", "error_band");
        band->GuiFunc   = [](ImGuiTestContext*) {
            drawChart("WithBand", kGpuPos, *gState->withBand);
            drawChart("WithoutBand", kCpuPos, *gState->withoutBand);
        };
        band->TestFunc = [](ImGuiTestContext* ctx) {
            // ground truth by construction: the band y ± 1 around a flat line covers a large share of the plot, the line alone a thin row
            "a signal's error surface is drawn under its line, also on the GPU line path"_test = [&] { //
                expectTinted(ctx, "the surface", [](std::size_t with, std::size_t without) { return with > without + kPlotPixels / 10UZ; });
            };
        };

        ImGuiTest* comboDense = IM_REGISTER_TEST(engine(), "gpurenderer", "combo_dense");
        comboDense->GuiFunc   = [](ImGuiTestContext*) {
            drawChart("ComboDense", kGpuPos, *gState->comboDense);
            drawChart("WithoutBand", kCpuPos, *gState->withoutBand);
        };
        comboDense->TestFunc = [](ImGuiTestContext* ctx) {
            // ground truth by construction: 300 points over 640 px are about 2 px apart, closer than the bar caps: the surface
            "combo draws the surface where the bars' caps would overlap"_test = [&] { //
                expectTinted(ctx, "combo", [](std::size_t with, std::size_t without) { return with > without + kPlotPixels / 10UZ; });
            };
        };

        ImGuiTest* comboSparse = IM_REGISTER_TEST(engine(), "gpurenderer", "combo_sparse");
        comboSparse->GuiFunc   = [](ImGuiTestContext*) {
            drawChart("ComboSparse", kGpuPos, *gState->comboSparse);
            drawChart("SparseWithoutBand", kCpuPos, *gState->sparseWithoutBand);
        };
        comboSparse->TestFunc = [](ImGuiTestContext* ctx) {
            // ground truth by construction: 10 points over 640 px are about 60 px apart: 10 bars across the plot's height, each over
            // 100 px long, and no surface
            "combo draws bars where their caps do not overlap"_test = [&] { //
                expectTinted(ctx, "combo", [](std::size_t with, std::size_t without) { return with > without + 10UZ * 100UZ && with < without + kPlotPixels / 10UZ; });
            };
        };

        ImGuiTest* surface = IM_REGISTER_TEST(engine(), "gpurenderer", "surface_mesh");
        surface->GuiFunc   = [](ImGuiTestContext*) { drawChart("Surface", kGpuPos, *gState->surface); };
        surface->TestFunc  = [](ImGuiTestContext* ctx) {
            // ground truth by construction: spectra ramping from 0 to 1 span the whole colormap;
            // about 2100 and 900 pixels at its ends on radeonsi and llvmpipe alike
            "a SurfacePlot draws its spectra as a mesh in the colormap's colours"_test = [&] {
                ctx->MouseMoveToPos(ImVec2(5.f, 5.f));
                test::waitUntil(ctx, "the surface renderer is initialised", [] { return gState->surface->_gpuRenderer._initAttempted; });
                expect(gState->surface->_gpuRenderer.available()) << "the surface shader builds" << fatal;
                const auto image       = test::ImGuiTestApp::captureSettledPixels(*ctx, ImRect(kGpuPos, kGpuPos + kChartSize));
                const auto [low, high] = colormapEnds(image, 0);
                if (low < 300UZ || high < 300UZ) {
                    test::ImGuiTestApp::captureScreenshot(*ctx, ImRect(kGpuPos, kGpuPos + kChartSize));
                }
                expect(low > 300UZ && high > 300UZ) << std::format("pixels in the colormap's lowest colour: {}, highest: {}", low, high);
            };
        };

        ImGuiTest* bars = IM_REGISTER_TEST(engine(), "gpurenderer", "correlation_error_bars");
        bars->GuiFunc   = [](ImGuiTestContext*) {
            drawChart("WithBars", kGpuPos, *gState->withBars);
            drawChart("WithoutBars", kCpuPos, *gState->withoutBars);
        };
        bars->TestFunc = [](ImGuiTestContext* ctx) {
            // ground truth by construction: 50 points with σ_y = 0.2 and σ_x = 0.03 draw 100 bars beside the line alone, each over 10 px long
            "a YYChart draws both signals' uncertainties as bars"_test = [&] { //
                expectTinted(ctx, "bars", [](std::size_t with, std::size_t without) { return with > without + 100UZ * 10UZ; });
            };
        };

        ImGuiTest* density = IM_REGISTER_TEST(engine(), "gpurenderer", "density_colour_pass");
        density->GuiFunc   = [](ImGuiTestContext*) { colourDensityOnce(); };
        density->TestFunc  = [](ImGuiTestContext* ctx) {
            "the density colour pass draws a full-scale cell in the colormap's last entry, an empty one transparent"_test = [&] {
                test::waitUntil(ctx, "the density image is read back", [] { return !gState->densityPixels.empty(); });
                expect(gState->densityProgramBuilt) << "the colour-map shader builds" << fatal;
                for (std::size_t row = 0UZ; row < 10UZ; ++row) {
                    for (std::size_t column = 0UZ; column < 4UZ; ++column) {
                        const uint32_t pixel    = gState->densityPixels[row * 4UZ + column];
                        const uint32_t expected = row == 5UZ ? gState->densityFullScaleColour : 0U;
                        expect(eq(pixel, expected)) << "row" << row << "column" << column;
                    }
                }
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    gState = std::addressof(state);

    auto options             = test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "gpu_vs_cpu_render";
    TestApp app(options);
    app.initImGui();

    state.sink                 = test::makeTestStreamingSink("gpu_line", 1024UZ);
    state.sink->metadata.color = 0x00C000;
    std::ignore                = charts::SinkRegistry::instance().registerSink(state.sink);
    state.gpu                  = makeChart("gpu", true);
    state.cpu                  = makeChart("cpu", false);

    const auto uncertainSink = [](std::string name, std::size_t capacity) { return std::make_shared<SignalSink>(SignalSink::Config{.kind = SignalKind::Streaming, .uniqueName = name, .name = name, .capacity = capacity, .capacityFloor = capacity, .doubleSamples = false, .withErrors = true}); };
    const auto errorChart    = [](std::string name, std::string sinkName, charts::ErrorStyle style) {
        auto chart               = makeChart(std::move(name), true);
        chart->data_sinks.value  = {std::move(sinkName)};
        chart->error_style.value = style;
        return chart;
    };
    state.errorSink                 = uncertainSink("error_band", 1024UZ);
    state.errorSink->metadata.color = 0x00C000;
    for (std::size_t i = 0UZ; i < 300UZ; ++i) {
        state.errorSink->pushSample(1e-3 * static_cast<double>(i), 0.f, 1.f);
    }
    std::ignore       = charts::SinkRegistry::instance().registerSink(state.errorSink);
    state.withBand    = errorChart("with band", "error_band", charts::ErrorStyle::surface);
    state.withoutBand = errorChart("without band", "error_band", charts::ErrorStyle::none);
    state.comboDense  = errorChart("combo dense", "error_band", charts::ErrorStyle::combo);

    state.sparseErrorSink                 = uncertainSink("error_sparse", 64UZ);
    state.sparseErrorSink->metadata.color = 0x00C000;
    for (std::size_t i = 0UZ; i < 10UZ; ++i) {
        state.sparseErrorSink->pushSample(1e-3 * static_cast<double>(i), 0.f, 1.f);
    }
    std::ignore             = charts::SinkRegistry::instance().registerSink(state.sparseErrorSink);
    state.comboSparse       = errorChart("combo sparse", "error_sparse", charts::ErrorStyle::combo);
    state.sparseWithoutBand = errorChart("sparse without band", "error_sparse", charts::ErrorStyle::none);

    state.surfaceSink = test::makeTestDataSetSink("surface", 16UZ);
    for (std::size_t row = 0UZ; row < 8UZ; ++row) {
        gr::DataSet<float> spectrum;
        spectrum.timestamp  = static_cast<std::int64_t>(row) * 1'000'000'000;
        spectrum.axis_names = {"frequency"};
        spectrum.axis_units = {"Hz"};
        spectrum.axis_values.resize(1UZ);
        spectrum.extents           = {64};
        spectrum.signal_names      = {"surface"};
        spectrum.signal_quantities = {"magnitude"};
        spectrum.signal_units      = {""};
        spectrum.signal_values.resize(64UZ);
        for (std::size_t bin = 0UZ; bin < 64UZ; ++bin) {
            spectrum.axis_values[0].push_back(static_cast<float>(bin));
            spectrum.signal_values[bin] = static_cast<float>(bin) / 63.f;
        }
        state.surfaceSink->pushDataSet(std::move(spectrum));
    }
    std::ignore                     = charts::SinkRegistry::instance().registerSink(state.surfaceSink);
    state.surface                   = std::make_unique<charts::SurfacePlot>();
    state.surface->chart_name.value = "surface";
    state.surface->data_sinks.value = {"surface"};
    state.surface->n_history.value  = 8U;

    state.correlationX                 = uncertainSink("yy_x", 64UZ);
    state.correlationY                 = uncertainSink("yy_y", 64UZ);
    state.correlationY->metadata.color = 0x00C000;
    for (std::size_t i = 0UZ; i < 50UZ; ++i) {
        const auto t = static_cast<float>(i);
        state.correlationX->pushSample(1e-3 * static_cast<double>(i), t / 10.f, 0.03f);
        state.correlationY->pushSample(1e-3 * static_cast<double>(i), std::sin(t / 5.f), 0.2f);
    }
    std::ignore        = charts::SinkRegistry::instance().registerSink(state.correlationX);
    std::ignore        = charts::SinkRegistry::instance().registerSink(state.correlationY);
    const auto yyChart = [](std::string name, charts::ErrorStyle style) {
        auto chart               = std::make_unique<charts::YYChart>();
        chart->chart_name.value  = std::move(name);
        chart->data_sinks.value  = {"yy_x", "yy_y"};
        chart->show_legend.value = false;
        chart->error_style.value = style;
        return chart;
    };
    state.withBars    = yyChart("with bars", charts::ErrorStyle::bars);
    state.withoutBars = yyChart("without bars", charts::ErrorStyle::none);

    const bool result = app.runTests();
    state.gpu.reset();
    state.cpu.reset();
    state.withBand.reset();
    state.withoutBand.reset();
    state.comboDense.reset();
    state.comboSparse.reset();
    state.sparseWithoutBand.reset();
    state.surface.reset();
    state.withBars.reset();
    state.withoutBars.reset();
    std::ignore = charts::SinkRegistry::instance().unregisterSink("yy_x");
    std::ignore = charts::SinkRegistry::instance().unregisterSink("yy_y");
    std::ignore = charts::SinkRegistry::instance().unregisterSink("error_band");
    std::ignore = charts::SinkRegistry::instance().unregisterSink("error_sparse");
    std::ignore = charts::SinkRegistry::instance().unregisterSink("surface");
    std::ignore = charts::SinkRegistry::instance().unregisterSink("gpu_line");
    gState      = nullptr;
    return result ? 0 : 1;
}
