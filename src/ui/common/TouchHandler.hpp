#ifndef OPENDIGITIZER_TOUCHHANDLER_HPP
#define OPENDIGITIZER_TOUCHHANDLER_HPP

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <stack>
#include <unordered_map>

#include <fmt/core.h>
#include <fmt/format.h>

#include "ImguiWrap.hpp"
#include <implot_internal.h>

#include <SDL.h>

#include "../common/LookAndFeel.hpp"
#include "conversion.hpp"
#include "scope_exit.hpp"

namespace DigitizerUi {

template<typename ClockSourceType = std::chrono::system_clock, bool zoomViaMouseWheel = false>
struct TouchHandler {
    using TimePoint                                   = std::chrono::time_point<ClockSourceType>;
    constexpr static inline std::size_t N_MAX_FINGERS = 10;

    // fingerID to unique small int mapper
    static inline std::unordered_map<SDL_FingerID, std::size_t> fingerIdToIndex;
    static inline std::stack<std::size_t>                       releasedIndices;
    static inline std::size_t                                   nextAvailableIndex = 0;

    //
    static std::size_t getOrAssignIndex(SDL_FingerID fingerId) {
        if (auto [it, inserted] = fingerIdToIndex.emplace(fingerId, nextAvailableIndex); !inserted) {
            return it->second;
        }

        if (!releasedIndices.empty()) { // reuse released indices
            const std::size_t index = releasedIndices.top();
            releasedIndices.pop();
            fingerIdToIndex[fingerId] = index;
            return index;
        }

        // no released indices -> use nextAvailableIndex.
        fingerIdToIndex[fingerId] = nextAvailableIndex;
        assert(((nextAvailableIndex + 1) < N_MAX_FINGERS) && "more fingers than N_MAX_FINGERS detected");
        return nextAvailableIndex++;
    }

    static void releaseIndex(SDL_FingerID fingerId) {
        if (fingerIdToIndex.find(fingerId) != fingerIdToIndex.end()) {
            releasedIndices.push(fingerIdToIndex[fingerId]);
            fingerIdToIndex.erase(fingerId);
        }
    }

    static void releaseFingerIndex(std::size_t index) {
        auto it = std::find_if(fingerIdToIndex.begin(), fingerIdToIndex.end(), [&](const auto& pair) { return pair.second == index; });

        if (it != fingerIdToIndex.end()) {
            fingerIdToIndex.erase(it);
            releasedIndices.push(index);
        }
    }

    // Static state variables
    static inline std::array<bool, N_MAX_FINGERS>          fingerPressed{};       // actual finger state
    static inline std::array<bool, N_MAX_FINGERS>          fingerLifted{};        // actual finger lifted
    static inline std::array<ImVec2, N_MAX_FINGERS>        fingerPos{};           // actual finger position
    static inline std::array<ImVec2, N_MAX_FINGERS>        fingerLastPos{};       // previous frame's finger position
    static inline std::array<ImVec2, N_MAX_FINGERS>        fingerPosDiff{};       // actual finger position difference
    static inline std::array<ImVec2, N_MAX_FINGERS>        fingerPosDown{};       // finger position when finger touch down
    static inline std::array<ImVec2, N_MAX_FINGERS>        fingerPosUp{};         // finger position when finger is lifted
    static inline std::array<TimePoint, N_MAX_FINGERS>     fingerTimeStamp{};     // time stamp when the last change occurred
    static inline std::array<TimePoint, N_MAX_FINGERS>     fingerDownTimeStamp{}; // time stamp when the finger was pressed
    static inline std::array<TimePoint, N_MAX_FINGERS>     fingerUpTimeStamp{};   // time stamp when the finger was lifted
    static inline std::array<std::uint32_t, N_MAX_FINGERS> fingerWindowID{};      // windowID where finger position when finger is lifted
    // gesture state
    static inline TimePoint gestureTimeStamp{};     // time stamp when the last change occurred
    static inline TimePoint gestureDownTimeStamp{}; // time stamp when the finger was pressed
    static inline TimePoint gestureUpTimeStamp{};   // time stamp when the finger was lifted
    static inline ImVec2    gestureCentre{-1.f, -1.f};
    static inline ImVec2    gestureCentreDiff{0.f, 0.f};
    static inline ImVec2    gestureLastCentre{-1.f, -1.f};
    static inline ImVec2    gestureCentreDown{-1.f, -1.f};
    static inline ImVec2    gestureCentreUp{-1.f, -1.f};
    static inline bool      gestureActive      = false;
    static inline bool      gestureDragActive  = false;
    static inline bool      gestureZoomActive  = false;
    static inline float     gestureRotationRad = 0.0f;
    static inline float     gestureRotationDeg = 0.0f;

