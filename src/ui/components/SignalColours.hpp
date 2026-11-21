#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_COLOURS_HPP
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_COLOURS_HPP

#include "../common/LookAndFeel.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace gr::od {

/// 0xRRGGBB colours assigned to signals by slot; a colour in use is not handed out again until released.
struct SignalColours {
    using Style = LookAndFeel::Style;

    Style                                     style = Style::Light;
    std::array<std::vector<std::uint32_t>, 2> palettes{{
        {0x0000C8, 0xC80000, 0x00C800, 0xFFA500, 0xFF00FF, 0x00FFFF, 0xA9A9A9, 0xFFC0CB, 0x000000},
        {0x5995BD, 0xD97347, 0xEDB120, 0xDA51F5, 0x77AC30, 0x4DBEEE, 0xA2898D},
    }};
    std::vector<std::uint32_t>                _used;

    static SignalColours& instance() {
        static SignalColours colours;
        return colours;
    }

    [[nodiscard]] std::vector<std::uint32_t>&       palette() { return palettes[static_cast<std::size_t>(std::to_underlying(style))]; }
    [[nodiscard]] const std::vector<std::uint32_t>& palette() const { return palettes[static_cast<std::size_t>(std::to_underlying(style))]; }

    [[nodiscard]] bool isUsed(std::uint32_t colour) const { return std::ranges::contains(_used, colour); }

    void markUsed(std::uint32_t colour) {
        if (!isUsed(colour)) {
            _used.push_back(colour);
        }
    }

    [[nodiscard]] std::size_t acquire() {
        auto& slots = palette();
        if (const auto free = std::ranges::find_if(slots, [this](std::uint32_t colour) { return !isUsed(colour); }); free != slots.end()) {
            markUsed(*free);
            return static_cast<std::size_t>(free - slots.begin());
        }
        return append(overflowColour(slots.size()));
    }

    [[nodiscard]] std::size_t acquire(std::uint32_t colour) {
        const auto& slots = palette();
        if (const auto slot = std::ranges::find(slots, colour); slot != slots.end() && !isUsed(colour)) {
            markUsed(colour);
            return static_cast<std::size_t>(slot - slots.begin());
        }
        return append(colour);
    }

    void release(std::size_t slot) {
        if (const auto& slots = palette(); slot < slots.size()) {
            std::erase(_used, slots[slot]);
        }
    }

    [[nodiscard]] std::uint32_t colour(std::size_t slot) const {
        const auto& slots = palette();
        return slots.empty() ? 0U : slots[slot % slots.size()];
    }

    std::size_t append(std::uint32_t colour) {
        auto& slots = palette();
        slots.push_back(colour);
        markUsed(colour);
        return slots.size() - 1UZ;
    }

    [[nodiscard]] std::uint32_t overflowColour(std::size_t slot) const {
        std::uint32_t colour = 0U;
        for (auto seed = static_cast<std::uint32_t>(slot) + 1U; colour == 0U || std::ranges::contains(palette(), colour) || isUsed(colour); ++seed) {
            colour = (seed * 0x9E3779B9U) >> 8U;
        }
        return colour;
    }
};

/// a slot of SignalColours::instance(), released on destruction
struct ManagedColour {
    std::size_t _localSlot = SignalColours::instance().acquire();

    ManagedColour() = default;
    explicit ManagedColour(std::uint32_t initialColour) : _localSlot(SignalColours::instance().acquire(initialColour)) {}
    ManagedColour(const ManagedColour&)            = delete;
    ManagedColour& operator=(const ManagedColour&) = delete;
    ~ManagedColour() { SignalColours::instance().release(_localSlot); }

    void updateColour() {
        SignalColours::instance().release(_localSlot);
        _localSlot = SignalColours::instance().acquire();
    }

    void setColour(std::uint32_t newColour) {
        SignalColours::instance().release(_localSlot);
        _localSlot = SignalColours::instance().acquire(newColour);
    }

    [[nodiscard]] std::uint32_t colour() const { return SignalColours::instance().colour(_localSlot); }
};

} // namespace gr::od

#endif
