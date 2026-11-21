#ifndef OPENDIGITIZER_TOUCHHANDLER_HPP
#define OPENDIGITIZER_TOUCHHANDLER_HPP

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <print>
#include <stack>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <implot_internal.h>

#include <SDL3/SDL.h>

#include "../common/LookAndFeel.hpp"
#include "stdex/cast.hpp"

namespace gr::od {

template<typename ClockSourceType = std::chrono::system_clock, bool zoomViaMouseWheel = false>
struct TouchHandler {
    using TimePoint                                 = std::chrono::time_point<ClockSourceType>;
    constexpr static inline std::size_t kMaxFingers = 10;

    static inline std::unordered_map<SDL_FingerID, std::size_t> fingerIdToIndex;
    static inline std::stack<std::size_t>                       releasedIndices;
    static inline std::size_t                                   nextAvailableIndex = 0;

    static inline std::array<bool, kMaxFingers>      fingerPressed{};
    static inline std::array<ImVec2, kMaxFingers>    fingerPos{};
    static inline std::array<ImVec2, kMaxFingers>    fingerLastPos{}; // previous frame's finger position
    static inline std::array<ImVec2, kMaxFingers>    fingerPosDiff{};
    static inline std::array<ImVec2, kMaxFingers>    fingerPosDown{};
    static inline std::array<ImVec2, kMaxFingers>    fingerPosUp{};
    static inline std::array<TimePoint, kMaxFingers> fingerLastChangeTimeStamp{};
    static inline std::array<TimePoint, kMaxFingers> fingerDownTimeStamp{};
    static inline TimePoint                          gestureTimeStamp{};
    static inline ImVec2                             gestureCentre{-1.f, -1.f};
    static inline ImVec2                             gestureCentreDiff{0.f, 0.f};
    static inline ImVec2                             gestureLastCentre{-1.f, -1.f};
    static inline ImVec2                             gestureCentreDown{-1.f, -1.f};
    static inline ImVec2                             gestureCentreUp{-1.f, -1.f};
    static inline bool                               gestureActive      = false;
    static inline bool                               gestureDragActive  = false;
    static inline bool                               gestureZoomActive  = false;
    static inline float                              gestureRotationRad = 0.0f;
    static inline float                              gestureRotationDeg = 0.0f;

    static inline bool        diagnostics         = false; // prints and draws finger/gesture state
    static inline std::size_t nFingers            = 0;
    static inline bool        fingerDown          = false;
    static inline bool        fingerUp            = false;
    static inline bool        touchActive         = false;
    static inline bool        singleFingerClicked = false;

    inline static std::map<ImGuiID, std::pair<bool, ImPlotRange>[ImAxis_COUNT]>                           plotLimits;
    inline static std::map<ImGuiID, std::pair<std::array<float, 2>, std::pair<ImPlotRange, ImPlotRange>>> initialAxisData;
    inline static ImGuiID                                                                                 zoomablePlotInit = 0UL;

