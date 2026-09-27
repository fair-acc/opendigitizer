#ifndef OPENDIGITIZER_IMGUI_NODE_EDITOR_WRAPPER_HPP
#define OPENDIGITIZER_IMGUI_NODE_EDITOR_WRAPPER_HPP

#include "ImguiWrap.hpp"

#include <imgui_node_editor.h>

namespace DigitizerUi::IMW {
namespace NodeEditor {
using Editor        = stdex::c_resource<void, ax::NodeEditor::Begin, ax::NodeEditor::End, true>;
using Creation      = stdex::c_resource<bool, ax::NodeEditor::BeginCreate, ax::NodeEditor::EndCreate, true>;
using Deletion      = stdex::c_resource<bool, ax::NodeEditor::BeginDelete, ax::NodeEditor::EndDelete, true>;
using Node          = stdex::c_resource<void, ax::NodeEditor::BeginNode, ax::NodeEditor::EndNode, true>;
using StyleVar      = stdex::c_resource<void, static_cast<void (*)(ax::NodeEditor::StyleVar, const ImVec2&)>(ax::NodeEditor::PushStyleVar), +detail::singlePop<ax::NodeEditor::PopStyleVar>(), true>;
using StyleFloatVar = stdex::c_resource<void, static_cast<void (*)(ax::NodeEditor::StyleVar, float)>(ax::NodeEditor::PushStyleVar), +detail::singlePop<ax::NodeEditor::PopStyleVar>(), true>;
using StyleColor    = stdex::c_resource<void, static_cast<void (*)(ax::NodeEditor::StyleColor, const ImVec4&)>(ax::NodeEditor::PushStyleColor), +detail::singlePop<ax::NodeEditor::PopStyleColor>(), true>;
} // namespace NodeEditor

} // namespace DigitizerUi::IMW

#endif // OPENDIGITIZER_IMGUI_NODE_EDITOR_WRAPPER_HPP
