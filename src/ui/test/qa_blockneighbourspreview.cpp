#include "GraphModel.hpp"
#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui_test_engine/imgui_te_context.h"

#include "components/Block.hpp"
#include "components/BlockNeighboursPreview.hpp"
#include <boost/ut.hpp>
#include <memory>
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

    auto block       = std::make_unique<DigitizerUi::UiGraphBlock>(&graphModel);
    block->blockName = "Test Block";

    auto sourceBlock       = std::make_unique<DigitizerUi::UiGraphBlock>(&graphModel);
    sourceBlock->blockName = "Source";

    auto destBlock       = std::make_unique<DigitizerUi::UiGraphBlock>(&graphModel);
    destBlock->blockName = "Destination";

    auto& blocks = graphModel.blocks();
    blocks.clear();
    blocks.push_back(std::move(block));
    blocks.push_back(std::move(sourceBlock));
    blocks.push_back(std::move(destBlock));

    g_context.graphModel = &graphModel;

    // Get pointers to blocks from the vector since we moved our local pointers
    auto* mainBlock = graphModel.blocks()[0].get();
    auto* srcBlock  = graphModel.blocks()[1].get();
    auto* dstBlock  = graphModel.blocks()[2].get();

    DigitizerUi::UiGraphPort input(mainBlock);
    input.portName      = "Input";
    input.portDirection = gr::PortDirection::INPUT;
    mainBlock->inputPorts.push_back(input);
    DigitizerUi::UiGraphPort output(mainBlock);
    output.portName      = "Output";
    output.portDirection = gr::PortDirection::OUTPUT;
    mainBlock->outputPorts.push_back(output);

    DigitizerUi::UiGraphPort sourceOut(srcBlock);
    sourceOut.portName      = "sourceOut";
    sourceOut.portDirection = gr::PortDirection::OUTPUT;
    srcBlock->outputPorts.push_back(sourceOut);
    DigitizerUi::UiGraphPort sinkIn(dstBlock);
    sinkIn.portName      = "sinkIn";
    sinkIn.portDirection = gr::PortDirection::INPUT;
    dstBlock->inputPorts.push_back(sinkIn);

    // Edges:
    DigitizerUi::UiGraphEdge inEdge(&graphModel);
    inEdge.edgeSourcePort      = &srcBlock->outputPorts.back();
    inEdge.edgeDestinationPort = &mainBlock->inputPorts.back();
    graphModel.edges().push_back(inEdge);
    DigitizerUi::UiGraphEdge outEdge(&graphModel);
    outEdge.edgeSourcePort      = &mainBlock->outputPorts.back();
    outEdge.edgeDestinationPort = &dstBlock->inputPorts.back();
    graphModel.edges().push_back(outEdge);

    g_context.graphModel           = &graphModel;
    g_context.block                = mainBlock;
    g_context.blockClickedCallback = [](DigitizerUi::UiGraphBlock*) { std::print("Block clicked.\n"); };

    TestApp app(options);
    return app.runTests() ? 0 : 1;
}
