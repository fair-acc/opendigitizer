#ifndef OPENDIGITIZER_COMPONENTS_PORTSHAPES_HPP
#define OPENDIGITIZER_COMPONENTS_PORTSHAPES_HPP

#include <imgui.h>

namespace DigitizerUi {

// size is a suggestion because some icons (complex numbers) will overflow slightly
using PortShapeFunction = void (*)(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);

void drawRectanglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawDot(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawRingedDot(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawDotPair(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawRingedDotPair(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawTriangle(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawHollowTriangle(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSquare(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawHollowSquare(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawPentagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawHollowPentagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawHexagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawHollowHexagon(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawDiamond(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawGridWithCornerRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawGridWithCornerDoubleRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawWavesWithCornerRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawWavesWithCornerDoubleRing(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);

void drawSolidRectanglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidDotPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidDotPairPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidTrianglePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidSquarePort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidPentagonPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidHexagonPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidDiamondPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);
void drawSolidCornerRingPort(ImDrawList* drawList, ImVec2 center, float size, ImU32 color);

} // namespace DigitizerUi

#endif // OPENDIGITIZER_COMPONENTS_PORTSHAPES_HPP