    static inline std::size_t nFingers            = 0;
    static inline bool        fingerDown          = false;
    static inline bool        fingerUp            = false;
    static inline bool        touchActive         = false;
    static inline bool        singleFingerClicked = false;

    // some helper functions
    static float getFingerMovementDistance(std::size_t fingerIndex) {
        if (fingerIndex >= N_MAX_FINGERS) {
            return -1.0f;
        }
        return std::hypot(fingerPos[fingerIndex].x - fingerPosDown[fingerIndex].x, fingerPos[fingerIndex].y - fingerPosDown[fingerIndex].y);
    }

    static auto getFingerPressedDuration(std::size_t fingerIndex) {
        if (fingerIndex >= N_MAX_FINGERS) {
            return std::chrono::microseconds(-1);
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(fingerTimeStamp[fingerIndex] - fingerDownTimeStamp[fingerIndex]);
    }

    static void drawFingerPositions() {
        const float circleRadius = 10.0f;
        const auto  now          = ClockSourceType::now();
        ImDrawList* draw_list    = ImGui::GetForegroundDrawList();

        for (std::size_t fingerIndex = 0; fingerIndex < N_MAX_FINGERS; ++fingerIndex) {
            if (fingerPressed[fingerIndex] && fingerPosDown[fingerIndex].x != -1.f && fingerPosDown[fingerIndex].y != -1.f) {
                draw_list->AddCircle(fingerPosDown[fingerIndex], circleRadius, IM_COL32(0, 255, 0, 255), 12, 3.0f); // green for pressed
            }

            if (fingerPressed[fingerIndex] && fingerLastPos[fingerIndex].x != -1.f && fingerLastPos[fingerIndex].y != -1.f) {
                draw_list->AddCircle(fingerLastPos[fingerIndex], 3.0f * circleRadius, IM_COL32(165, 165, 0, 255), 12, 3.0f); // yellow for last
            }

            if (fingerPressed[fingerIndex] && fingerPos[fingerIndex].x != -1.f && fingerPos[fingerIndex].y != -1.f) {
                draw_list->AddCircle(fingerPos[fingerIndex], 3.0f * circleRadius, IM_COL32(255, 165, 0, 255), 12, 3.0f); // orange for moving
            }

            const auto timeSinceLifted = std::chrono::duration_cast<std::chrono::milliseconds>(now - fingerTimeStamp[fingerIndex]);
            if (fingerPosUp[fingerIndex].x != -1.f && fingerPosUp[fingerIndex].y != -1.f && (timeSinceLifted < std::chrono::seconds(3))) {
                draw_list->AddNgon(fingerPosUp[fingerIndex], circleRadius, IM_COL32(255, 0, 0, 255), 3, 3.0f); // red for lifted
            }
        }

        // gesture diagnostics
        const auto timeSinceLifted = std::chrono::duration_cast<std::chrono::milliseconds>(now - gestureTimeStamp);
        if (gestureActive && gestureCentreDown.x != -1.f && gestureCentreDown.y != -1.f) {
            draw_list->AddNgon(gestureCentreDown, 1.0f * circleRadius, IM_COL32(0, 255, 0, 255), 5, 3.0f); // green for initial gesture centre
        }
        if (gestureActive && gestureLastCentre.x != -1.f && gestureLastCentre.y != -1.f) {
            draw_list->AddNgon(gestureLastCentre, 3.0f * circleRadius, IM_COL32(165, 165, 0, 255), 5, 3.0f); // yellow pentagon for last gesture centre
        }
        if (gestureActive && gestureCentre.x != -1.f && gestureCentre.y != -1.f) {
            draw_list->AddNgon(gestureCentre, 3.0f * circleRadius, IM_COL32(255, 165, 0, 255), 5, 3.0f); // orange pentagon for moving gesture centre
        }
        if (gestureCentreUp.x != -1.f && gestureCentreUp.y != -1.f && (timeSinceLifted < std::chrono::seconds(3))) {
            draw_list->AddNgon(gestureCentreUp, circleRadius, IM_COL32(255, 0, 0, 255), 4, 3.0f); // red square for gesture centre lifted
        }
    }

    static void processSDLEvent(const SDL_Event& event) {
        const auto& displaySize = ImGui::GetIO().DisplaySize;
        const auto  now         = ClockSourceType::now();

        switch (event.type) {
        case SDL_FINGERDOWN: {
            const std::size_t fingerIndex    = getOrAssignIndex(event.tfinger.fingerId);
            touchActive                      = true;
            fingerDown                       = true;
            fingerTimeStamp[fingerIndex]     = now;
            fingerDownTimeStamp[fingerIndex] = fingerTimeStamp[fingerIndex];
            fingerUpTimeStamp[fingerIndex]   = fingerTimeStamp[fingerIndex];
            fingerPressed[fingerIndex]       = true;
            fingerLifted[fingerIndex]        = false;
            fingerPos[fingerIndex]           = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerLastPos[fingerIndex]       = fingerPos[fingerIndex];
            fingerPosDiff[fingerIndex]       = {0.f, 0.f};
            fingerPosDown[fingerIndex]       = fingerPos[fingerIndex];
            fingerPosUp[fingerIndex]         = {-1.f, -1.f};
            fingerWindowID[fingerIndex]      = event.tfinger.windowID;
            ++nFingers;

            if (nFingers >= 2 && !gestureActive) {
                if (singleFingerClicked) {
                    ImGui::GetIO().AddMouseButtonEvent(0, false); // release initial finger - not a simple click/drag
                    singleFingerClicked = false;
                }
                gestureActive        = true;
                gestureDownTimeStamp = now;
                const ImVec2 centre  = {0.5f * fingerPos[0].x + 0.5f * fingerPos[1].x, 0.5f * fingerPos[0].y + 0.5f * fingerPos[1].y};
                gestureLastCentre    = (gestureCentre.x != -1.f && gestureCentre.y != -1.f) ? gestureCentre : centre;
                gestureCentre        = centre;
                gestureCentreDown    = gestureCentre;
            }
            if (!gestureActive && !gestureDragActive && !gestureZoomActive && nFingers == 1) {
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
                ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(fingerIndex), true);
                singleFingerClicked = true;
            }
            if (LookAndFeel::instance().touchDiagnostics) {
                fmt::print("touch: finger down: {} fingerID: {} p:{} @({},{})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y);
            }
        } break;
        case SDL_FINGERUP: {
            const std::size_t fingerIndex  = getOrAssignIndex(event.tfinger.fingerId);
            touchActive                    = true;
            fingerUp                       = true;
            fingerTimeStamp[fingerIndex]   = now;
            fingerUpTimeStamp[fingerIndex] = fingerTimeStamp[fingerIndex];
            fingerPressed[fingerIndex]     = false;
            fingerLifted[fingerIndex]      = true;
            fingerLastPos[fingerIndex]     = fingerPos[fingerIndex];
            fingerPos[fingerIndex]         = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerPosDiff[fingerIndex]     = fingerPos[fingerIndex] - fingerLastPos[fingerIndex];
            fingerPosUp[fingerIndex]       = fingerPos[fingerIndex];
            fingerWindowID[fingerIndex]    = event.tfinger.windowID;
            assert(nFingers > 0);
            --nFingers;
            releaseIndex(event.tfinger.fingerId);
            if (nFingers == 0 && !gestureActive && !gestureDragActive && !gestureZoomActive) {
                if (getFingerPressedDuration(fingerIndex) < std::chrono::milliseconds(500)) { // short click -> process as left click
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonLeft, true);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonLeft, false);
                    ImGui::GetIO().MouseDown[ImGuiPopupFlags_MouseButtonRight]    = false;
                    ImGui::GetIO().MouseClicked[ImGuiPopupFlags_MouseButtonRight] = false;
                } else { // long click -> process as right click
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonLeft, false);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonRight, true);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonRight, false);
                    ImGui::GetIO().MouseDown[ImGuiPopupFlags_MouseButtonLeft]    = false;
                    ImGui::GetIO().MouseClicked[ImGuiPopupFlags_MouseButtonLeft] = false;
                    // reset to avoid recurring 'right click emulation'
                    fingerDownTimeStamp[fingerIndex] = now;
                }
            }

