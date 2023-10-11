//
// Created by alex on 7/24/23.
//

#include "remotesignalsources.h"
#include <misc/cpp/imgui_stdlib.h>

void QueryFilterElement::drawFilterLine() {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 3);
    if (ImGui::BeginCombo(_keyIdentifier.c_str(), field_names[_selectedIndex])) {
        for (int i = 0; i < field_names.size(); i++) {
            bool isSelected = _selectedIndex == i;
            if (ImGui::Selectable(field_names[i], isSelected)) {
                if (std::any_of(list.begin(), list.end(), [&i, this](auto &e) { return e._keyIdentifier != _keyIdentifier && e._selectedIndex == i; })) {
                    if (ImGui::BeginPopupModal("Wrong Entry", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                        ImGui::Text("Key already selected. Please select a different one");
                        if (ImGui::Button("Ok")) {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                } else {
                    _selectedIndex = i;
                    list.triggerChange();
                }
            }

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x / 2);
    if (ImGui::InputText(_valueIdentifier.c_str(), &filterText)) {
        list.triggerChange();
    }

    ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() - ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button(_buttonIdentifier.data())) {
        list.pop(*this);
    }
}

QueryFilterElement &QueryFilterElement::operator=(const QueryFilterElement &other) {
    this->list              = other.list;
    this->_valueIdentifier  = other._valueIdentifier;
    this->_keyIdentifier    = other._keyIdentifier;
    this->_selectedIndex    = other._selectedIndex;
    this->_buttonIdentifier = other._buttonIdentifier;
    this->filterText        = other.filterText;
    return *this;
}
/*
 bool FlowGraphItem::QueryFilterElement::operator==(const FlowGraphItem::QueryFilterElement &rhs) const {
     return std::tie(list, _selectedIndex, _keyIdentifier, _valueIdentifier, filterText) == std::tie(rhs.list, rhs._selectedIndex, rhs._keyIdentifier, rhs._valueIdentifier, rhs.filterText);
 }*/

void QueryFilterElementList::triggerChange() {
    std::for_each(onChange.begin(), onChange.end(), [](auto &f) { f(); });
}

#ifdef COMPILEWITHMAIN
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl.h>
#include <SDL.h>

QueryFilterElementList querySignalFilters;
SignalList             signalList{ querySignalFilters };
SDL_Window            *window;

void                   SetupImGui(SDL_Window *window) {
}

void RenderImGui() {
    // Start a new ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame(window);
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(1000, 700));
    ImGui::Begin("Query signals");
    querySignalFilters.drawFilters();

    float windowWidth = ImGui::GetWindowWidth();
    float buttonPosX  = windowWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Add Filter").x;
    ImGui::SetCursorPosX(buttonPosX);
    if (ImGui::Button("Add Filter")) {
        querySignalFilters.emplace_back(QueryFilterElement{ querySignalFilters });
    }
    ImGui::Separator();
    ImGui::SetNextWindowSize(ImGui::GetContentRegionAvail(), ImGuiCond_Once);
    ImGui::BeginChild("Signals");
    signalList.drawElements();
    ImGui::EndChild();

    signalList.drawElements();

    ImGui::End();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int main(int argc, char *argv[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        // Handle initialization error
        return 1;
    }

    SDL_DisplayMode current;
    SDL_GetCurrentDisplayMode(0, &current);
    SDL_WindowFlags window_flags = (SDL_WindowFlags) (SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

    auto            window       = SDL_CreateWindow("opendigitizer UI", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    auto            glContext    = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr, "Failed to initialize WebGL context!\n");
        return 1;
    }

    //   Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui_ImplSDL2_InitForOpenGL(window, nullptr);
    ImGui_ImplOpenGL3_Init("#version 330"); // Provide the appropriate OpenGL version

    bool done = false;
    while (!done) {
        // Process user input events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = true;
            }
        }
        RenderImGui();
        SDL_GL_SwapWindow(window);
    }

    //   Cleanup ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    //   Cleanup SDL
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
#endif