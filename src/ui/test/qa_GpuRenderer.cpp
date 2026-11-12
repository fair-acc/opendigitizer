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

using namespace boost::ut;

namespace {
using opendigitizer::charts::XYChart;
using opendigitizer::test::TestStreamingSink;

constexpr ImVec2 kGpuPos{50.f, 50.f};
constexpr ImVec2 kCpuPos{750.f, 50.f};
constexpr ImVec2 kChartSize{640.f, 400.f};

struct TestState {
    std::shared_ptr<TestStreamingSink> sink;
    std::unique_ptr<XYChart>           gpu;
    std::unique_ptr<XYChart>           cpu;
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
    IMW::Window window(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    std::ignore = chart.draw(opendigitizer::charts::chartDrawConfig(opendigitizer::charts::ChartMode::View));
}

void push(std::size_t to) {
    for (std::size_t i = gState->sink->totalSampleCount(); i < to; ++i) {
        const auto t = static_cast<double>(i);
        gState->sink->pushSample(1e-3 * t, static_cast<float>((0.3 + 0.6 * t / 1100.0) * std::sin(std::numbers::pi * t / 100.0)));
    }
}

void yieldUntil(ImGuiTestContext* ctx, auto condition) {
    while (!condition()) {
        ctx->Yield();
    }
}

using Rows = std::vector<std::optional<std::pair<int, int>>>;

Rows lineRows(const DigitizerUi::test::ImGuiTestApp::CapturedPixels& image, int left) {
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

DigitizerUi::test::ImGuiTestApp::CapturedPixels settledCapture(ImGuiTestContext* ctx) {
    const auto capture = [ctx] { return DigitizerUi::test::ImGuiTestApp::capturePixels(*ctx, ImRect(kGpuPos, kCpuPos + kChartSize)); };
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
        DigitizerUi::test::ImGuiTestApp::captureScreenshot(*ctx, ImRect(kGpuPos, kCpuPos + kChartSize));
    }
    expect(differences.empty()) << std::format("{} samples: {} columns differ, first: {}", total, differences.size(), differences.empty() ? std::string() : differences.front());
    expect(compared > 300UZ) << std::format("{} samples: {} columns with a line", total, compared) << fatal;
}
} // namespace

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

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
                    yieldUntil(ctx, [&] { return (gpu._gpuLineProgram._compileAttempted && gpu._gpuLineProgram.handles.program == 0) || (!gpu._gpuLines.empty() && gpu._gpuLines.begin()->second._uploadedEnd == total); });
                    expect(!gpu._gpuLines.empty()) << "the GPU path draws" << fatal;
                    expectMatch(ctx, total);
                }
            };
        };
    }
};

int main(int argc, char* argv[]) {
    TestState state;
    gState = std::addressof(state);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "gpu_vs_cpu_render";
    TestApp app(options);
    app.initImGui();

    state.sink = std::make_shared<TestStreamingSink>("gpu_line", 1024UZ);
    state.sink->setColor(0x00C000);
    std::ignore = opendigitizer::charts::SinkRegistry::instance().registerSink(state.sink);
    state.gpu   = makeChart("gpu", true);
    state.cpu   = makeChart("cpu", false);

    const bool result = app.runTests();
    state.gpu.reset();
    state.cpu.reset();
    std::ignore = opendigitizer::charts::SinkRegistry::instance().unregisterSink("gpu_line");
    gState      = nullptr;
    return result ? 0 : 1;
}
