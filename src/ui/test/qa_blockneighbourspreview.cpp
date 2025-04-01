#include "GraphModel.hpp"
#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/Block.hpp"
#include "components/BlockNeighboursPreview.hpp"
#include <boost/ut.hpp>
#include <string>

#include "ImGuiTestApp.hpp"

using namespace boost;

DigitizerUi::components::BlockControlsPanelContext g_context;

class TestApp : public DigitizerUi::test::ImGuiTestApp {
private:
    using DigitizerUi::test::ImGuiTestApp::ImGuiTestApp;

    void registerTests() override {
        ImGuiTest* t = IM_REGISTER_TEST(engine(), "blockneighbourspreview", "basic");

        t->GuiFunc = [](ImGuiTestContext*) {
            // black edges on black background isn't readable, use some other color:
            DigitizerUi::IMW::StyleColor style(ImGuiCol_WindowBg, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));

            ImGui::Begin("Test Window", NULL, ImGuiWindowFlags_NoSavedSettings);
            ImGui::SetWindowSize(ImVec2(500, 400));

            DigitizerUi::components::BlockNeighboursPreview(g_context, ImGui::GetContentRegionAvail());

            ImGui::End();
        };

        t->TestFunc = [](ImGuiTestContext* ctx) {
            ut::test("block neighbours preview") = [ctx] {
                ctx->SetRef("Test Window");
                captureScreenshot(*ctx);
            };
        };
    }
};

int main(int argc, char* argv[]) {
    auto options             = DigitizerUi::test::TestOptions::fromArgs(argc, argv);
    options.screenshotPrefix = "blockneighbourspreview";

    DigitizerUi::UiGraphModel graphModel;

    // So styling can be accessed:
    ax::NodeEditor::Config config;
    config.SettingsFile = nullptr;
    ax::NodeEditor::CreateEditor(&config);
    ax::NodeEditor::SetCurrentEditor(ax::NodeEditor::CreateEditor(&config));

    DigitizerUi::UiGraphBlock block(&graphModel);
    block.blockName = "Test Block";

    DigitizerUi::UiGraphBlock sourceBlock(&graphModel);
    sourceBlock.blockName = "Source";

    DigitizerUi::UiGraphBlock destBlock(&graphModel);
    destBlock.blockName = "Destination";

    auto& blocks = graphModel.blocks();
    blocks       = {block, sourceBlock, destBlock};

    g_context.graphModel = &graphModel;

    DigitizerUi::UiGraphPort input(&block);
    input.portName      = "Input";
    input.portDirection = gr::PortDirection::INPUT;
    block.inputPorts.push_back(input);
    DigitizerUi::UiGraphPort output(&block);
    output.portName      = "Output";
    output.portDirection = gr::PortDirection::OUTPUT;
    block.outputPorts.push_back(output);

    DigitizerUi::UiGraphPort sourceOut(&sourceBlock);
    input.portName      = "sourceOut";
    input.portDirection = gr::PortDirection::OUTPUT;
    sourceBlock.outputPorts.push_back(input);
    DigitizerUi::UiGraphPort sinkIn(&destBlock);
    output.portName      = "sinkIn";
    output.portDirection = gr::PortDirection::INPUT;
    block.inputPorts.push_back(output);

    // Edges:
    DigitizerUi::UiGraphEdge inEdge(&graphModel);
    inEdge.edgeSourcePort      = &sourceOut;
    inEdge.edgeDestinationPort = &input;
    graphModel.edges().push_back(inEdge);
    DigitizerUi::UiGraphEdge outEdge(&graphModel);
    outEdge.edgeSourcePort      = &output;
    outEdge.edgeDestinationPort = &sinkIn;
    graphModel.edges().push_back(outEdge);

    g_context.graphModel           = &graphModel;
    g_context.block                = &block;
    g_context.blockClickedCallback = [](DigitizerUi::UiGraphBlock*) { fmt::print("Block clicked.\n"); };

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
