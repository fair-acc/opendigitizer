#ifndef IMGUIHELPERSDL_HPP
#define IMGUIHELPERSDL_HPP

#include <SDL3/SDL.h>
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES // required by ImguiXKCD.hpp for GL 2.0+ function prototypes
#endif
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_video.h>

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>
#include <implot.h>
#include <implot3d.h>
#include <misc/cpp/imgui_stdlib.h>

#include "LookAndFeel.hpp"

#include "Events.hpp"
#include "TouchHandler.hpp"

#define IMGUI_XKCD_IMPLEMENTATION
#include "ImguiXKCD.hpp"

#include <format>
#include <functional>
#include <print>

namespace gr::od::sdl {
inline SDL_Window*   gWindow               = nullptr;
inline SDL_GLContext gGLContext            = nullptr;
inline bool          gImGuiSDL3Initialised = false;

inline bool requestGLContext(std::string_view windowTitle, ImVec2 windowSize, std::string& glslVersionOut, SDL_WindowFlags extraWindowFlags = 0) {
    std::println("[Main] Requesting OpenGL context 3.3");

#ifdef __EMSCRIPTEN__
    // WebGL2 via GLES 3.0 + GLSL ES 300
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    glslVersionOut = "#version 100";
#else
#ifdef __APPLE__
    // macOS requires forward-compatible core context ≥ 3.2
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    glslVersionOut = "#version 150";
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    glslVersionOut = "#version 330 core";
#endif
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifdef __APPLE__
    constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    if (gWindow != nullptr) {
        SDL_DestroyWindow(gWindow);
        gWindow = nullptr;
    }
    gWindow = SDL_CreateWindow(windowTitle.data(), static_cast<int>(windowSize.x), static_cast<int>(windowSize.y), kWindowFlags | extraWindowFlags);
    if (!gWindow) {
        std::println(stderr, "[Main] SDL_CreateWindow failed: '{}'", SDL_GetError());
        return false;
    }

    gGLContext = SDL_GL_CreateContext(gWindow);
    if (!gGLContext) {
        std::println(stderr, "[Main] SDL_GL_CreateContext(3.3) failed: '{}'", SDL_GetError());
        SDL_DestroyWindow(gWindow);
        gWindow = nullptr;
        return false;
    }
    std::println("[Main] GL_VERSION:   {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::println("[Main] GL_RENDERER:  {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::println("[Main] GLSL_VERSION: {}", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    std::println("[Main] OpenGL context successfully created using shader '{}'", glslVersionOut);
    return true;
}

inline bool initSDL(std::string& glslVersion, std::string_view windowTitle = "OpenDigitizer UI", ImVec2 windowSize = {1280.f, 720.f}, SDL_WindowFlags extraWindowFlags = 0) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println("[Main] SDL_Init failed: '{}'", SDL_GetError());
        return false;
    }

    if (!requestGLContext(windowTitle, windowSize, glslVersion, extraWindowFlags)) {
        std::println("[Main] Could not create any GL context!");
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(1);
    return true;
}

inline bool initImGui(const std::string& glslVersion) {
    if (gImGuiSDL3Initialised) {
        return true;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImPlot::CreateContext();
    ImPlot3D::CreateContext();

    ImGui_ImplSDL3_InitForOpenGL(gWindow, gGLContext);

    ImGuiIO& io                  = ImGui::GetIO();
    ImPlot::GetInputMap().Select = ImGuiMouseButton_Left;
    ImPlot::GetInputMap().Pan    = ImGuiMouseButton_Middle;

    // the Emscripten build has no file-system access, so no imgui.ini
    io.IniFilename = nullptr;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    if (bool ret = ImGui_ImplOpenGL3_Init(glslVersion.c_str()); !ret) {
        SDL_GL_DestroyContext(gGLContext);
        SDL_DestroyWindow(gWindow);
        SDL_Quit();
        return false;
    }
    xkcd::init();
    gImGuiSDL3Initialised = true;
    return true;
}

inline void setWindowMode(SDL_Window* window, const WindowMode& state) {
    using enum WindowMode;
    const SDL_WindowFlags flags        = SDL_GetWindowFlags(window);
    const bool            isMaximised  = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    const bool            isMinimised  = (flags & SDL_WINDOW_MINIMIZED) != 0;
    const bool            isFullscreen = (flags & SDL_WINDOW_FULLSCREEN) != 0;
    if ((isMaximised && state == MAXIMISED) || (isMinimised && state == MINIMISED) || (isFullscreen && state == FULLSCREEN)) {
        return;
    }
    switch (state) {
    case FULLSCREEN: SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN); return;
    case MAXIMISED:
        SDL_SetWindowFullscreen(window, false);
        SDL_MaximizeWindow(window);
        return;
    case MINIMISED:
        SDL_SetWindowFullscreen(window, false);
        SDL_MinimizeWindow(window);
        return;
    case RESTORED: SDL_SetWindowFullscreen(window, false); SDL_RestoreWindow(window);
    }
}

inline bool isWindowEventForOtherWindow(const SDL_Event& e, SDL_Window* w) {
    const auto my = SDL_GetWindowID(w);
    switch (e.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED: return e.window.windowID && e.window.windowID != my;
    default: return false;
    }
}

struct EventPumpResult {
    bool running  = true;
    bool hadInput = false;
};

[[nodiscard]] inline EventPumpResult pollEvents() {
    EventPumpResult result;
    SDL_Event       event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (isWindowEventForOtherWindow(event, gWindow)) {
            continue;
        }

        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: result.running = false; return result;
        case SDL_EVENT_WINDOW_RESTORED:
            gAppState.windowMode = WindowMode::RESTORED;
            result.hadInput      = true;
            break;
        case SDL_EVENT_WINDOW_MINIMIZED: gAppState.windowMode = WindowMode::MINIMISED; break;
        case SDL_EVENT_WINDOW_MAXIMIZED:
            gAppState.windowMode = WindowMode::MAXIMISED;
            result.hadInput      = true;
            break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: { // SDL3 recommends this one for logical size changes
            const int width            = event.window.data1;
            const int height           = event.window.data2;
            ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
            glViewport(0, 0, width, height);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
            result.hadInput = true;
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_MOTION: result.hadInput = true; break;
        default: break;
        }
        TouchHandler<>::onSdlEvent(event);
    }

    EventLoop::instance().fireCallbacks();
    TouchHandler<>::updateGestures();
    return result;
}

[[maybe_unused]] inline bool newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    LookAndFeel::mutableInstance().updateFormFactor(ImGui::GetIO().DisplaySize);
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    int  width  = -1;
    int  height = -1;
    bool ret    = SDL_GetWindowSize(gWindow, &width, &height);
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(width), static_cast<float>(height)});
    return ret;
}

