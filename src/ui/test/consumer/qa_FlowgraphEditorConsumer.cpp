#include "FlowgraphPage.hpp"

#include <boost/ut.hpp>

using namespace boost::ut;

static_assert(
    requires(DigitizerUi::FlowgraphEditor& editor) {
        editor.showEditorControls = false;
        editor.requestRelayout();
    }, "an embedder can hide an editor's controls and request a relayout");

const suite<"FlowgraphEditor consumer"> _consumer = [] {
    "an embedder hides the page's editor controls and requests a relayout before any editor exists"_test = [] {
        DigitizerUi::FlowgraphPage page;
        expect(page.showEditorControls) << "shown by default";
        page.showEditorControls = false;
        page.requestRelayout();
        expect(eq(page.editorCount(), 0UZ));
        expect(!page.showEditorControls);
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
