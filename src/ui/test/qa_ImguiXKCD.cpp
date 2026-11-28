#include "../common/ImguiXKCD.hpp" // first: the header must compile standalone

#include <boost/ut.hpp>

#include <cmath>
#include <vector>

using namespace boost::ut;

namespace {

constexpr float     kUtilityMaxV  = 0.03f; // atlas V below which a vertex counts as untextured
constexpr ImVec2    kUntexturedUv = {0.f, 0.f};
constexpr ImVec2    kGlyphUv      = {0.f, 0.5f};
constexpr ImU32     kColour       = IM_COL32(200, 40, 40, 255);
constexpr ImDrawIdx kLine         = 60U; // a polyline above minElemCount
constexpr ImDrawIdx kGlyphs       = 60U; // text quads above minElemCount
constexpr ImDrawIdx kAxis         = 4U;  // an axis line below minElemCount

/// one draw list: a polyline [0, 60), text [60, 120) and an axis [120, 124), each its own command
struct Scene {
    ImDrawList list{ImGui::GetDrawListSharedData()};
    ImDrawData data{};

    Scene() {
        const auto addVertices = [&](ImDrawIdx count, ImVec2 uv) {
            for (ImDrawIdx i = 0U; i < count; ++i) {
                list.VtxBuffer.push_back(ImDrawVert{.pos = {10.f + 7.f * static_cast<float>(list.VtxBuffer.Size), 300.f}, .uv = uv, .col = kColour});
            }
        };
        const auto addCommand = [&](ImDrawIdx first, ImDrawIdx count) {
            ImDrawCmd command{};
            command.IdxOffset = static_cast<unsigned int>(list.IdxBuffer.Size);
            command.ElemCount = count;
            for (ImDrawIdx i = 0U; i < count; ++i) {
                list.IdxBuffer.push_back(first + i); // ImDrawIdx is unsigned int here (src/ui/cmake/Dependencies.cmake)
            }
            list.CmdBuffer.push_back(command);
        };
        list.CmdBuffer.clear();
        addVertices(kLine, kUntexturedUv);
        addVertices(kGlyphs, kGlyphUv);
        addVertices(kAxis, kUntexturedUv);
        addCommand(0U, kLine);
        addCommand(kLine, kGlyphs);
        addCommand(kLine + kGlyphs, kAxis);
        data.Valid = true;
        data.CmdLists.push_back(&list); // not AddDrawList: it asserts the write pointers that only the primitive API keeps
        data.CmdListsCount = 1;
    }

    [[nodiscard]] std::vector<ImVec2> positions() const {
        std::vector<ImVec2> result;
        for (const ImDrawVert& vertex : list.VtxBuffer) {
            result.push_back(vertex.pos);
        }
        return result;
    }
};

[[nodiscard]] float distance(ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

void ensureContext() {
    if (ImGui::GetCurrentContext() == nullptr) {
        ImGui::CreateContext();
    }
}

} // namespace

const suite<"xkcd wobble on the CPU"> wobbleTests = [] {
    "untextured geometry moves, text and short commands do not"_test = [] {
        ensureContext();
        Scene      scene;
        const auto before = scene.positions();
        gr::od::xkcd::apply(&scene.data, kUtilityMaxV);
        const auto after = scene.positions();

        std::size_t moved = 0UZ;
        for (std::size_t i = 0UZ; i < kLine; ++i) {
            const float shift = distance(before[i], after[i]);
            expect(le(shift, std::sqrt(2.f) * gr::od::xkcd::amplitude)) << "vertex" << i;
            moved += shift > 0.01f ? 1UZ : 0UZ;
        }
        expect(eq(moved, kLine)) << "each vertex of the polyline wobbles";
        for (std::size_t i = kLine; i < before.size(); ++i) {
            expect(eq(before[i].x, after[i].x) and eq(before[i].y, after[i].y)) << "vertex" << i << "stays";
        }
    };

    "a vertex shared by two commands moves once, and the result is deterministic"_test = [] {
        ensureContext();
        Scene first;
        Scene second;
        second.list.IdxBuffer.back() = 0U; // the axis command now also indexes the polyline's vertex 0
        gr::od::xkcd::minElemCount   = 4U; // and is long enough to wobble
        gr::od::xkcd::apply(&first.data, kUtilityMaxV);
        gr::od::xkcd::apply(&second.data, kUtilityMaxV);
        gr::od::xkcd::minElemCount = 50U;
        expect(eq(first.list.VtxBuffer[0].pos.x, second.list.VtxBuffer[0].pos.x) and eq(first.list.VtxBuffer[0].pos.y, second.list.VtxBuffer[0].pos.y));
    };

    "disabled leaves every vertex in place"_test = [] {
        ensureContext();
        Scene      scene;
        const auto before     = scene.positions();
        gr::od::xkcd::enabled = false;
        gr::od::xkcd::apply(&scene.data, kUtilityMaxV);
        gr::od::xkcd::enabled = true;
        const auto after      = scene.positions();
        for (std::size_t i = 0UZ; i < before.size(); ++i) {
            expect(eq(before[i].x, after[i].x) and eq(before[i].y, after[i].y)) << "vertex" << i;
        }
    };
};

// suites run here, not at exit, where the ImGui context they use may already be destroyed
int main() { return boost::ut::cfg<boost::ut::override>.run(); }
