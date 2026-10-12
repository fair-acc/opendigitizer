#ifndef OPENDIGITIZER_UI_STATUSBARVIEW_HPP
#define OPENDIGITIZER_UI_STATUSBARVIEW_HPP

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>
#include <gnuradio-4.0/algorithm/fileio/FileIo.hpp>

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

    std::optional<gr::log::Level> _levelFilter;
    std::string                   _messageFilter;

    struct SaveData {
        std::string                                  pendingText;
        std::string                                  path;
        std::string                                  writerPath;
        std::optional<gr::algorithm::fileio::Writer> writer;
    };

    std::optional<SaveData> _saving;

    void drawLogLine(float width);
    void drawLogPopup();
    void drawSavePopup();
    void openSavePopup(std::string text);
    void pollSaveResult();

    [[nodiscard]] bool        startSavePendingTextToFile();
    [[nodiscard]] std::string selectedOrAllAsText(const std::vector<gr::log::LogRecord>& records, bool selectedOnly) const;
};

} // namespace DigitizerUi

#endif