    static std::size_t getOrAssignIndex(SDL_FingerID fingerId) {
        if (auto [it, inserted] = fingerIdToIndex.emplace(fingerId, nextAvailableIndex); !inserted) {
            return it->second;
        }

        if (!releasedIndices.empty()) {
            const std::size_t index = releasedIndices.top();
            releasedIndices.pop();
            fingerIdToIndex[fingerId] = index;
            return index;
        }

        fingerIdToIndex[fingerId] = nextAvailableIndex;
        assert(((nextAvailableIndex + 1) < kMaxFingers) && "more fingers than kMaxFingers detected");
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

    static void releaseFinger(std::size_t fingerIndex) {
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        fingerPressed[fingerIndex] = false;
        assert(nFingers > 0);
        --nFingers;
        touchActive         = true;
        singleFingerClicked = false;
        releaseFingerIndex(fingerIndex);
    }

    static auto getFingerPressedDuration(std::size_t fingerIndex) {
        if (fingerIndex >= kMaxFingers) {
            return std::chrono::microseconds(-1);
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(fingerLastChangeTimeStamp[fingerIndex] - fingerDownTimeStamp[fingerIndex]);
    }

    static void drawFingerPositions() {
        const float circleRadius = 10.0f;
        const auto  now          = ClockSourceType::now();
        ImDrawList* drawList     = ImGui::GetForegroundDrawList();

        for (std::size_t fingerIndex = 0; fingerIndex < kMaxFingers; ++fingerIndex) {
            if (fingerPressed[fingerIndex] && fingerPosDown[fingerIndex].x != -1.f && fingerPosDown[fingerIndex].y != -1.f) {
                drawList->AddCircle(fingerPosDown[fingerIndex], circleRadius, IM_COL32(0, 255, 0, 255), 12, 3.0f);
            }

            if (fingerPressed[fingerIndex] && fingerLastPos[fingerIndex].x != -1.f && fingerLastPos[fingerIndex].y != -1.f) {
                drawList->AddCircle(fingerLastPos[fingerIndex], 3.0f * circleRadius, IM_COL32(165, 165, 0, 255), 12, 3.0f);
            }

            if (fingerPressed[fingerIndex] && fingerPos[fingerIndex].x != -1.f && fingerPos[fingerIndex].y != -1.f) {
                drawList->AddCircle(fingerPos[fingerIndex], 3.0f * circleRadius, IM_COL32(255, 165, 0, 255), 12, 3.0f);
            }

            const auto timeSinceLifted = std::chrono::duration_cast<std::chrono::milliseconds>(now - fingerLastChangeTimeStamp[fingerIndex]);
            if (fingerPosUp[fingerIndex].x != -1.f && fingerPosUp[fingerIndex].y != -1.f && (timeSinceLifted < std::chrono::seconds(3))) {
                drawList->AddNgon(fingerPosUp[fingerIndex], circleRadius, IM_COL32(255, 0, 0, 255), 3, 3.0f);
            }
        }

        const auto timeSinceLifted = std::chrono::duration_cast<std::chrono::milliseconds>(now - gestureTimeStamp);
        if (gestureActive && gestureCentreDown.x != -1.f && gestureCentreDown.y != -1.f) {
            drawList->AddNgon(gestureCentreDown, 1.0f * circleRadius, IM_COL32(0, 255, 0, 255), 5, 3.0f);
        }
        if (gestureActive && gestureLastCentre.x != -1.f && gestureLastCentre.y != -1.f) {
            drawList->AddNgon(gestureLastCentre, 3.0f * circleRadius, IM_COL32(165, 165, 0, 255), 5, 3.0f);
        }
        if (gestureActive && gestureCentre.x != -1.f && gestureCentre.y != -1.f) {
            drawList->AddNgon(gestureCentre, 3.0f * circleRadius, IM_COL32(255, 165, 0, 255), 5, 3.0f);
        }
        if (gestureCentreUp.x != -1.f && gestureCentreUp.y != -1.f && (timeSinceLifted < std::chrono::seconds(3))) {
            drawList->AddNgon(gestureCentreUp, circleRadius, IM_COL32(255, 0, 0, 255), 4, 3.0f);
        }
    }

    static void onSdlEvent(const SDL_Event& event) {
        const auto& displaySize = ImGui::GetIO().DisplaySize;
        const auto  now         = ClockSourceType::now();

        switch (event.type) {
        case SDL_EVENT_FINGER_DOWN: {
            const std::size_t fingerIndex          = getOrAssignIndex(event.tfinger.fingerID);
            touchActive                            = true;
            fingerDown                             = true;
            fingerLastChangeTimeStamp[fingerIndex] = now;
            fingerDownTimeStamp[fingerIndex]       = fingerLastChangeTimeStamp[fingerIndex];
            fingerPressed[fingerIndex]             = true;
            fingerPos[fingerIndex]                 = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerLastPos[fingerIndex]             = fingerPos[fingerIndex];
            fingerPosDiff[fingerIndex]             = {0.f, 0.f};
            fingerPosDown[fingerIndex]             = fingerPos[fingerIndex];
            fingerPosUp[fingerIndex]               = {-1.f, -1.f};
            ++nFingers;

            if (nFingers >= 2 && !gestureActive) {
                if (singleFingerClicked) {
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false); // release initial finger - not a simple click/drag
                    singleFingerClicked = false;
                }
                gestureActive       = true;
                const ImVec2 centre = {0.5f * fingerPos[0].x + 0.5f * fingerPos[1].x, 0.5f * fingerPos[0].y + 0.5f * fingerPos[1].y};
                gestureLastCentre   = (gestureCentre.x != -1.f && gestureCentre.y != -1.f) ? gestureCentre : centre;
                gestureCentre       = centre;
                gestureCentreDown   = gestureCentre;
            }
            if (!gestureActive && !gestureDragActive && !gestureZoomActive && nFingers == 1) {
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
                ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                singleFingerClicked = true;
            }
            if (diagnostics) {
                std::print("touch: finger down: {} fingerID: {} p:{} @({},{})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y);
            }
        } break;
        case SDL_EVENT_FINGER_UP: {
            const auto knownFinger = fingerIdToIndex.find(event.tfinger.fingerID);
            if (knownFinger == fingerIdToIndex.end()) {
                break;
            }
            const std::size_t fingerIndex          = knownFinger->second;
            touchActive                            = true;
            fingerUp                               = true;
            fingerLastChangeTimeStamp[fingerIndex] = now;
            fingerPressed[fingerIndex]             = false;
            fingerLastPos[fingerIndex]             = fingerPos[fingerIndex];
            fingerPos[fingerIndex]                 = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerPosDiff[fingerIndex]             = fingerPos[fingerIndex] - fingerLastPos[fingerIndex];
            fingerPosUp[fingerIndex]               = fingerPos[fingerIndex];
            assert(nFingers > 0);
            --nFingers;
            releaseIndex(event.tfinger.fingerID);
            if (nFingers == 0 && !gestureActive && !gestureDragActive && !gestureZoomActive) {
                if (getFingerPressedDuration(fingerIndex) < LookAndFeel::kLongPressDuration) {
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    ImGui::GetIO().MouseDown[ImGuiMouseButton_Right]    = false;
                    ImGui::GetIO().MouseClicked[ImGuiMouseButton_Right] = false;
                } else {
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, true);
                    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
                    ImGui::GetIO().MouseDown[ImGuiMouseButton_Left]    = false;
                    ImGui::GetIO().MouseClicked[ImGuiMouseButton_Left] = false;
                    // reset to avoid recurring 'right click emulation'
                    fingerDownTimeStamp[fingerIndex] = now;
                }
            }

            if (!gestureDragActive && !gestureZoomActive && nFingers == 0) {
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
                ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            }

            if (diagnostics) {
                std::print("touch: finger up: {} fingerID: {} p:{} @({},{})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y);
            }
        } break;
        case SDL_EVENT_FINGER_CANCELED: { // lifecycle ends without a lift: no click, no fingerUp
            const auto knownFinger = fingerIdToIndex.find(event.tfinger.fingerID);
            if (knownFinger == fingerIdToIndex.end()) {
                break;
            }
            const std::size_t fingerIndex          = knownFinger->second;
            fingerLastChangeTimeStamp[fingerIndex] = now;
            releaseFinger(fingerIndex);
            if (diagnostics) {
                std::print("touch: finger cancelled: {} fingerID: {}\n", nFingers, fingerIndex);
            }
        } break;
        case SDL_EVENT_FINGER_MOTION: {
            const auto knownFinger = fingerIdToIndex.find(event.tfinger.fingerID);
            if (knownFinger == fingerIdToIndex.end()) {
                break;
            }
            const std::size_t fingerIndex          = knownFinger->second;
            touchActive                            = true;
            fingerLastChangeTimeStamp[fingerIndex] = now;
            fingerPressed[fingerIndex]             = true;
            fingerLastPos[fingerIndex]             = fingerPos[fingerIndex];
            fingerPos[fingerIndex]                 = {event.tfinger.x * displaySize.x, event.tfinger.y * displaySize.y};
            fingerPosDiff[fingerIndex]             = fingerPos[fingerIndex] - fingerLastPos[fingerIndex];
            if (nFingers == 1) {
                ImGui::GetIO().AddMousePosEvent(fingerPos[fingerIndex].x, fingerPos[fingerIndex].y);
            }
            if (diagnostics) {
                std::print("touch: finger motion: {} fingerID: {} p:{} @({},{}) motion (dx,dy): ({}, {})\n", nFingers, fingerIndex, event.tfinger.pressure, event.tfinger.x, event.tfinger.y, event.tfinger.dx, event.tfinger.dy);
            }
        } break;
        }
    }

    static void updateGestures() {
        const auto now = ClockSourceType::now();

        // auto-lift a finger inactive for more than 5 s: its SDL_EVENT_FINGER_UP was probably lost
        const auto timeSinceAnyLastActive = std::chrono::duration_cast<std::chrono::milliseconds>(now - *std::max_element(fingerLastChangeTimeStamp.begin(), fingerLastChangeTimeStamp.end()));
        for (std::size_t fingerIndex = 0UL; fingerIndex < kMaxFingers; fingerIndex++) {
            const auto timeSinceLastActive = std::chrono::duration_cast<std::chrono::milliseconds>(now - fingerLastChangeTimeStamp[fingerIndex]);
            if (fingerPressed[fingerIndex] && (timeSinceLastActive > std::chrono::seconds(5)) && (timeSinceAnyLastActive > std::chrono::seconds(5))) {
                releaseFinger(fingerIndex);
                fingerUp = true;
                std::print("WARNING: probably lost SDL_FINGER_UP event -> reset inactive fingerID {} out of {} - timeSinceLifted {}\n", fingerIndex, nFingers, timeSinceLastActive);
            }
        }

        if (nFingers >= 2 && fingerPressed[0] && fingerPressed[1]) {
            gestureTimeStamp = now;

            const ImVec2 centre = {0.5f * fingerPos[0].x + 0.5f * fingerPos[1].x, 0.5f * fingerPos[0].y + 0.5f * fingerPos[1].y};
            gestureLastCentre   = (gestureCentre.x != -1.f && gestureCentre.y != -1.f) ? gestureCentre : centre;
            gestureCentre       = centre;

            gestureCentreDiff = gestureCentre - gestureCentreDown;

            if constexpr (zoomViaMouseWheel) {
                if (!gestureDragActive && std::hypot(gestureCentreDiff.x, gestureCentreDiff.y) > ImGui::GetIO().MouseDragThreshold) {
                    ImGui::GetIO().AddMouseButtonEvent(ImPlot::GetInputMap().Pan, true);
                    gestureDragActive = true;
                    if (diagnostics) {
                        std::print("gesture: start two finger drag - centre ({},{}) move {} vs. threshold {}\n", gestureCentreUp.x, gestureCentreUp.y, std::hypot(gestureCentreDiff.x, gestureCentreDiff.y), ImGui::GetIO().MouseDragThreshold);
                    }
                }
            }

            if (gestureDragActive || (!gestureDragActive && std::hypot(gestureCentreDiff.x, gestureCentreDiff.y) > ImGui::GetIO().MouseDragThreshold)) {
                ImGui::GetIO().MousePos   = gestureCentre;
                ImGui::GetIO().MouseDelta = gestureCentre - gestureLastCentre;
            }
        } else if (nFingers == 0) {
            if (gestureActive) {
                gestureActive   = false;
                gestureCentreUp = gestureCentre;

                gestureCentre      = {-1.f, -1.f};
                gestureLastCentre  = {-1.f, -1.f};
                gestureCentreDiff  = {0.f, 0.f};
                gestureRotationRad = 0.0f;
                gestureRotationDeg = 0.0f;
            }
            if (gestureDragActive) {
                ImGui::GetIO().AddMouseButtonEvent(ImPlot::GetInputMap().Pan, false);
                gestureDragActive = false;

                if (diagnostics) {
                    std::print("gesture: stop two finger drag - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
                }
            }
            if (gestureZoomActive) {
                gestureZoomActive = false;
                ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Right, false);
                ImGui::GetIO().AddMousePosEvent(0.f, 0.f);
                if (diagnostics) {
                    std::print("gesture: stop two finger zoom - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
                }
            }
        }

        if (nFingers != 2) {
            return;
        }
        const float prevDist = std::hypot(fingerLastPos[0].x - fingerLastPos[1].x, fingerLastPos[0].y - fingerLastPos[1].y);
        const float currDist = std::hypot(fingerPos[0].x - fingerPos[1].x, fingerPos[0].y - fingerPos[1].y);

        float pinchFactor = currDist / prevDist;

        if constexpr (zoomViaMouseWheel) {
            ImGui::GetIO().AddMouseWheelEvent((pinchFactor - 1.f) * 2.f, (pinchFactor - 1.f) * 2.f);
        }
        ImVec2 prevDir = {fingerLastPos[1].x - fingerLastPos[0].x, fingerLastPos[1].y - fingerLastPos[0].y};
        ImVec2 currDir = {fingerPos[1].x - fingerPos[0].x, fingerPos[1].y - fingerPos[0].y};

        float prevAngle = std::atan2(prevDir.y, prevDir.x);
        float currAngle = std::atan2(currDir.y, currDir.x);

        gestureRotationRad = (currAngle - prevAngle);
        gestureRotationDeg = gestureRotationRad * (180.f / std::numbers::pi_v<float>);

        if (diagnostics) {
            std::print("multi-gesture event -- {}: numFingers: {} @({},{} delta {},{}) pinchFactor:{} dTheta:{}\n", fingerLastChangeTimeStamp[0].time_since_epoch(), nFingers, fingerLastPos[0].x, fingerLastPos[0].y, fingerPosDiff[1].x, fingerPosDiff[1].y, pinchFactor, gestureRotationDeg);
        }
    }

    static void resetState() {
        touchActive         = false;
        fingerDown          = false;
        fingerUp            = false;
        singleFingerClicked = false;
    }

    inline static bool beginZoomablePlot(const std::string& plotName, const ImVec2& size, ImPlotFlags flags) {
        assert((zoomablePlotInit == 0) && "mismatched beginZoomablePlot <-> endZoomablePlot");
        const ImGuiID ID = ImGui::GetID(plotName.c_str());
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

        ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2{0, 0});
        ImPlot::PushStyleVar(ImPlotStyleVar_LabelPadding, ImVec2{3, 1});
        ImPlot::PushStyleVar(ImPlotStyleVar_FitPadding, ImVec2{0.0f, 0.05f});

        if (ImPlot::BeginPlot(plotName.c_str(), size, flags)) {
            return true;
        }
        ImPlot::PopStyleVar(3);
        zoomablePlotInit = 0UL;
        return false;
    }

    static void endZoomablePlot() {
        applyPinchZoom();
        ImPlot::EndPlot();
        ImPlot::PopStyleVar(3);
        zoomablePlotInit = 0UL;
    }

    static void applyPinchZoom() {
        if (!gestureActive || gestureDragActive || nFingers != 2) {
            return;
        }
        const auto isPointInRect = [](const ImVec2& point, const ImRect& rect) { return point.x >= rect.Min.x && point.x <= rect.Max.x && point.y >= rect.Min.y && point.y <= rect.Max.y; };
        if (!isPointInRect(gestureCentre, ImPlot::GetCurrentContext()->CurrentPlot->PlotRect)) {
            return;
        }

        if constexpr (zoomViaMouseWheel) {
            const ImVec2 initialDist = fingerPosDown[0] - fingerPosDown[1];
            const ImVec2 currDist    = fingerPosDiff[0] - fingerPosDiff[1];
            const ImVec2 zoomFactor  = {1.0f - currDist.x / initialDist.x, 1.0f - currDist.y / initialDist.y};

            constexpr float kZoomThreshold = LookAndFeel::kIsDesktop ? 0.001f : 0.02f;
            if (std::abs(zoomFactor.x - 1.f) < kZoomThreshold && std::abs(zoomFactor.y - 1.f) < kZoomThreshold) {
                return;
            }
        }

        if (!gestureZoomActive) {
            gestureZoomActive = true;
            ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            if (diagnostics) {
                std::print("gesture: start two finger zoom - centre ({},{})\n", gestureCentreUp.x, gestureCentreUp.y);
            }
        }

        const auto updateAxis = [ALPHA = 0.8](const ImPlotAxis& axis, ImPlotRange& rangeToUpdate, const std::array<ImVec2, kMaxFingers>& fingerPosDown, const std::array<ImVec2, kMaxFingers>& fingerPos) {
            bool initialOrder = (axis.Vertical ? fingerPosDown[0].y : fingerPosDown[0].x) < (axis.Vertical ? fingerPosDown[1].y : fingerPosDown[1].x);
            bool currentOrder = (axis.Vertical ? fingerPos[0].y : fingerPos[0].x) < (axis.Vertical ? fingerPos[1].y : fingerPos[1].x);

            bool fingersCrossed = initialOrder != currentOrder;

            const float initialPosLower  = axis.Vertical ? (fingersCrossed ? fingerPosDown[1].y : fingerPosDown[0].y) : (fingersCrossed ? fingerPosDown[1].x : fingerPosDown[0].x);
            const float initialPosHigher = axis.Vertical ? (fingersCrossed ? fingerPosDown[0].y : fingerPosDown[1].y) : (fingersCrossed ? fingerPosDown[0].x : fingerPosDown[1].x);
            const float currentPosLower  = axis.Vertical ? (fingersCrossed ? fingerPos[1].y : fingerPos[0].y) : (fingersCrossed ? fingerPos[1].x : fingerPos[0].x);
            const float currentPosHigher = axis.Vertical ? (fingersCrossed ? fingerPos[0].y : fingerPos[1].y) : (fingersCrossed ? fingerPos[0].x : fingerPos[1].x);

            const ImPlotRect  currLimits   = ImPlot::GetPlotLimits();
            const ImPlotRange currentRange = axis.Vertical ? currLimits.Y : currLimits.X;

            if (initialAxisData.find(axis.ID) == initialAxisData.end() || initialAxisData[axis.ID].first[0] != initialPosLower || initialAxisData[axis.ID].first[1] != initialPosHigher) {
                initialAxisData[axis.ID] = {{initialPosLower, initialPosHigher}, {currentRange, {axis.PixelsToPlot(initialPosLower), axis.PixelsToPlot(initialPosHigher)}}};
            }
            const auto [initialPixelPosition, initialPlotPosition]   = initialAxisData[axis.ID];
            const auto [initialPlotRange, initialPlotFingerPosition] = initialPlotPosition;

            const double panCentre        = 0.5 * (axis.PixelsToPlot(currentPosLower) + axis.PixelsToPlot(currentPosHigher));
            const double panCentreInitial = 0.5 * (initialPlotFingerPosition.Min + initialPlotFingerPosition.Max);
            const double averagePanAmount = 4.0 * (panCentre - panCentreInitial);

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

        auto& limitsEntry = plotLimits[zoomablePlotInit];
        for (std::size_t axisID = 0UL; axisID < ImAxis_COUNT; ++axisID) {
            auto& axis = ImPlot::GetCurrentContext()->CurrentPlot->Axes[axisID];
            if (!axis.Enabled) {
                limitsEntry[axisID].first = false;
                continue;
            }
            ImPlotRange range;
            updateAxis(axis, range, fingerPosDown, fingerPos);
            limitsEntry[axisID].first  = true;
            limitsEntry[axisID].second = range;
        }
    }

    static void endFrame() {
        if (diagnostics) {
            drawFingerPositions();
        }

        if (!touchActive) {
            return;
        }

        resetState();
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_TOUCHHANDLER_HPP
