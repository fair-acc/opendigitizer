#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl.h>
#include <implot.h>
#include <SDL.h>
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#include <complex>
#include <cstdio>

#include "dashboard.h"
#include "datasink.h"
#include "datasource.h"
#include "flowgraph.h"
#include "flowgraphitem.h"

#include "fair_header.h"

// Emscripten requires to have full control over the main loop. We're going to
// store our SDL book-keeping variables globally. Having a single function that
// acts as a loop prevents us to store state in the stack of said function. So
// we need some location for this.
SDL_Window   *g_Window    = NULL;
SDL_GLContext g_GLContext = NULL;
bool          running     = true;

static void   main_loop(void *);

ImFont       *addDefaultFont(float pixel_size) {
    ImGuiIO     &io = ImGui::GetIO();
    ImFontConfig config;
    config.SizePixels = pixel_size;
    // high oversample to have better looking text when zooming in on the flowgraph
    config.OversampleH = config.OversampleV = 4;
    config.PixelSnapH                       = true;
    ImFont *font                            = io.Fonts->AddFontDefault(&config);
    return font;
}

class SumBlock : public DigitizerUi::Block {
public:
    explicit SumBlock(std::string_view name, DigitizerUi::BlockType *type)
        : DigitizerUi::Block(name, type->name, type) {
    }

    void processData() override {
        auto &in = inputs();
        if (in[0].connections.empty() || in[1].connections.empty()) {
            return;
        }

        auto *p0   = static_cast<DigitizerUi::Block::OutputPort *>(in[0].connections[0]->ports[0]);
        auto *p1   = static_cast<DigitizerUi::Block::OutputPort *>(in[1].connections[0]->ports[0]);
        auto  val0 = p0->dataSet.asFloat32();
        auto  val1 = p1->dataSet.asFloat32();

        if (val0.size() != val1.size()) {
            return;
        }

        m_data.resize(val0.size());
        memcpy(m_data.data(), val0.data(), m_data.size() * 4);
        for (int i = 0; i < m_data.size(); ++i) {
            m_data[i] += val1[i];
        }
        outputs()[0].dataSet = m_data;
    }

    std::vector<float> m_data;
};

template<typename T>
class FFT {
    std::size_t                  _N;
    std::vector<std::complex<T>> _w;

public:
    FFT() = delete;
    explicit FFT(std::size_t N)
        : _N(N), _w(N) {
        assert(N > 1 && "N should be > 0");
        for (std::size_t s = 2; s <= N; s *= 2) {
            const std::size_t m = s / 2;
            _w[m]               = exp(std::complex<T>(0, -2 * M_PI / s));
        }
    }

    void compute(std::vector<std::complex<T>> &X) const noexcept {
        std::size_t rev = 0;
        for (std::size_t i = 0; i < _N; i++) {
            if (rev > i && rev < _N) {
                std::swap(X[i], X[rev]);
            }
            std::size_t mask = _N / 2;
            while (rev & mask) {
                rev -= mask;
                mask /= 2;
            }
            rev += mask;
        }

        for (std::size_t s = 2; s <= _N; s *= 2) {
            const std::size_t m = s / 2;
            // std::complex<T> w = exp(std::complex<T>(0, -2 * M_PI / s));
            for (std::size_t k = 0; k < _N; k += s) {
                std::complex<T> wk = 1;
                for (std::size_t j = 0; j < m; j++) {
                    const std::complex<T> t = wk * X[k + j + m];
                    const std::complex<T> u = X[k + j];
                    X[k + j]                = u + t;
                    X[k + j + m]            = u - t;
                    // wk *= w;
                    wk *= _w[m];
                }
            }
        }
    }

    template<typename C>
    std::vector<T> compute_magnitude_spectrum(C signal) {
        static_assert(std::is_same_v<T, typename C::value_type>, "input type T mismatch");
        std::vector<T>               magnitude_spectrum(_N / 2 + 1);
        std::vector<std::complex<T>> fft_signal(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++) {
            fft_signal[i] = { signal[i], 0.0 };
        }

        compute(fft_signal);

        for (std::size_t n = 0; n < _N / 2 + 1; n++) {
            magnitude_spectrum[n] = std::abs(fft_signal[n]) * 2.0 / _N;
        }

        return magnitude_spectrum;
    }
};

class FFTBlock : public DigitizerUi::Block {
public:
    explicit FFTBlock(std::string_view name, DigitizerUi::BlockType *type)
        : DigitizerUi::Block(name, type->name, type) {
    }

    void processData() override {
        auto &in = inputs()[0];
        if (in.connections.empty()) {
            return;
        }

        auto *p   = static_cast<DigitizerUi::Block::OutputPort *>(in.connections[0]->ports[0]);
        auto  val = p->dataSet.asFloat32();

        m_data.resize(val.size());
        FFT<float> fft(val.size());
        m_data               = fft.compute_magnitude_spectrum(val);
        outputs()[0].dataSet = m_data;
    }

    std::vector<float> m_data;
};

struct App {
    DigitizerUi::FlowGraph     flowGraph;
    DigitizerUi::FlowGraphItem fgItem;
    DigitizerUi::Dashboard     dashboard;

    ImFont                    *font12 = nullptr;
    ImFont                    *font14 = nullptr;
    ImFont                    *font16 = nullptr;
};

