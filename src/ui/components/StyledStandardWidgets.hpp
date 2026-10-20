#ifndef OPENDIGITIZER_UI_COMPONENTS_STYLED_STANDARD_WIDGETS_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_STYLED_STANDARD_WIDGETS_HPP_

#include <c_resource.hpp>

#include <imgui.h>

/*
 * Functions that replace imgui equivalents and offer customization points for
 * styling the widgets, though they may just call the default imgui
 * implementations depending on the theme.
 *
 * Not all imgui widgets are present because many of them would require
 * reimplementing other (large) imgui widgets that use them (for example, a
 * custom Selectable implementation requires reimplementing imgui Menu). This
 * file just provides the bare minimum replacements required to implement the
 * styles we currently have in opendigitizer
 */
namespace DigitizerUi {

bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, bool active);
bool RadioButton(const char* label, int* v, int vButton);

bool ArrowButton(const char* strId, ImGuiDir dir);

bool BeginCombo(const char* label, const char* previewValue, ImGuiComboFlags flags = 0);
using Combo = stdex::c_resource<bool, BeginCombo, ImGui::EndCombo>;

bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags = 0);

} // namespace DigitizerUi

#endif