[[maybe_unused]] inline bool renderFrame(const std::function<void()>& beforeSwap = {}) {
    ImGui::Render();
    if (gAppState.prototypeMode) {
        xkcd::apply(ImGui::GetDrawData());
    }
    if (!SDL_GL_MakeCurrent(gWindow, gGLContext)) {
        std::println("[Main] SDL_GL_MakeCurrent failed: '{}'", SDL_GetError());
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, int(io.DisplaySize.x), int(io.DisplaySize.y));
    {
        const ImVec4& clearColor = LookAndFeel::instance().palette().clearColor;
        glClearColor(clearColor.x, clearColor.y, clearColor.z, clearColor.w);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (beforeSwap) {
        beforeSwap();
    }
    SDL_GL_SwapWindow(gWindow);
    setWindowMode(gWindow, gAppState.windowMode);
    return true;
}

inline bool teardownSDL() {
    if (gWindow == nullptr) {
        return false;
    }
    if (gGLContext == nullptr) {
        return false;
    }
    xkcd::shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImPlot3D::DestroyContext();
    ImGui::DestroyContext();
    if (bool state = SDL_GL_DestroyContext(gGLContext); !state) {
        std::println("[Main] teardownSDL failed: '{}'", SDL_GetError());
        return false;
    }
    SDL_DestroyWindow(gWindow);
    SDL_Quit();
    gWindow    = nullptr;
    gGLContext = nullptr;
    return true;
}
} // namespace gr::od::sdl

#endif // IMGUIHELPERSDL_HPP
