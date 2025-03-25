#ifndef COLOURMANAGER_HPP
#define COLOURMANAGER_HPP

#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>
#include <gnuradio-4.0/Message.hpp>

namespace opendigitizer {
enum class OverflowStrategy { Throw, ExtendAuto, ExtendDarkened };

struct ColourManager {
    enum class ColourMode { Light, Dark };

    struct PaletteIndices {
        std::string         name;
        std::vector<size_t> indices;
    };

    std::vector<std::uint32_t>                      _masterColours;
    std::vector<bool>                               _usage;         // usage[i] = true if _masterColours[i] is taken
    std::unordered_map<std::uint32_t, std::size_t>  _colourToIndex; // maps 0xRRGGBB -> index in _masterColours
    std::unordered_map<std::string, PaletteIndices> _palettes;      // paletteName -> list of global indices
    std::unordered_map<ColourMode, std::string>     _modeMap;
    OverflowStrategy                                _overflow{OverflowStrategy::ExtendAuto};
    ColourMode                                      _currentMode{ColourMode::Light};

    static ColourManager& instance() {
        static ColourManager mgr;
        return mgr;
    }

    ColourManager() { initDefaults(); }

    void reset() {
        _masterColours.clear();
        _usage.clear();
        _colourToIndex.clear();
        _palettes.clear();
        _modeMap.clear();
        _overflow    = OverflowStrategy::ExtendAuto;
        _currentMode = ColourMode::Light;
        initDefaults();
    }

    void       setCurrentMode(ColourMode mode) { _currentMode = mode; }
    ColourMode getCurrentMode() const { return _currentMode; }
    void       setModePalette(ColourMode mode, const std::string& palName) {
        if (!_palettes.count(palName)) {
            throw gr::exception(fmt::format("Unknown palette: '{}'", palName));
        }
        _modeMap[mode] = palName;
    }
    void               setModePalette(const std::string& palName) { setModePalette(_currentMode, palName); }
    const std::string& getActivePalette() const { return _modeMap.at(_currentMode); }
    const std::string& getModePalette(ColourMode mode) const { return _modeMap.at(mode); }
    void               setOverflowStrategy(OverflowStrategy s) { _overflow = s; }
    OverflowStrategy   getOverflowStrategy() const { return _overflow; }
    void               setPalette(const std::string& paletteName, const std::vector<std::uint32_t>& rawColours) {
        PaletteIndices p;
        p.name = paletteName;
        p.indices.reserve(rawColours.size());

        for (std::uint32_t c : rawColours) {
            std::size_t gIdx = findOrAddGlobalColour(c);
            p.indices.push_back(gIdx);
        }
        _palettes[paletteName] = std::move(p);
    }

    std::size_t getNextSlotIndex(const std::string& paletteName) {
        auto it = _palettes.find(paletteName);
        if (it == _palettes.end()) {
            throw gr::exception("palette '" + paletteName + "' not found.");
        }
        auto& pal = it->second.indices;

        // find the first global index that is free
        for (std::size_t localSlot = 0; localSlot < pal.size(); ++localSlot) {
            std::size_t gIdx = pal[localSlot];
            if (!_usage[gIdx]) {
                // mark used
                _usage[gIdx] = true;
                return localSlot;
            }
        }

        // all slots used => overflow
        switch (_overflow) {
        case OverflowStrategy::Throw: throw gr::exception("All colours in palette '" + paletteName + "' are used.");
        case OverflowStrategy::ExtendAuto: return extendPaletteWithRandomColour(paletteName);
        case OverflowStrategy::ExtendDarkened: throw gr::exception("ExtendDarkened not implemented.");
        }
        throw gr::exception("Unknown overflow strategy.");
    }

    std::size_t getNextSlotIndex() { return getNextSlotIndex(_modeMap[_currentMode]); }

    void releaseSlotIndex(const std::string& paletteName, std::size_t localSlot) {
        auto it = _palettes.find(paletteName);
        if (it == _palettes.end()) {
            return;
        }
        auto& pal = it->second.indices;
        if (localSlot < pal.size()) {
            _usage[pal[localSlot]] = false;
        }
    }

    void releaseSlotIndex(std::size_t localSlot) { releaseSlotIndex(_modeMap[_currentMode], localSlot); }

    std::uint32_t getColourAtSlot(const std::string& paletteName, std::size_t localSlot) const {
        auto it = _palettes.find(paletteName);
        if (it == _palettes.end()) {
            throw gr::exception("Palette not found: " + paletteName);
        }
        auto& pal = it->second.indices;
        if (localSlot >= pal.size()) {
            throw gr::exception("Local slot out of range for palette " + paletteName);
        }
        std::size_t gIdx = pal[localSlot];
        return _masterColours.at(gIdx);
    }