int main(int, char **) {
    // Setup SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // For the browser using Emscripten, we are going to use WebGL1 with GL ES2.
    // It is very likely the generated file won't work in many browsers.
    // Firefox is the only sure bet, but I have successfully run this code on
    // Chrome for Android for example.
    const char *glsl_version = "#version 100";
    // const char* glsl_version = "#version 300 es";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags window_flags = (SDL_WindowFlags) (SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    g_Window                     = SDL_CreateWindow("opendigitizer UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    g_GLContext                  = SDL_GL_CreateContext(g_Window);
    if (!g_GLContext) {
        fprintf(stderr, "Failed to initialize WebGL context!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;

    // For an Emscripten build we are disabling file-system access, so let's not
    // attempt to do a fopen() of the imgui.ini file. You may manually call
    // LoadIniSettingsFromMemory() to load settings from your own storage.
    io.IniFilename = NULL;

    // Setup Dear ImGui style
    // ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();
    ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(g_Window, g_GLContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Load Fonts
    // io.Fonts->AddFontDefault();
#ifndef IMGUI_DISABLE_FILE_FUNCTIONS
    // io.Fonts->AddFontFromFileTTF("fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/Cousine-Regular.ttf", 15.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("fonts/ProggyTiny.ttf", 10.0f);
    // ImFont* font = io.Fonts->AddFontFromFileTTF("fonts/ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    // IM_ASSERT(font != NULL);
#endif

    App app = {
        .flowGraph = {},
        .fgItem    = { &app.flowGraph },
        .dashboard = DigitizerUi::Dashboard(&app.flowGraph)
    };

    app.fgItem.newSinkCallback = [&, n = 1]() mutable {
        auto name = fmt::format("sink {}", n++);
        app.flowGraph.addSinkBlock(std::make_unique<DigitizerUi::DataSink>(name));
    };

#ifndef EMSCRIPTEN
    app.flowGraph.loadBlockDefinitions(BLOCKS_DIR);
#endif
    app.flowGraph.parse(opencmw::URI<opencmw::STRICT>("http://localhost:8080/flowgraph"));

    app.flowGraph.addSourceBlock(std::make_unique<DigitizerUi::DataSource>("source1", 0.1));
    app.flowGraph.addSourceBlock(std::make_unique<DigitizerUi::DataSource>("source2", 0.02));

    app.flowGraph.addBlockType([]() {
        auto t         = std::make_unique<DigitizerUi::BlockType>("sum sigs");
        t->createBlock = [t = t.get()](std::string_view name) {
            return std::make_unique<SumBlock>(name, t);
        };
        t->inputs.resize(2);
        t->inputs[0].name = "in1";
        t->inputs[0].type = "float";

        t->inputs[1].name = "in2";
        t->inputs[1].type = "float";

        t->outputs.resize(1);
        t->outputs[0].name = "out";
        t->outputs[0].type = "float";
        return t;
    }());

    app.flowGraph.addBlockType([]() {
        auto t         = std::make_unique<DigitizerUi::BlockType>("FFT");
        t->createBlock = [t = t.get()](std::string_view name) {
            return std::make_unique<FFTBlock>(name, t);
        };
        t->inputs.resize(1);
        t->inputs[0].name = "in1";
        t->inputs[0].type = "float";

        t->outputs.resize(1);
        t->outputs[0].name = "out";
        t->outputs[0].type = "float";
        return t;
    }());

    app.font12 = addDefaultFont(12);
    app.font14 = addDefaultFont(14);
    app.font16 = addDefaultFont(16);

    app_header::load_header_assets();

    // This function call won't return, and will engage in an infinite loop, processing events from the browser, and dispatching them.
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop_arg(main_loop, &app, 0, true);
#else
    SDL_GL_SetSwapInterval(1); // Enable vsync

    while (running) {
        main_loop(&app);
    }
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(g_GLContext);
    SDL_DestroyWindow(g_Window);
    SDL_Quit();
#endif
    // emscripten_set_main_loop_timing(EM_TIMING_SETIMMEDIATE, 10);
}

static void main_loop(void *arg) {
    App     *app = static_cast<App *>(arg);

    ImGuiIO &io  = ImGui::GetIO();
    IM_UNUSED(arg); // We can pass this argument as the second parameter of emscripten_set_main_loop_arg(), but we don't use that.

    // Poll and handle events (inputs, window resize, etc.)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            running = false;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(g_Window))
            running = false;
        // Capture events here, based on io.WantCaptureMouse and io.WantCaptureKeyboard
    }

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({ 0, 0 });
    int width, height;
    SDL_GetWindowSize(g_Window, &width, &height);
    ImGui::SetNextWindowSize({ float(width), float(height) });
    ImGui::Begin("Main Window", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    app_header::draw_header_bar("OpenDigitizer", app->font16);

    ImGui::BeginTabBar("maintabbar");
    if (ImGui::BeginTabItem("Dashboard")) {
        app->dashboard.draw();

        ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Flowgraph")) {
        auto contentRegion = ImGui::GetContentRegionAvail();

        app->fgItem.draw(contentRegion);

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    ImGui::End();

    // Rendering
    ImGui::Render();
    SDL_GL_MakeCurrent(g_Window, g_GLContext);
    glViewport(0, 0, (int) io.DisplaySize.x, (int) io.DisplaySize.y);
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_Window);
}
