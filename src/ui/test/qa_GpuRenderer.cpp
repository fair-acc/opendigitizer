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

    // the density colour pass, read back from its image on the render thread
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

void drawChart(const char* name, ImVec2 pos, XYChart& chart) {
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
            const unsigned int pixel = image.rgba[static_cast<std::size_t>(y * image.width + left) + column];
            if (((pixel >> 8U) & 0xFFU) > 100U && (pixel & 0xFFU) < 80U && ((pixel >> 16U) & 0xFFU) < 80U) {
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
// ground truth by construction: a constant spectrum at 5 in rows of 1 unit from yMax = 10 hits row 5 of every column; with
// peak normalisation that cell is at full scale (the colormap's last entry) and every other cell is empty (transparent)
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

    const bool result = app.runTests();
    state.gpu.reset();
    state.cpu.reset();
    std::ignore = charts::SinkRegistry::instance().unregisterSink("gpu_line");
    gState      = nullptr;
    return result ? 0 : 1;
}
