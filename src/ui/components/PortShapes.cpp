#include "PortShapes.hpp"

#include "../common/LookAndFeel.hpp"
#include "DataTypeStyle.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace DigitizerUi {

namespace {

constexpr float kPointingUp   = -std::numbers::pi_v<float> / 2.f;
constexpr float kAxisAligned  = -3.f * std::numbers::pi_v<float> / 4.f;
constexpr int   kWaveRowCount = 3;

[[nodiscard]] const LookAndFeel::Flowgraph& flowgraphLookAndFeel() { return LookAndFeel::instance().flowgraph; }

template<std::size_t nVertices>
[[nodiscard]] std::array<ImVec2, nVertices> regularPolygonInSquare(ImVec2 center, float size, float firstVertexAngle, float inset) {
    std::array<ImVec2, nVertices> unitVertices;
    for (std::size_t i = 0; i < nVertices; ++i) {
        const float angle = firstVertexAngle + 2.f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(nVertices);
        unitVertices[i]   = ImVec2{std::cos(angle), std::sin(angle)};
    }

    const auto [minX, maxX]               = std::ranges::minmax(unitVertices | std::views::transform(&ImVec2::x));
    const auto [minY, maxY]               = std::ranges::minmax(unitVertices | std::views::transform(&ImVec2::y));
    const float  circumscribedRadius      = size / std::max(maxX - minX, maxY - minY);
    const ImVec2 polygonCenter            = center - ImVec2{(minX + maxX) / 2.f, (minY + maxY) / 2.f} * circumscribedRadius;
    const float  insetCircumscribedRadius = circumscribedRadius - inset / std::cos(std::numbers::pi_v<float> / static_cast<float>(nVertices));

    std::array<ImVec2, nVertices> vertices;
    std::ranges::transform(unitVertices, vertices.begin(), [&](ImVec2 unitVertex) { return polygonCenter + unitVertex * insetCircumscribedRadius; });
    return vertices;
}

template<std::size_t nVertices>
void fillRegularPolygon(ImDrawList* drawList, ImVec2 center, float size, float firstVertexAngle, ImU32 color) {
    const auto vertices = regularPolygonInSquare<nVertices>(center, size, firstVertexAngle, 0.f);
    drawList->AddConvexPolyFilled(vertices.data(), static_cast<int>(nVertices), color);
}

template<std::size_t nVertices>
void strokeRegularPolygon(ImDrawList* drawList, ImVec2 center, float size, float firstVertexAngle, ImU32 color) {
    const float thickness = flowgraphLookAndFeel().dataTypeIconLineThickness;
    const auto  vertices  = regularPolygonInSquare<nVertices>(center, size, firstVertexAngle, thickness / 2.f);
    drawList->AddPolyline(vertices.data(), static_cast<int>(nVertices), color, ImDrawFlags_Closed, thickness);
}

struct DotPair {
    std::array<ImVec2, 2> centers;
    float                 diameter;
};

[[nodiscard]] DotPair dotPairInSquare(ImVec2 center, float size) {
    const auto& lookAndFeel  = flowgraphLookAndFeel();
    const float diameter     = (size * lookAndFeel.complexNumbersIconDotWidthFactor - lookAndFeel.complexNumbersIconDotPairGap) / 2.f;
    const float centerOffset = (diameter + lookAndFeel.complexNumbersIconDotPairGap) / 2.f;
    return {.centers = {center - ImVec2{centerOffset, 0.f}, center + ImVec2{centerOffset, 0.f}}, .diameter = diameter};
}

// some datatype icons like DataSet<T> draw an icon for DataSet with the type for <T> in the bottom right corner. this is
// a description of where to draw things for such icons
struct CornerRingLayout {
    ImVec2 iconMin;
    ImVec2 iconMax;
    ImVec2 ringCenter;
    float  ringRadius;
};

[[nodiscard]] CornerRingLayout cornerRingLayoutInSquare(ImVec2 center, float size) {
    const auto&  lookAndFeel = flowgraphLookAndFeel();
    const float  halfExtent  = size / 2.f - lookAndFeel.dataTypeIconLineThickness / 2.f;
    const float  ringInset   = size * lookAndFeel.containerIconsCornerRingInsetFactor;
    const ImVec2 iconMax     = center + ImVec2{halfExtent, halfExtent};
    return {
        .iconMin    = center - ImVec2{halfExtent, halfExtent},
        .iconMax    = iconMax,
        .ringCenter = iconMax - ImVec2{ringInset, ringInset},
        .ringRadius = size * lookAndFeel.containerIconsCornerRingRadiusFactor,
    };
}

// returns a description of an intersection of a line with a circle, returning the two points on the circle as angles from 0
[[nodiscard]] std::optional<std::pair<float, float>> circleIntersectionParameters(ImVec2 from, ImVec2 to, ImVec2 circleCenter, float circleRadius) {
    const ImVec2 direction    = to - from;
    const ImVec2 fromToCenter = from - circleCenter;
    const float  a            = ImLengthSqr(direction);
    const float  b            = 2.f * ImDot(fromToCenter, direction);
    const float  c            = ImLengthSqr(fromToCenter) - circleRadius * circleRadius;
    const float  discriminant = b * b - 4.f * a * c;
    if (a == 0.f || discriminant <= 0.f) {
        return std::nullopt;
    }
    const float root = std::sqrt(discriminant);
    return std::pair{(-b - root) / (2.f * a), (-b + root) / (2.f * a)};
}

void addPolylineOutsideCircle(ImDrawList* drawList, std::span<const ImVec2> points, ImVec2 circleCenter, float circleRadius, ImU32 color, float thickness) {
    const auto isInside = [&](ImVec2 point) { return ImLengthSqr(point - circleCenter) < circleRadius * circleRadius; };

    std::vector<ImVec2> pointsStaging;
    const auto          drawAndClearPoints = [&] {
        if (pointsStaging.size() >= 2) {
            drawList->AddPolyline(pointsStaging.data(), static_cast<int>(pointsStaging.size()), color, ImDrawFlags_None, thickness);
        }
        pointsStaging.clear();
    };

    if (!points.empty() && !isInside(points.front())) {
        pointsStaging.push_back(points.front());
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        const ImVec2 previousPoint         = points[i - 1];
        const ImVec2 thisPoint             = points[i];
        const bool   isPreviousPointInside = isInside(previousPoint);
        const bool   isThisPointInside     = isInside(thisPoint);
        const auto   intersections         = circleIntersectionParameters(previousPoint, thisPoint, circleCenter, circleRadius);
        const auto   pointOnCircle         = [&](float t) { return previousPoint + (thisPoint - previousPoint) * t; };

        if (!isPreviousPointInside && !isThisPointInside) {
            if (intersections && intersections->first > 0.f && intersections->second < 1.f) {
                pointsStaging.push_back(pointOnCircle(intersections->first));
                drawAndClearPoints();
                pointsStaging.push_back(pointOnCircle(intersections->second));
            }
            pointsStaging.push_back(thisPoint);
        } else if (!isPreviousPointInside && intersections) {
            pointsStaging.push_back(pointOnCircle(intersections->first));
            drawAndClearPoints();
        } else if (!isThisPointInside && intersections) {
            pointsStaging.push_back(pointOnCircle(intersections->second));
            pointsStaging.push_back(thisPoint);
        }
    }
    drawAndClearPoints();
}

void drawCornerRings(ImDrawList* drawList, const CornerRingLayout& layout, ImU32 color, bool withInnerRing) {
    const auto& lookAndFeel = flowgraphLookAndFeel();
    const float thickness   = lookAndFeel.dataTypeIconLineThickness;
    drawList->AddCircle(layout.ringCenter, layout.ringRadius, color, 0, thickness);
    if (withInnerRing) {
        drawList->AddCircle(layout.ringCenter, std::max(0.f, layout.ringRadius - thickness - lookAndFeel.float64IconRingGapWidthPixels), color, 0, thickness);
    }
}

void drawGridWithRings(ImDrawList* drawList, ImVec2 center, float size, ImU32 color, bool withInnerRing) {
    const float  thickness = flowgraphLookAndFeel().dataTypeIconLineThickness;
    const auto   layout    = cornerRingLayoutInSquare(center, size);
    const ImVec2 min       = layout.iconMin;
    const ImVec2 max       = layout.iconMax;

    const std::array border{max, ImVec2{max.x, min.y}, min, ImVec2{min.x, max.y}, max};
    const std::array verticalDivider{ImVec2{center.x, min.y}, ImVec2{center.x, max.y}};
    const std::array horizontalDivider{ImVec2{min.x, center.y}, ImVec2{max.x, center.y}};
    for (std::span<const ImVec2> line : {std::span<const ImVec2>(border), std::span<const ImVec2>(verticalDivider), std::span<const ImVec2>(horizontalDivider)}) {
        addPolylineOutsideCircle(drawList, line, layout.ringCenter, layout.ringRadius, color, thickness);
    }
    drawCornerRings(drawList, layout, color, withInnerRing);
}

void drawWavesWithRings(ImDrawList* drawList, ImVec2 center, float size, ImU32 color, bool withInnerRing) {
    const auto& lookAndFeel  = flowgraphLookAndFeel();
    const auto  layout       = cornerRingLayoutInSquare(center, size);
    const auto  extent       = layout.iconMax - layout.iconMin;
    const float amplitude    = size * lookAndFeel.dataSetIconWaveAmplitudeFactor;
    const int   segmentCount = std::max(1, lookAndFeel.dataSetIconNumLineSegmentsPerWave);

    std::vector<ImVec2> wave(static_cast<std::size_t>(segmentCount) + 1);
    for (int row = 0; row < kWaveRowCount; ++row) {
        const float baselineY = layout.iconMin.y + extent.y * static_cast<float>(2 * row + 1) / (2.f * kWaveRowCount);
        for (std::size_t i = 0; i < wave.size(); ++i) {
            const float phase = static_cast<float>(i) / static_cast<float>(segmentCount);
            wave[i]           = ImVec2{layout.iconMin.x + extent.x * phase, baselineY - amplitude * std::sin(2.f * std::numbers::pi_v<float> * phase)};
        }
        addPolylineOutsideCircle(drawList, wave, layout.ringCenter, layout.ringRadius, color, lookAndFeel.dataTypeIconLineThickness);
    }
    drawCornerRings(drawList, layout, color, withInnerRing);
}

} // namespace

void drawRectanglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const ImVec2 halfExtent{size / 2.f, size / 2.f};
    const float  rounding = flowgraphLookAndFeel().rectangularPortRounding;
    drawList->AddRectFilled(center - halfExtent, center + halfExtent, color, rounding);
    drawList->AddRect(center - halfExtent, center + halfExtent, darkenOrLighten(color), rounding);
}

void drawDot(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawList->AddCircleFilled(center, size / 2.f, color); }

void drawRingedDot(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const auto& lookAndFeel = flowgraphLookAndFeel();
    const float thickness   = lookAndFeel.dataTypeIconLineThickness;
    drawList->AddCircle(center, size / 2.f - thickness / 2.f, color, 0, thickness);
    drawList->AddCircleFilled(center, std::max(0.f, size / 2.f - thickness - lookAndFeel.float64IconRingGapWidthPixels), color);
}

void drawDotPair(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const auto pair = dotPairInSquare(center, size);
    std::ranges::for_each(pair.centers, [&](ImVec2 dotCenter) { drawDot(drawList, dotCenter, pair.diameter, color); });
}

void drawRingedDotPair(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const auto pair = dotPairInSquare(center, size);
    std::ranges::for_each(pair.centers, [&](ImVec2 dotCenter) { drawRingedDot(drawList, dotCenter, pair.diameter, color); });
}

