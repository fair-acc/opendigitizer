#ifndef OPENDIGITIZER_UI_STATUSBARVIEW_HPP
#define OPENDIGITIZER_UI_STATUSBARVIEW_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include <imgui.h>

#include "GraphSession.hpp"
#include "PaneBlocks.hpp"

namespace DigitizerUi {

class StatusBarView {
public:
    static constexpr std::chrono::seconds kInfoDotLifetime{5};

    std::string version = "app/grc version";
    std::string versionDetails;

    void draw(GraphSession* session); // nullptr: log only

    [[nodiscard]] static float height() noexcept;
    [[nodiscard]] static bool  isInfoDotLit(std::uint64_t latestInfoOrDebugNanos, std::uint64_t nowNanos) noexcept;

private:
    PaneBlocks                 _blocks{gr::UICategory::StatusBar};
    float                      _blocksWidth = 0.f; // last frame's
    ImGuiSelectionBasicStorage _selectedRecords;

    void drawLogLine(float width);
    void drawLogPopup();

    [[nodiscard]] std::string selectedOrAllAsText(const std::vector<gr::log::LogRecord>& records) const;
};

} // namespace DigitizerUi

#endif
