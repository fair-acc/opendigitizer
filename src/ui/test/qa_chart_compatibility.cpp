#include "ImGuiTestApp.hpp"

#include <boost/ut.hpp>

#include <Dashboard.hpp>
#include <DashboardPage.hpp>

#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>
#include <gnuradio-4.0/Profiler.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/meta/formatter.hpp>

#include <GrBasicBlocks.hpp>
#include <GrTestingBlocks.hpp>

#include "blocks/ImPlotSink.hpp"

#include <cmrc/cmrc.hpp>
#include <memory>

#include "../components/ColourManager.hpp"

CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;
using namespace opendigitizer::charts;

struct TestState {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::shared_ptr<DigitizerUi::Dashboard>      dashboard;
    // this test retains dashboardPage because we need it to see its _pendingTransmutation from the previous frame
    std::shared_ptr<DigitizerUi::DashboardPage> dashboardPage;

    void stopScheduler() { std::ignore = dashboard->scheduler->stop(); }

    void waitForSchedulerActive(std::size_t maxCount = 30UZ, std::source_location location = std::source_location::current()) {
        std::size_t count = 0;
        while (!gr::lifecycle::isActive(dashboard->scheduler->state()) && count < maxCount) {
            dashboard->handleMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            count++;
        }
        if (count >= maxCount) {
            throw gr::exception(std::format("waitForSchedulerActive({}): maxCount exceeded", count), location);
        }
    }
};

TestState g_state;

template<typename Registry>
void registerTestBlocks(Registry& registry) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
    gr::registerBlock<opendigitizer::ImPlotSink, float, gr::DataSet<float>>(registry);
#pragma GCC diagnostic pop
}

DigitizerUi::Dashboard::UIWindow* findWindowByName(std::string_view windowName) {
    for (auto& w : g_state.dashboard->uiWindows) {
        if (w.window && w.window->name == windowName) {
            return &w;
        }
    }
    return nullptr;
}

opendigitizer::SignalKind getChartBlockSignalKinds(const gr::BlockModel& block) {
    auto settings = block.settings().get();
    auto iter     = settings.find("data_sinks");
    auto out      = opendigitizer::SignalKind::None;
    if (iter == std::end(settings)) {
        return out;
    }
    auto namesValues = iter->second.get_if<gr::Tensor<gr::pmt::Value>>();
    if (!namesValues) {
        return out;
    }
    std::vector<std::string> names;
    for (auto& pmtValue : *namesValues) {
        names.emplace_back(pmtValue.value_or(std::string_view{}));
    }
    SinkRegistry::instance().forEach([&out, &names](const SignalSink& sink) {
        auto found = std::ranges::find(names, sink.name());
        if (found != std::end(names)) {
            out = out | sink.signalKind();
        }
    });
    return out;
}

