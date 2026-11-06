#ifndef OPENDIGITIZER_BLOCKS_PIANO_CONTROL_HPP
#define OPENDIGITIZER_BLOCKS_PIANO_CONTROL_HPP

#include "ImControl.hpp"
#include "common/LookAndFeel.hpp"

#include <imgui.h>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/annotated.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace opendigitizer {

GR_REGISTER_BLOCK(opendigitizer::PianoControl)

struct PianoControl : DigitizerUi::ImControl<PianoControl> {
    using Description = gr::Doc<"UI control that draws a piano keyboard">;

    gr::Annotated<std::string, "signal_generator_name", gr::Visible, gr::Doc<"Unique name of the block which will have its \"frequency\" field changed by the piano">> signal_generator_name;
    gr::Annotated<gr::Size_t, "octave", gr::Visible, gr::Doc<"Octave of the first key. Octave 4 is middle C">>                                                         octave              = 4;
    gr::Annotated<gr::Size_t, "octave_count", gr::Visible, gr::Doc<"Number of octaves drawn">, gr::Limits<1, 10>>                                                      octave_count        = 2;
    const std::string                                                                                                                                                  compatible_type_map = "bool";

    static constexpr float  kBlackKeyProportionToWhiteKey = 0.6f;
    static constexpr ImVec4 kColorHoveredBlackKey         = {.3f, .3f, .3f, 1.f};
    static constexpr ImVec4 kColorHoveredWhiteKey         = {.85f, .85f, .85f, 1.f};
    static constexpr ImVec4 kColorIdleBlackKey            = {.05f, .05f, .05f, 1.f};
    static constexpr ImVec4 kColorIdleWhiteKey            = {1.f, 1.f, 1.f, 1.f};

    GR_MAKE_REFLECTABLE(PianoControl, signal_generator_name, octave, octave_count, compatible_type_map);

    explicit PianoControl(gr::property_map initParameters = {}) : DigitizerUi::ImControl<PianoControl>(std::move(initParameters)) {}

    using midi_note_t = int;
    struct Key {
        midi_note_t midiNote   = 0;
        bool        isBlackKey = false;
        ImVec2      min;
        ImVec2      max;

        [[nodiscard]] bool contains(ImVec2 pos) const noexcept { return pos.x >= min.x && pos.x < max.x && pos.y >= min.y && pos.y < max.y; }
    };

    [[nodiscard]] static constexpr bool isBlackKey(midi_note_t note) noexcept {
        constexpr std::array<bool, 12> lookupTable{false, true, false, true, false, false, true, false, true, false, true, false};
        return lookupTable[static_cast<std::size_t>(note) % lookupTable.size()];
    }

    [[nodiscard]] static float noteFrequency(midi_note_t note) noexcept { return 440.f * std::exp2(static_cast<float>(note - 69) / 12.f); }
    [[nodiscard]] midi_note_t  firstNote() const noexcept { return 12 * (static_cast<midi_note_t>(octave) + 1); }
    [[nodiscard]] std::size_t  whiteKeyCount() const noexcept { return 7U * octave_count; }

    /// Returns a bunch of Key structs which describe the area and color the keys should be drawn
    [[nodiscard]] std::vector<Key> calculateKeyLocations(ImVec2 origin, float whiteKeyWidth, float height) const {
        const float blackKeyWidth  = kBlackKeyProportionToWhiteKey * whiteKeyWidth;
        const float blackKeyHeight = kBlackKeyProportionToWhiteKey * height;

        std::vector<Key>  keys;
        std::size_t       whiteKeysToTheLeft = 0;
        const midi_note_t noteRangeEnd       = firstNote() + 12 * static_cast<midi_note_t>(octave_count);
        for (midi_note_t note = firstNote(); note < noteRangeEnd; ++note) {
            if (isBlackKey(note)) {
                const float x = origin.x + static_cast<float>(whiteKeysToTheLeft) * whiteKeyWidth - .5f * blackKeyWidth;
                keys.push_back({.midiNote = note, .isBlackKey = true, .min = {x, origin.y}, .max = {x + blackKeyWidth, origin.y + blackKeyHeight}});
            } else {
                // the size of the keyboard and the way the cursor advances is determined by white keys, the black keys are sort of like ornaments/overlays
                const float x = origin.x + static_cast<float>(whiteKeysToTheLeft) * whiteKeyWidth;
                keys.push_back({.midiNote = note, .isBlackKey = false, .min = {x, origin.y}, .max = {x + whiteKeyWidth, origin.y + height}});
                ++whiteKeysToTheLeft;
            }
        }
        return keys;
    }

