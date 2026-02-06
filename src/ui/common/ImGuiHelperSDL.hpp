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
#include <misc/cpp/imgui_stdlib.h>

#include "LookAndFeel.hpp"

#include "Events.hpp"
#include "TouchHandler.hpp"

#define IMGUI_XKCD_IMPLEMENTATION
#include "ImguiXKCD.hpp"

#include <format>
#include <print>

namespace imgui_helper {
inline static SDL_Window*   g_Window               = nullptr;
inline static SDL_GLContext g_GLContext            = nullptr;
inline static bool          g_ImGuiSDL3Initialised = false;

inline bool requestGLContext(std::string_view windowTitle, ImVec2 windowSize, int major, int minor, std::string& glslVersionOut) {
    std::println("[Main] Requesting OpenGL context {}.{}", major, minor);

#ifdef __EMSCRIPTEN__
    // WebGL2 via GLES 3.0 + GLSL ES 300
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    // glslVersionOut = "#version 300 es"; //TODO: check compatibility of this
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    glslVersionOut = "#version 330 core";
#endif
#endif

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifdef __APPLE__
    constexpr SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_ALLOW_HIGHDPI;
#else
    constexpr SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    if (g_Window != nullptr) {
        SDL_DestroyWindow(g_Window);
        g_Window = nullptr;
    }
    g_Window = SDL_CreateWindow(windowTitle.data(), static_cast<int>(windowSize.x), static_cast<int>(windowSize.y), windowFlags);
    if (!g_Window) {
        std::println(stderr, "[Main] SDL_CreateWindow failed: '{}'", SDL_GetError());
        return false;
    }

    g_GLContext = SDL_GL_CreateContext(g_Window);
    if (!g_GLContext) {
        std::println(stderr, "[Main] SDL_GL_CreateContext({}.{}) failed: '{}'", major, minor, SDL_GetError());
        SDL_DestroyWindow(g_Window);
        g_Window = nullptr;
        return false;
    }
    std::println("[Main] GL_VERSION:   {}", reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    std::println("[Main] GL_RENDERER:  {}", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    std::println("[Main] GLSL_VERSION: {}", reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    std::println("[Main] OpenGL context successfully created using shader '{}'", glslVersionOut);
    return true;
}

inline bool initSDL(std::string& glslVersion, std::string_view windowTitle = "OpenDigitizer UI", ImVec2 windowSize = {1280.f, 720.f}) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::println("[Main] SDL_Init failed: '{}'", SDL_GetError());
        return false;
    }

    if (!requestGLContext(windowTitle, windowSize, 3, 3, glslVersion) && !requestGLContext(windowTitle, windowSize, 2, 0, glslVersion)) {
        std::println("[Main] Could not create any GL context!");
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(1);
    return true;
}

inline bool initImGui(const std::string& glslVersion) {
    if (g_ImGuiSDL3Initialised) {
        return true; // already initialised
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImPlot::CreateContext();

    ImGui_ImplSDL3_InitForOpenGL(g_Window, g_GLContext);

    ImGuiIO& io                  = ImGui::GetIO();
    ImPlot::GetInputMap().Select = ImGuiPopupFlags_MouseButtonLeft;
    ImPlot::GetInputMap().Pan    = ImGuiPopupFlags_MouseButtonMiddle;

    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = nullptr;

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    if (bool ret = ImGui_ImplOpenGL3_Init(glslVersion.c_str()); !ret) {
        SDL_GL_DestroyContext(g_GLContext);
        SDL_DestroyWindow(g_Window);
        SDL_Quit();
        return false;
    }
    ImXkcd::Init();
    g_ImGuiSDL3Initialised = true;
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

[[maybe_unused]] inline bool processEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (isWindowEventForOtherWindow(event, imgui_helper::g_Window)) {
            continue;
        }

        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return false; break;
        case SDL_EVENT_WINDOW_RESTORED: DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::RESTORED; break;
        case SDL_EVENT_WINDOW_MINIMIZED: DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::MINIMISED; break;
        case SDL_EVENT_WINDOW_MAXIMIZED: DigitizerUi::LookAndFeel::mutableInstance().windowMode = WindowMode::MAXIMISED; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED: { // SDL3 recommends this one for logical size changes
            const int width            = event.window.data1;
            const int height           = event.window.data2;
            ImGui::GetIO().DisplaySize = ImVec2(float(width), float(height));
            glViewport(0, 0, width, height);
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(float(width), float(height)));
            break;
        }
        default: break;
        }
        DigitizerUi::TouchHandler<>::processSDLEvent(event);
    }

    DigitizerUi::EventLoop::instance().fireCallbacks();
    DigitizerUi::TouchHandler<>::updateGestures();

    return true;
}

[[maybe_unused]] inline bool newFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({0, 0});
    int  width  = -1;
    int  height = -1;
    bool ret    = SDL_GetWindowSize(g_Window, &width, &height);
    ImGui::SetNextWindowSize(ImVec2{static_cast<float>(width), static_cast<float>(height)});
    return ret;
}

[[maybe_unused]] inline bool renderFrame() {
    ImGui::Render();
    if (DigitizerUi::LookAndFeel::instance().prototypeMode) {
        ImXkcd::Apply(ImGui::GetDrawData());
    }
    if (!SDL_GL_MakeCurrent(g_Window, g_GLContext)) {
        std::println("[Main] SDL_GL_MakeCurrent failed: '{}'", SDL_GetError());
        return false;
    }
    const ImGuiIO& io = ImGui::GetIO();
    glViewport(0, 0, int(io.DisplaySize.x), int(io.DisplaySize.y));
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_Window);
    setWindowMode(g_Window, DigitizerUi::LookAndFeel::instance().windowMode);
    return true;
}

inline bool teardownSDL() {
    if (g_Window == nullptr) {
        return false;
    }
    if (g_GLContext == nullptr) {
        return false;
    }
    ImXkcd::Shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (bool state = SDL_GL_DestroyContext(g_GLContext); !state) {
        std::println("[Main] teardownSDL failed: '{}'", SDL_GetError());
        return false;
    }
    SDL_DestroyWindow(g_Window);
    SDL_Quit();
    g_Window    = nullptr;
    g_GLContext = nullptr;
    return true;
}
} // namespace imgui_helper

#endif // IMGUIHELPERSDL_HPP