bool blockHasSink(const gr::BlockModel& block, std::string_view sinkName) {
    auto settings = block.settings().get();
    auto it       = settings.find("data_sinks");
    if (it == settings.end()) {
        return false;
    }
    auto sinkNames = it->second.value_or(gr::Tensor<gr::pmt::Value>{});
    for (std::size_t i = 0; i < sinkNames.size(); ++i) {
        if (sinkNames[i].value_or(std::string{}).find(sinkName) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::size_t blockSinkCount(const gr::BlockModel& block) {
    auto settings = block.settings().get();
    auto it       = settings.find("data_sinks");
    if (it == settings.end()) {
        return 0;
    }
    const auto* tensor = it->second.get_if<gr::Tensor<gr::pmt::Value>>();
    return tensor ? tensor->size() : 0UL;
}

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_compatibility", "signal type compatibility");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext*) {
            IMW::Window window("Test Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(1200, 800));

            if (g_state.dashboard) {
                if (!g_state.dashboardPage) {
                    g_state.dashboardPage = std::make_shared<DigitizerUi::DashboardPage>();
                    g_state.dashboardPage->setDashboard(*g_state.dashboard.get());
                }
                g_state.dashboardPage->draw(DigitizerUi::DashboardPage::Mode::Interaction);
                ut::expect(!g_state.dashboard->uiWindows.empty());
            }
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ctx->SetRef("Test Window");

            auto& sinkRegistry = SinkRegistry::instance();

            auto dipoleSink           = sinkRegistry.findSink([](const auto& s) { return s.name() == "DipoleCurrentSink"; });
            auto intensitySink        = sinkRegistry.findSink([](const auto& s) { return s.name() == "IntensitySink"; });
            auto dipoleDataSetSink    = sinkRegistry.findSink([](const auto& s) { return s.name() == "DipoleCurrentDataSetSink"; });
            auto intensityDataSetSink = sinkRegistry.findSink([](const auto& s) { return s.name() == "IntensityDataSetSink"; });

            "all sinks registered"_test = [&] {
                expect(dipoleSink != nullptr) << "DipoleCurrentSink not found";
                expect(intensitySink != nullptr) << "IntensitySink not found";
                expect(dipoleDataSetSink != nullptr) << "DipoleCurrentDataSetSink not found";
                expect(intensityDataSetSink != nullptr) << "IntensityDataSetSink not found";
            };

            if (!dipoleSink || !intensitySink || !dipoleDataSetSink || !intensityDataSetSink) {
                g_state.stopScheduler();
                return;
            }

            "expected signal kinds per sink"_test = [&] {
                expect(dipoleSink->signalKind() == opendigitizer::SignalKind::Streaming);
                expect(intensitySink->signalKind() == opendigitizer::SignalKind::Streaming);
                expect(dipoleDataSetSink->signalKind() == opendigitizer::SignalKind::Dataset1D);
                expect(intensityDataSetSink->signalKind() == opendigitizer::SignalKind::Dataset1D);
            };

            g_state.waitForSchedulerActive();

            while (dipoleDataSetSink->dataSetCount() == 0 || dipoleSink->size() == 0) {
                ctx->Yield();
            }

            g_state.stopScheduler();

            "expected UIWindow names"_test = [] {
                expect(findWindowByName("Plot 1") != nullptr) << "Plot 1 window";
                expect(findWindowByName("Plot 2") != nullptr) << "Plot 2 window";
                expect(findWindowByName("DataSinkPlot") != nullptr) << "DataSinkPlot window";
            };

            "change Plot 2 type (via dashboard) while it has sinks"_test = [&ctx] {
                auto* plot2 = findWindowByName("Plot 2");
                expect(plot2 != nullptr && plot2->block != nullptr) << "Plot 2 must exist";
                expect(blockSinkCount(*plot2->block) == 2) << "Plot 2 should have two sinks initially";
                expect(getChartBlockSignalKinds(*plot2->block) == opendigitizer::SignalKind::Streaming);
                expect(g_state.dashboard->transmuteUIWindow(*plot2, "opendigitizer::charts::YYChart"));
                ctx->Yield();

                auto* plot2After = findWindowByName("Plot 2");
                expect(plot2After != nullptr && plot2After->block != nullptr) << "Plot 2 should still exist after changing type";
                expect(plot2After->block->typeName().find("YYChart") != std::string::npos) << "Plot 2 should now be a YYChart";
                expect(getChartBlockSignalKinds(*plot2After->block) == opendigitizer::SignalKind::Streaming);
                expect(blockSinkCount(*plot2After->block) == 2) << "plot 2's two sinks should be preserved";
                captureScreenshot(*ctx);
            };

            "change Plot 1 chart type via UI"_test = [&ctx] {
                auto* plot1 = findWindowByName("Plot 1");
                expect(plot1 && plot1->block);

                expect(getChartBlockSignalKinds(*plot1->block) == opendigitizer::SignalKind::Streaming) << "Plot 1 should only have streaming sources";

                expect(plot1->block->typeName().find("XYChart") != std::string_view::npos) << "Expected Plot 1 type to initially be XYChart";

                auto winInfo = ctx->WindowInfo(std::format("//{}", plot1->window->name).c_str());
                expect(winInfo.ID);
                ctx->ItemClick(winInfo.ID, ImGuiMouseButton_Right);

                ImGuiContext& g = *GImGui;
                expect(g.OpenPopupStack.Size > 0) << "context menu popup should have opened";
                if (g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window) {
                    const auto contextMenuID = g.OpenPopupStack.back().Window->ID;
                    ctx->SetRef(contextMenuID);

                    ctx->MenuClick("Change Type");
                    ctx->Yield();

                    const auto submenuID = g.OpenPopupStack.back().Window->ID;
                    expect(submenuID && submenuID != contextMenuID) << "Context submenu for changing chart type should have opened";
                    ctx->SetRef(submenuID);

                    auto chartTypes = registeredChartTypes();
                    for (const auto& type : chartTypes) {
                        if (type.find("YYChart") != std::string::npos) {
                            ctx->ItemClick(type.c_str());
                            break;
                        }
                    }
                    ctx->Yield();
                }

                expect(getChartBlockSignalKinds(*findWindowByName("Plot 1")->block) == opendigitizer::SignalKind::Streaming) << "Plot 1 should *still* only have streaming sources";

                expect(findWindowByName("Plot 1")->block->typeName().find("YYChart") != std::string_view::npos) << "Block type name should now contain YYChart";

                ctx->PopupCloseAll();
                ctx->SetRef("Test Window");
                captureScreenshot(*ctx);
            };

            "incompatible chart types are disabled in the change type menu"_test = [&ctx] {
                // this test tries to use the UI to change type to YYChart and makes sure that the button is disabled
                auto* dataSinkPlot = findWindowByName("DataSinkPlot");
                expect(dataSinkPlot && dataSinkPlot->block);

                const std::string originalBlockType(dataSinkPlot->block->typeName());

                expect(getChartBlockSignalKinds(*dataSinkPlot->block) == opendigitizer::SignalKind::Dataset1D) << "DataSinkPlot should only have dataset sources";

                auto winInfo = ctx->WindowInfo(std::format("//{}", dataSinkPlot->window->name).c_str());
                expect(winInfo.ID);
                ctx->ItemClick(winInfo.ID, ImGuiMouseButton_Right);

                ImGuiContext& g = *GImGui;
                expect(g.OpenPopupStack.Size > 0) << "context menu popup should have opened";
                if (g.OpenPopupStack.Size > 0 && g.OpenPopupStack.back().Window) {
                    const auto contextMenuID = g.OpenPopupStack.back().Window->ID;
                    ctx->SetRef(contextMenuID);

                    ctx->MenuClick("Change Type");
                    ctx->Yield();

                    const auto submenuID = g.OpenPopupStack.back().Window->ID;
                    expect(submenuID && submenuID != contextMenuID) << "Context submenu for changing chart type should have opened";
                    ctx->SetRef(submenuID);

                    auto chartTypes = registeredChartTypes();
                    for (const auto& type : chartTypes) {
                        if (type.find("YYChart") != std::string::npos) {
                            ctx->ItemClick(type.c_str());
                            break;
                        }
                    }
                    ctx->Yield();
                }

                expect(getChartBlockSignalKinds(*findWindowByName("DataSinkPlot")->block) == opendigitizer::SignalKind::Dataset1D) << "DataSinkPlot should *still* only have dataset sources";
                expect(findWindowByName("DataSinkPlot")->block->typeName() == originalBlockType) << "Block type name should not have changed";

                ctx->PopupCloseAll();
                ctx->SetRef("Test Window");
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    [[maybe_unused]] opendigitizer::ColourManager& colourManager = opendigitizer::ColourManager::instance();

    auto& registry = gr::globalBlockRegistry();

    gr::blocklib::initGrBasicBlocks(registry);
    gr::blocklib::initGrTestingBlocks(registry);
    registerTestBlocks(registry);

    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "chart_compat";

    options.speedMode = ImGuiTestRunSpeed_Normal;
    TestApp app(options);

    app.initImGui();

    auto fs      = cmrc::ui_test_assets::get_filesystem();
    auto grcFile = fs.open("examples/fg_dipole_intensity_ramp.grc");

    auto dashboardDescription = DigitizerUi::DashboardDescription::createEmpty("empty");
    g_state.dashboard         = DigitizerUi::Dashboard::create(g_state.restClient, dashboardDescription);
    g_state.dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), //
        [](gr::Graph&& grGraph) {                                               //
            g_state.dashboard->emplaceGraph(std::move(grGraph));
        });

    auto result = app.runTests();
    g_state.dashboard.reset();
    return result ? 0 : 1;
}