    gr::work::Status draw(const gr::property_map& config = {}) noexcept {
        const auto          parsedConfig  = DigitizerUi::DrawableBlockConfig::fromMap(config);
        const float         whiteKeyWidth = .8f * ImGui::GetFrameHeight();
        const float         height        = DigitizerUi::LookAndFeel::instance().dashboardStyle.toolbarRowHeight - ImGui::GetStyle().ItemSpacing.y;
        const ImVec2        size          = {whiteKeyWidth * static_cast<float>(whiteKeyCount()), height};
        const SizeAndLayout layout        = prepareDraw(parsedConfig, size.x, size.x, labelWidth());
        drawWithLabelAndTooltip(layout, [&](float) { drawKeys(whiteKeyWidth, size); });
        return gr::work::Status::OK;
    }

    void drawKeys(float whiteWidth, ImVec2 size) {
        const ImVec2     origin = ImGui::GetCursorScreenPos();
        std::vector<Key> keys   = calculateKeyLocations(origin, whiteWidth, size.y);
        const ImGuiIO&   io     = ImGui::GetIO();

        std::ranges::stable_partition(keys, &Key::isBlackKey); // prioritize black keys when getting note under cursor to effectively layer them on top
        const auto getNoteAt = [&](ImVec2 pos) -> std::optional<midi_note_t> {
            const auto it = std::ranges::find_if(keys, [pos](const Key& key) { return key.contains(pos); });
            return it == keys.end() ? std::nullopt : std::optional(it->midiNote);
        };

        // treat the entire keyboard as a single button and handle which key specifically is being pressed internally, this way you can drag the mouse to slide up the keyboard
        std::ignore                                          = ImGui::InvisibleButton("##piano", size);
        const bool                       wasActiveLastFrame  = (ImGui::IsItemActive() && !ImGui::IsItemActivated()) || ImGui::IsItemDeactivated();
        const ImVec2                     mousePosLastFrame   = {io.MousePos.x - io.MouseDelta.x, io.MousePos.y - io.MouseDelta.y};
        const std::optional<midi_note_t> notePlayedLastFrame = wasActiveLastFrame ? getNoteAt(mousePosLastFrame) : std::nullopt;
        const std::optional<midi_note_t> currentNote         = ImGui::IsItemActive() ? getNoteAt(io.MousePos) : std::nullopt;
        const std::optional<midi_note_t> hoveredNote         = ImGui::IsItemHovered() ? getNoteAt(io.MousePos) : std::nullopt;

        if (currentNote != notePlayedLastFrame) {
            if (currentNote) {
                sendFrequency(*currentNote);
                sendToTargets(true);
            } else {
                sendFrequency(*notePlayedLastFrame);
                sendToTargets(false);
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 border   = ImGui::GetColorU32(ImGuiCol_Border);
        for (const Key& key : keys | std::views::reverse) {
            ImU32 fill;
            if (currentNote == key.midiNote) {
                fill = ImGui::GetColorU32(ImGuiCol_ButtonActive);
            } else if (hoveredNote == key.midiNote) {
                fill = ImGui::GetColorU32(key.isBlackKey ? kColorHoveredBlackKey : kColorHoveredWhiteKey);
            } else {
                fill = ImGui::GetColorU32(key.isBlackKey ? kColorIdleBlackKey : kColorIdleWhiteKey);
            }
            drawList->AddRectFilled(key.min, key.max, fill);
            drawList->AddRect(key.min, key.max, border);
        }
    }

    void sendFrequency(int midiNote) {
        if (signal_generator_name.value.empty()) {
            return;
        }
        DigitizerUi::toolbar::sendSettings(this->msgOut, signal_generator_name.value, gr::property_map{{std::pmr::string("frequency"), noteFrequency(midiNote)}});
    }
};

} // namespace opendigitizer

inline auto registerPianoControl = gr::registerBlock<opendigitizer::PianoControl>(gr::globalBlockRegistry());

#endif // OPENDIGITIZER_BLOCKS_PIANO_CONTROL_HPP
