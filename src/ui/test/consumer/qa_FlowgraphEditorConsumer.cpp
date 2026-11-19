#include "FlowgraphPage.hpp"

#include <boost/ut.hpp>

using namespace boost::ut;

static_assert(
    requires(gr::od::FlowgraphEditor& editor) {
        editor.showEditorControls = false;
        editor.requestRelayout();
    }, "an embedder can hide an editor's controls and request a relayout");

const suite<"FlowgraphEditor consumer"> consumerTests = [] {
    "an embedder hides the page's editor controls and requests a relayout before any editor exists"_test = [] {
        gr::od::FlowgraphPage page;
        expect(page.showEditorControls) << "shown by default";
        page.showEditorControls = false;
        page.requestRelayout();
        expect(eq(page.editorCount(), 0UZ));
        expect(!page.showEditorControls);
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
