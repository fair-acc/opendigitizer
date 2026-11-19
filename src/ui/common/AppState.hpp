#ifndef OPENDIGITIZER_UI_APPSTATE_HPP
#define OPENDIGITIZER_UI_APPSTATE_HPP

#include <chrono>

enum class WindowMode { FULLSCREEN, MAXIMISED, MINIMISED, RESTORED };

namespace gr::od {

/// the application's state that widgets read; LookAndFeel holds only the look
struct AppState {
    WindowMode                windowMode = WindowMode::RESTORED;
    std::chrono::milliseconds execTime{}; // to handle the events and draw one frame
    bool                      prototypeMode = false;
    bool                      compact       = false;
    bool                      portrait      = false;
};

inline AppState gAppState;

} // namespace gr::od

#endif // OPENDIGITIZER_UI_APPSTATE_HPP