void drawTriangle(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { fillRegularPolygon<3>(drawList, center, size, kPointingUp, color); }
void drawHollowTriangle(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { strokeRegularPolygon<3>(drawList, center, size, kPointingUp, color); }
void drawSquare(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { fillRegularPolygon<4>(drawList, center, size, kAxisAligned, color); }
void drawHollowSquare(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { strokeRegularPolygon<4>(drawList, center, size, kAxisAligned, color); }
void drawPentagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { fillRegularPolygon<5>(drawList, center, size, kPointingUp, color); }
void drawHollowPentagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { strokeRegularPolygon<5>(drawList, center, size, kPointingUp, color); }
void drawHexagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { fillRegularPolygon<6>(drawList, center, size, kPointingUp, color); }
void drawHollowHexagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { strokeRegularPolygon<6>(drawList, center, size, kPointingUp, color); }
void drawDiamond(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { fillRegularPolygon<4>(drawList, center, size, kPointingUp, color); }

void drawGridWithCornerRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawGridWithRings(drawList, center, size, color, false); }
void drawGridWithCornerDoubleRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawGridWithRings(drawList, center, size, color, true); }
void drawWavesWithCornerRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawWavesWithRings(drawList, center, size, color, false); }
void drawWavesWithCornerDoubleRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawWavesWithRings(drawList, center, size, color, true); }

void drawSolidRectanglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const ImVec2 halfExtent{size / 2.f, size / 2.f};
    drawList->AddRectFilled(center - halfExtent, center + halfExtent, color, flowgraphLookAndFeel().rectangularPortRounding);
}

void drawSolidDotPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawDot(drawList, center, size, color); }
void drawSolidDotPairPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawDotPair(drawList, center, size, color); }
void drawSolidTrianglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawTriangle(drawList, center, size, color); }
void drawSolidSquarePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawSquare(drawList, center, size, color); }
void drawSolidPentagonPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawPentagon(drawList, center, size, color); }
void drawSolidHexagonPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawHexagon(drawList, center, size, color); }
void drawSolidDiamondPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) { drawDiamond(drawList, center, size, color); }

// this is a bit awkward to fill, and unfortunately we need to do it concave to avoid having two shapes which might overlap if transparent or have gaps
void drawSolidCornerRingPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color) {
    const auto   cornerRingLayout = cornerRingLayoutInSquare(center, size);
    const float  outerRadius      = cornerRingLayout.ringRadius + flowgraphLookAndFeel().dataTypeIconLineThickness / 2.f;
    const ImVec2 min              = center - ImVec2{size / 2.f, size / 2.f};
    const ImVec2 max              = center + ImVec2{size / 2.f, size / 2.f};

    const ImVec2 ringToBottomEdge{0.f, max.y - cornerRingLayout.ringCenter.y};
    const ImVec2 ringToRightEdge{max.x - cornerRingLayout.ringCenter.x, 0.f};
    const float  bottomEdgeCutX     = -std::sqrt(std::max(0.f, outerRadius * outerRadius - ringToBottomEdge.y * ringToBottomEdge.y));
    const float  rightEdgeCutY      = -std::sqrt(std::max(0.f, outerRadius * outerRadius - ringToRightEdge.x * ringToRightEdge.x));
    const float  bottomEdgeCutAngle = std::atan2(ringToBottomEdge.y, bottomEdgeCutX);
    const float  rightEdgeCutAngle  = std::atan2(rightEdgeCutY, ringToRightEdge.x);

    drawList->PathLineTo({max.x, min.y});
    drawList->PathLineTo(min);
    drawList->PathLineTo({min.x, max.y});
    drawList->PathArcTo(cornerRingLayout.ringCenter, outerRadius, bottomEdgeCutAngle, rightEdgeCutAngle);
    drawList->PathFillConcave(color);
}

} // namespace DigitizerUi