            if (!gestureDragActive && !gestureZoomActive && nFingers == 0) { // finish single-finger drag
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
                ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(fingerIndex), false);
            }

            if (LookAndFeel::instance().touchDiagnostics) {
                fmt::print("touch: finger up: {} fingerID: {} p:{} @({},{})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y);
            }
        } break;
        case SDL_FINGERMOTION: {
            std::size_t fingerIndex      = getOrAssignIndex(event.tfinger.fingerId);
            touchActive                  = true;
            fingerTimeStamp[fingerIndex] = now;
            fingerPressed[fingerIndex]   = true;
            fingerLifted[fingerIndex]    = false;
            fingerLastPos[fingerIndex]   = fingerPos[fingerIndex];
            fingerPos[fingerIndex]       = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerPosDiff[fingerIndex]   = fingerPos[fingerIndex] - fingerLastPos[fingerIndex];
            fingerWindowID[fingerIndex]  = event.tfinger.windowID;
            if (nFingers == 1) {
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
            }
            if (LookAndFeel::instance().touchDiagnostics) {
                fmt::print("touch: finger motion: {} fingerID: {} p:{} @({},{}) motion (dx,dy): ({}, {})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y, event.tfinger.dx, event.tfinger.dy);
            }
        } break;
            // ... [add any other cases you'd like to handle]
        }
    }

    static void updateGestures() {
        const auto now = ClockSourceType::now();

        // auto-lift finger if it hasn't been active (moving/lifted) for more than 10 seconds -> usually happens when an IO event has been lost
        const auto timeSinceAnyLastActive = std::chrono::duration_cast<std::chrono::milliseconds>(now - *std::max_element(fingerTimeStamp.begin(), fingerTimeStamp.end()));
        for (std::size_t fingerIndex = 0UL; fingerIndex < N_MAX_FINGERS; fingerIndex++) {
            const auto timeSinceLastActive = std::chrono::duration_cast<std::chrono::milliseconds>(now - fingerTimeStamp[fingerIndex]);
            if (fingerPressed[fingerIndex] && (timeSinceLastActive > std::chrono::seconds(5)) && (timeSinceAnyLastActive > std::chrono::seconds(5))) { // more than 5 seconds of inaction, reset finger state
                ImGui::GetIO().AddMouseButtonEvent(static_cast<int>(fingerIndex), false);
                fingerPressed[fingerIndex] = false;
                assert(nFingers > 0);
                --nFingers;
                touchActive         = true;
                fingerUp            = true;
                singleFingerClicked = false;
                releaseFingerIndex(fingerIndex);
                fmt::print("WARNING: probably lost SDL_FINGERUP event -> reset inactive fingerID {} out of {} - timeSinceLifted {}\n", fingerIndex, nFingers, timeSinceLastActive);
            }
        }

        // compute gesture centre, pinch, and rotation
        if (nFingers >= 2 && fingerPressed[0] && fingerPressed[1]) {
            gestureTimeStamp = now;

            const ImVec2 centre = {0.5f * fingerPos[0].x + 0.5f * fingerPos[1].x, 0.5f * fingerPos[0].y + 0.5f * fingerPos[1].y};
            gestureLastCentre   = (gestureCentre.x != -1.f && gestureCentre.y != -1.f) ? gestureCentre : centre;
            gestureCentre       = centre;

            gestureCentreDiff = gestureCentre - gestureCentreDown;

            if constexpr (zoomViaMouseWheel) {
                if (!gestureDragActive && std::hypot(gestureCentreDiff.x, gestureCentreDiff.y) > ImGui::GetIO().MouseDragThreshold) {
                    ImGui::GetIO().AddMouseButtonEvent(ImPlot::GetInputMap().Pan, true);
                    // ImGui::GetIO().AddMousePosEvent(gestureCentre.x, gestureCentre.y);
                    gestureDragActive = true;
                    if (LookAndFeel::instance().touchDiagnostics) {
                        fmt::print("gesture: start two finger drag - centre ({},{}) move {} vs. threshold {}\n", gestureCentreUp.x, gestureCentreUp.y, std::hypot(gestureCentreDiff.x, gestureCentreDiff.y), ImGui::GetIO().MouseDragThreshold);
                    }
                }
            }

            if (gestureDragActive || (!gestureDragActive && std::hypot(gestureCentreDiff.x, gestureCentreDiff.y) > ImGui::GetIO().MouseDragThreshold)) {
                ImGui::GetIO().MousePos   = gestureCentre;
                ImGui::GetIO().MouseDelta = gestureCentre - gestureLastCentre;
            }
        } else if (nFingers == 0) {
            if (gestureActive) {
                gestureActive      = false;
                gestureUpTimeStamp = now;
                gestureCentreUp    = gestureCentre;

                gestureCentre      = {-1.f, -1.f};
                gestureLastCentre  = {-1.f, -1.f};
                gestureCentreDiff  = {0.f, 0.f};
                gestureRotationRad = 0.0f;
                gestureRotationDeg = 0.0f;
            }
            if (gestureDragActive) {
                ImGui::GetIO().AddMouseButtonEvent(ImPlot::GetInputMap().Pan, false);
                gestureDragActive = false;

                if (LookAndFeel::instance().touchDiagnostics) {
                    fmt::print("gesture: stop two finger drag - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
                }
            }
            if (gestureZoomActive) {
                gestureZoomActive = false;
                ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonLeft, false);
                ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonRight, false);
                ImGui::GetIO().AddMousePosEvent(0.f, 0.f);
                if (LookAndFeel::instance().touchDiagnostics) {
                    fmt::print("gesture: stop two finger zoom - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
                }
            }
        }

        if (nFingers != 2) {
            return;
        }
        // handle pinch/spread and rotation gestures
        const float prevDist = std::hypot(fingerLastPos[0].x - fingerLastPos[1].x, fingerLastPos[0].y - fingerLastPos[1].y);
        const float currDist = std::hypot(fingerPos[0].x - fingerPos[1].x, fingerPos[0].y - fingerPos[1].y);

        float pinchFactor = currDist / prevDist;

        if constexpr (zoomViaMouseWheel) {
            // zoom interaction via mouse wheel
            ImGui::GetIO().AddMouseWheelEvent((pinchFactor - 1.f) * 2.f, (pinchFactor - 1.f) * 2.f);
        }
        ImVec2 prevDir = {fingerLastPos[1].x - fingerLastPos[0].x, fingerLastPos[1].y - fingerLastPos[0].y};
        ImVec2 currDir = {fingerPos[1].x - fingerPos[0].x, fingerPos[1].y - fingerPos[0].y};

        float prevAngle = std::atan2(prevDir.y, prevDir.x);
        float currAngle = std::atan2(currDir.y, currDir.x);

        gestureRotationRad = (currAngle - prevAngle);
        gestureRotationDeg = gestureRotationRad * (180.f / std::numbers::pi_v<float>);

        if (LookAndFeel::instance().touchDiagnostics) {
            fmt::print("multi-gesture event -- {}: numFingers: {} @({},{} delta {},{}) pinchFactor:{} dTheta:{}\n", fingerTimeStamp[0], nFingers, fingerLastPos[0].x, fingerLastPos[0].y, fingerPosDiff[1].x, fingerPosDiff[1].y, pinchFactor, gestureRotationDeg);
        }
    }

    static void resetState() {
        touchActive         = false;
        fingerDown          = false;
        fingerUp            = false;
        singleFingerClicked = false;
    }

    inline static std::map<ImGuiID, std::pair<bool, ImPlotRange>[ImAxis_COUNT]> plotLimits;
    inline static ImGuiID                                                       zoomablePlotInit = 0UL;

    //
    inline static bool BeginZoomablePlot(const std::string& plotName, const ImVec2& size, ImPlotFlags flags) {
        assert((zoomablePlotInit == 0) && "mismatched BeginZoomablePlot <-> EndZoomablePlot");
        const ImGuiID ID = ImHashStr(plotName.c_str(), plotName.length());
        zoomablePlotInit = ID;

        if (auto limits = plotLimits.find(ID); limits != plotLimits.end()) {
            for (int axisID = 0; axisID < ImAxis_COUNT; ++axisID) {
                auto& [apply, range] = limits->second[axisID];
                if (!apply) {
                    continue;
                }
                ImPlot::SetNextAxisLimits(axisID, range.Min, range.Max, ImGuiCond_Always);
                apply = false;
            }
        }

        if (ImPlot::BeginPlot(plotName.c_str(), size, flags)) {
            return true;
        }
        zoomablePlotInit = 0UL;
        return false;
    }

    static void EndZoomablePlot() {
        Digitizer::utils::scope_exit guard = [&] {
            zoomablePlotInit = 0UL;
            ImPlot::EndPlot();
        };

        if (!gestureActive || gestureDragActive || nFingers != 2) {
            return;
        }
        constexpr auto isPointInRect = [](const ImVec2& point, const ImRect& rect) -> bool { return point.x >= rect.Min.x && point.x <= rect.Max.x && point.y >= rect.Min.y && point.y <= rect.Max.y; };
        if (!isPointInRect(gestureCentre, ImPlot::GetCurrentContext()->CurrentPlot->PlotRect)) {
            return;
        }

        if constexpr (zoomViaMouseWheel) {
            const ImVec2 initialDist = fingerPosDown[0] - fingerPosDown[1];
            const ImVec2 currDist    = fingerPosDiff[0] - fingerPosDiff[1];
            const ImVec2 zoomFactor  = {1.0f - currDist.x / initialDist.x, 1.0f - currDist.y / initialDist.y};

            const float ZOOM_THRESHOLD = LookAndFeel::instance().isDesktop ? 0.001f : 0.02f;
            if (std::abs(zoomFactor.x - 1.f) < ZOOM_THRESHOLD && std::abs(zoomFactor.y - 1.f) < ZOOM_THRESHOLD) {
                return;
            }
        }

        if (!gestureZoomActive) {
            gestureZoomActive = true;
            ImGui::GetIO().AddMouseButtonEvent(ImGuiPopupFlags_MouseButtonLeft, false);
            if (LookAndFeel::instance().touchDiagnostics) {
                fmt::print("gesture: start two finger zoom - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
            }
        }

        const auto processAxis = [ALPHA = 0.8](const ImPlotAxis& axis, ImPlotRange& rangeToUpdate, const std::array<ImVec2, N_MAX_FINGERS>& fingerPosDown, const std::array<ImVec2, N_MAX_FINGERS>& fingerPos) {
            // check flipping of upper/lower finger position
            bool initialOrder = (axis.Vertical ? fingerPosDown[0].y : fingerPosDown[0].x) < (axis.Vertical ? fingerPosDown[1].y : fingerPosDown[1].x);
            bool currentOrder = (axis.Vertical ? fingerPos[0].y : fingerPos[0].x) < (axis.Vertical ? fingerPos[1].y : fingerPos[1].x);

            // if the initial order is different from the current order, fingers have crossed.
            bool fingersCrossed = initialOrder != currentOrder;

            // assign positions based on whether fingers are crossed.
            const float initialPosLower  = axis.Vertical ? (fingersCrossed ? fingerPosDown[1].y : fingerPosDown[0].y) : (fingersCrossed ? fingerPosDown[1].x : fingerPosDown[0].x);
            const float initialPosHigher = axis.Vertical ? (fingersCrossed ? fingerPosDown[0].y : fingerPosDown[1].y) : (fingersCrossed ? fingerPosDown[0].x : fingerPosDown[1].x);
            const float currentPosLower  = axis.Vertical ? (fingersCrossed ? fingerPos[1].y : fingerPos[0].y) : (fingersCrossed ? fingerPos[1].x : fingerPos[0].x);
            const float currentPosHigher = axis.Vertical ? (fingersCrossed ? fingerPos[0].y : fingerPos[1].y) : (fingersCrossed ? fingerPos[0].x : fingerPos[1].x);

            const ImPlotRect  currLimits   = ImPlot::GetPlotLimits();
            const ImPlotRange currentRange = axis.Vertical ? currLimits.Y : currLimits.X;

            // store initial plot and finger positions
            static std::map<ImGuiID, std::pair<std::array<float, 2>, std::pair<ImPlotRange, ImPlotRange>>> initialAxisData;
            // check if the initial range data for this axis ID exists
            if (initialAxisData.find(axis.ID) == initialAxisData.end() || initialAxisData[axis.ID].first[0] != initialPosLower || initialAxisData[axis.ID].first[1] != initialPosHigher) {
                initialAxisData[axis.ID] = {{initialPosLower, initialPosHigher}, {currentRange, {axis.PixelsToPlot(initialPosLower), axis.PixelsToPlot(initialPosHigher)}}};
            }
            const auto [initialPixelPosition, initialPlotPosition]   = initialAxisData[axis.ID];
            const auto [initialPlotRange, initialPlotFingerPosition] = initialPlotPosition;

            // panning calculation:
            const double panCentre        = 0.5 * (axis.PixelsToPlot(currentPosLower) + axis.PixelsToPlot(currentPosHigher));
            const double panCentreInitial = 0.5 * (initialPlotFingerPosition.Min + initialPlotFingerPosition.Max);
            const double averagePanAmount = 4.0 * (panCentre - panCentreInitial);

            // zoom calculation:
            const double currentDistance = std::abs(static_cast<double>(currentPosHigher) - static_cast<double>(currentPosLower));
            const double initialDistance = static_cast<double>(std::max(std::abs(initialPixelPosition[1] - initialPixelPosition[0]), ImGui::GetIO().MouseDragThreshold));
            const double zoomFactor      = std::clamp(currentDistance / initialDistance, 0.1, 20.0);
            const double rangeCenter     = (initialPlotRange.Max + initialPlotRange.Min) / 2.0;
            const double newHalfRange    = (initialPlotRange.Max - initialPlotRange.Min) * 0.5 / zoomFactor;
            const double targetMin       = rangeCenter - newHalfRange - averagePanAmount;
            const double targetMax       = rangeCenter + newHalfRange - averagePanAmount;

            if (!((axis.Flags & ImPlotAxisFlags_LockMin) || axis.FitThisFrame)) {
                rangeToUpdate.Min = ALPHA * currentRange.Min + (1.0 - ALPHA) * targetMin;
            } else {
                rangeToUpdate.Min = currentRange.Min;
            }

            if (!((axis.Flags & ImPlotAxisFlags_LockMax) || axis.FitThisFrame)) {
                rangeToUpdate.Max = ALPHA * currentRange.Max + (1.0 - ALPHA) * targetMax;
            } else {
                rangeToUpdate.Max = currentRange.Max;
            }
        };

        // retrieve or create the plot limit entry and apply the new limits
        auto& limitsEntry = plotLimits[zoomablePlotInit];
        for (std::size_t axisID = 0UL; axisID < ImAxis_COUNT; ++axisID) {
            auto& axis = ImPlot::GetCurrentContext()->CurrentPlot->Axes[axisID];
            if (!axis.Enabled) {
                limitsEntry[axisID].first = false;
                continue;
            }
            ImPlotRange range;
            processAxis(axis, range, fingerPosDown, fingerPos);
            limitsEntry[axisID].first  = true;
            limitsEntry[axisID].second = range;
        }
    }

    static void applyToImGui() {
        if (LookAndFeel::instance().touchDiagnostics) {
            drawFingerPositions();
        }

        if (!touchActive) {
            return;
        }

        resetState();
    }
};

} // namespace DigitizerUi

#endif // OPENDIGITIZER_TOUCHHANDLER_HPP
