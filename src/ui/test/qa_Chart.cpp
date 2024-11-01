#include "imgui.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/PopupMenu.hpp"
#include <boost/ut.hpp>

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>
#include <blocks/ImPlotSink.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Scheduler.hpp>

//TODO: blocks are locally included/registered for this test -> should become a global feature
#include "gnuradio-4.0/basic/FunctionGenerator.hpp"
#include "gnuradio-4.0/basic/clock_source.hpp"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ui_test_assets);

using namespace boost;
using namespace boost::ut;

struct TestApp : public DigitizerUi::test::ImGuiTestApp {
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        struct TestState {
            bool pressed = false;
        };

        ImGuiTest* t = IM_REGISTER_TEST(engine(), "chart_dashboard", "test1");
        t->SetVarsDataType<TestState>();

        t->GuiFunc = [](ImGuiTestContext* ctx) {
            // init GR graph elements
            gr::PluginLoader loader(gr::globalBlockRegistry(), {});
            gr::Graph        testGraph;
            try {
                auto grcFile = cmrc::ui_test_assets::get_filesystem().open("examples/fg_dipole_intensity_ramp.grc");
                testGraph    = loadGrc(loader, std::string(grcFile.begin(), grcFile.end()));
            } catch (const std::exception& e) {
                fmt::println(std::cerr, "GRC loading failed: {}", e);
                expect(false);
            } catch (const std::string& e) {
                fmt::println(std::cerr, "GRC loading failed: {}", e);
                expect(false);
            } catch (...) {
                fmt::println(std::cerr, "GRC loading failed with unknown error");
            }

            // init ImGui elements
            ImGui::Begin("Test Window", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowPos({0, 0});
            ImGui::SetWindowSize(ImVec2(500, 500));

            DigitizerUi::VerticalPopupMenu<1> menu;

            if (!menu.isOpen()) {
                menu.addButton("button", [ctx] {
                    auto& vars   = ctx->GetVars<TestState>();
                    vars.pressed = true;
                });
            }



            gr::BlockModel* sink = testGraph.blocks().back().get();
            fmt::println("sink block name: {}", sink->name());

            gr::scheduler::Simple sched{std::move(testGraph)};
            std::jthread          uiThread([&sched, &sink] {
                fmt::println("starting uiThread");
                while (sched.state() != gr::lifecycle::STOPPED) {
                    if (ImPlot::BeginPlot("Line Plot")) {
                        gr::work::Status success = sink->draw();
                        fmt::println("sink {} - draw: {}", sink->name(), magic_enum::enum_name(success));
                        ImPlot::EndPlot();
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(40));
                }
                fmt::println("finished uiThread");
            });

            fmt::println("starting scheduler");
            expect(sched.runAndWait().has_value());
            uiThread.join();
            fmt::println("finished scheduler");

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            "my test"_test = [ctx] {
                ctx->SetRef("Test Window");
                const ImGuiID popupId = ctx->PopupGetWindowID("MenuPopup_1");
                captureScreenshot(*ctx);

                ctx->SetRef(popupId);
                ctx->ItemClick("button");

                auto& vars = ctx->GetVars<TestState>();
                expect(vars.pressed);
            };
        };
    }
};

int main(int, char**) {
    TestApp app({.useInteractiveMode = false, .screenshotPrefix = "chart"});
    return app.runTests() ? 0 : 1;
}