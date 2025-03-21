#ifndef OPENDIGITIZER_UI_LOOK_AND_FEEL_H
#define OPENDIGITIZER_UI_LOOK_AND_FEEL_H

#include <array>
#include <chrono>

struct ImFont;

namespace DigitizerUi {

enum class WindowMode { FULLSCREEN, MAXIMISED, MINIMISED, RESTORED };

struct LookAndFeel {
    enum class Style { Light, Dark };

#ifdef __EMSCRIPTEN__
    const bool isDesktop = false;
#else
    const bool isDesktop = true;
#endif
    bool                      prototypeMode    = false;
    bool                      touchDiagnostics = false;
    std::chrono::milliseconds execTime; /// time it took to handle events and draw one frame
    float                     defaultDPI  = 76.2f;
    float                     verticalDPI = defaultDPI;
    std::array<ImFont*, 2>    fontNormal  = {nullptr, nullptr}; /// default font [0] production [1] prototype use
    std::array<ImFont*, 2>    fontBig     = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontBigger  = {nullptr, nullptr}; /// 0: production 1: prototype use
    std::array<ImFont*, 2>    fontLarge   = {nullptr, nullptr}; /// 0: production 1: prototype use
    ImFont*                   fontIcons;
    ImFont*                   fontIconsBig;
    ImFont*                   fontIconsLarge;
    ImFont*                   fontIconsSolid;
    ImFont*                   fontIconsSolidBig;
    ImFont*                   fontIconsSolidLarge;
    std::chrono::seconds      editPaneCloseDelay{15};

    Style      style      = Style::Light;
    WindowMode windowMode = WindowMode::RESTORED;

    static LookAndFeel&       mutableInstance();
    static const LookAndFeel& instance();

    void loadFonts();

private:
    LookAndFeel() {}
};

} // namespace DigitizerUi

#endif