    std::size_t setColourInPalette(const std::string& paletteName, std::uint32_t c) {
        // 1) Find the palette
        auto it = _palettes.find(paletteName);
        if (it == _palettes.end()) {
            throw gr::exception("palette '" + paletteName + "' not found.");
        }
        auto& pal = it->second.indices; // vector of global indices

        // 2) Find (or create) the global index for colour c
        std::size_t gIdx = findOrAddGlobalColour(c);

        // 3) Check if this palette already references c
        for (std::size_t localSlot = 0; localSlot < pal.size(); ++localSlot) {
            if (pal[localSlot] == gIdx) {
                // The palette already has c at localSlot
                if (!_usage[gIdx]) {
                    // It's free, so claim it
                    _usage[gIdx] = true;
                    return localSlot;
                }
            }
        }

        // 5) Append a new local slot for c
        pal.push_back(gIdx);
        // Mark usage
        _usage[gIdx] = true;

        // Return the newly created local slot index
        return pal.size() - 1;
    }

    // Overload: forcibly set a colour c in the *current* mode's palette
    std::size_t setColour(std::uint32_t c) {
        std::string currentPal = getModePalette(_currentMode);
        return setColourInPalette(currentPal, c);
    }

    void initDefaults() {
        setPalette("misc", {0x5DA5DA, 0xF15854, 0xFAA43A, 0x60BD68, 0xF17CB0, 0xB2912F, 0xB276B2, 0xDECF3F, 0x4D4D4D});
        setPalette("adobe", {0x00A4E4, 0xFF0000, 0xFBB034, 0xFFDD00, 0xC1D82F, 0x8A7967, 0x6A737B});
        setPalette("dell", {0x0085C3, 0x7AB800, 0xF2AF00, 0xDC5034, 0x6E2585, 0x71C6C1, 0x009BBB, 0x444444});
        setPalette("equidistant", {0x003F5C, 0x2F4B7C, 0x665191, 0xA05195, 0xD45087, 0xF95D6A, 0xFF7C43, 0xFFA600});
        setPalette("tuneviewer", {0x0000C8, 0xC80000, 0x00C800, 0xFFA500, 0xFF00FF, 0x00FFFF, 0xA9A9A9, 0xFFC0CB, 0x000000});
        setPalette("matlab-light", {0x0072BD, 0xD95319, 0xEDB120, 0x7E2F8E, 0x77AC30, 0x4DBEEE, 0xA2142F});
        setPalette("matlab-dark", {0x5995BD, 0xD97347, 0xEDB120, 0xDA51F5, 0x77AC30, 0x4DBEEE, 0xA2898D});

        _modeMap[ColourMode::Light] = "tuneviewer";
        _modeMap[ColourMode::Dark]  = "matlab-dark";
    }

    std::size_t findOrAddGlobalColour(std::uint32_t c) {
        auto it = _colourToIndex.find(c);
        if (it != _colourToIndex.end()) {
            return it->second;
        }
        // brand new colour
        std::size_t idx = _masterColours.size();
        _masterColours.push_back(c);
        _usage.push_back(false);
        _colourToIndex[c] = idx;
        return idx;
    }

    std::size_t extendPaletteWithRandomColour(const std::string& paletteName) {
        static std::mt19937                                 rng{std::random_device{}()};
        static std::uniform_int_distribution<std::uint32_t> dist(0, 0xFFFFFF);

        std::uint32_t c   = dist(rng);
        std::size_t   idx = findOrAddGlobalColour(c);

        auto& p = _palettes[paletteName].indices;
        p.push_back(idx);
        _usage[idx] = true;

        // return the new local slot
        return p.size() - 1;
    }

    static std::string toHex(std::uint32_t c) {
        char buf[10];
        std::snprintf(buf, sizeof(buf), "%06X", c);
        return std::string(buf);
    }
};

struct ManagedColour {
    std::size_t _localSlot{0};

    void updateColour() {
        ColourManager& colorManager = ColourManager::instance();
        colorManager.releaseSlotIndex(_localSlot);
        _localSlot = colorManager.getNextSlotIndex();
    }

    ManagedColour() {
        ColourManager& colorManager = ColourManager::instance();
        _localSlot                  = colorManager.getNextSlotIndex();
    }
    explicit ManagedColour(std::uint32_t initialColour) {
        ColourManager& colorManager = ColourManager::instance();
        _localSlot                  = colorManager.setColour(initialColour);
    }

    ~ManagedColour() { ColourManager::instance().releaseSlotIndex(_localSlot); }

    void setColour(std::uint32_t newColour) {
        ColourManager& colorManager = ColourManager::instance();
        colorManager.releaseSlotIndex(_localSlot);
        _localSlot = colorManager.setColour(newColour);
    }

    std::uint32_t colour() const {
        ColourManager& colorManager = ColourManager::instance();
        auto           currPal      = colorManager.getModePalette(colorManager.getCurrentMode());
        return colorManager.getColourAtSlot(currPal, _localSlot);
    }
};

} // namespace opendigitizer

#endif
