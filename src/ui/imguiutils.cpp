#include "imguiutils.h"
#include "app.h"
#include "flowgraph.h"

#include <fmt/format.h>

#include <imgui_internal.h>

namespace ImGuiUtils {

DialogButton drawDialogButtons(bool okEnabled) {
    int y = ImGui::GetContentRegionAvail().y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y - 20);
    ImGui::Separator();

    if (!okEnabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Ok") || (okEnabled && ImGui::IsKeyPressed(ImGuiKey_Enter))) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Ok;
    }
    if (!okEnabled) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
        return DialogButton::Cancel;
    }
    return DialogButton::None;
}

float splitter(ImVec2 space, bool vertical, float size, float defaultRatio) {
    auto   storage    = ImGui::GetStateStorage();

    auto   ctxid      = ImGui::GetID("splitter_context");
    float  startRatio = storage->GetFloat(ctxid, defaultRatio);
    auto   ratioId    = ImGui::GetID("splitter_ratio");

    float *ratio      = storage->GetFloatRef(ratioId, startRatio);
    float  s          = vertical ? space.x : space.y;
    auto   w          = s * *ratio;
    if (vertical) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + s - w - size / 2.f);
    } else {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + s - w - size / 2.f);
    }

    ImGui::BeginChild("##c");
    ImGui::Button("##sep", vertical ? ImVec2{ size, space.y } : ImVec2{ space.x, size });

    if (ImGui::IsItemActive()) {
        const auto delta = ImGui::GetMouseDragDelta();
        *ratio           = startRatio - (vertical ? delta.x : delta.y) / s;
    } else {
        storage->SetFloat(ctxid, *ratio);
    }
    ImGui::EndChild();
    return *ratio;
}

void blockParametersControls(DigitizerUi::Block *b, bool verticalLayout, const ImVec2 &size) {
    const auto availableSize = ImGui::GetContentRegionAvail();

    auto       storage       = ImGui::GetStateStorage();
    ImGui::PushID("block_controls");

    const auto &style     = ImGui::GetStyle();
    const auto  indent    = style.IndentSpacing;
    const auto  textColor = ImGui::ColorConvertFloat4ToU32(style.Colors[ImGuiCol_Text]);

    for (int i = 0; i < b->type->parameters.size(); ++i) {
        const auto &p  = b->type->parameters[i];

        auto        id = ImGui::GetID(p.label.c_str());
        ImGui::PushID(id);
        auto *enabled = storage->GetBoolRef(id, true);

        ImGui::BeginGroup();
        const auto curpos = ImGui::GetCursorPos();
        ImGui::SetCursorPosY(curpos.y + ImGui::GetFrameHeightWithSpacing());
        ImGui::BeginGroup();

        if (*enabled) {
            char label[64];
            snprintf(label, sizeof(label), "##parameter_%d", i);

            if (auto *e = std::get_if<DigitizerUi::BlockType::EnumParameter>(&p.impl)) {
                auto value = std::get<DigitizerUi::Block::EnumParameter>(b->parameters()[i]);

                for (int j = 0; j < e->options.size(); ++j) {
                    auto &opt      = e->options[j];

                    bool  selected = value.optionIndex == j;
                    if (ImGui::RadioButton(opt.c_str(), selected)) {
                        value.optionIndex = j;
                        b->setParameter(i, value);
                        b->update();
                    }
                }
            } else if (auto *ip = std::get_if<DigitizerUi::Block::NumberParameter<int>>(&b->parameters()[i])) {
                int val = ip->value;
                ImGui::SetNextItemWidth(100);
                ImGui::DragInt(label, &val);
                b->setParameter(i, DigitizerUi::Block::NumberParameter<int>{ val });
                b->update();
            } else if (auto *fp = std::get_if<DigitizerUi::Block::NumberParameter<float>>(&b->parameters()[i])) {
                float val = fp->value;
                ImGui::SetNextItemWidth(100);
                ImGui::DragFloat(label, &val, 0.1f);
                b->setParameter(i, DigitizerUi::Block::NumberParameter<float>{ val });
                b->update();
            } else if (auto *rp = std::get_if<DigitizerUi::Block::RawParameter>(&b->parameters()[i])) {
                std::string val = rp->value;
                ImGui::SetNextItemWidth(100);
                ImGui::InputText(label, &val);
                b->setParameter(i, DigitizerUi::Block::RawParameter{ std::move(val) });
                b->update();
            }
        }
        ImGui::EndGroup();
        ImGui::SameLine(0, 0);

        auto        width = verticalLayout ? availableSize.x : ImGui::GetCursorPosX() - curpos.x;
        const auto *text  = *enabled || verticalLayout ? p.label.c_str() : "";
        width             = std::max(width, indent + ImGui::CalcTextSize(text).x + style.FramePadding.x * 2);

        if (*enabled) {
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_ButtonActive]);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_TabUnfocusedActive]);
        }

        ImGui::SetCursorPos(curpos);

        float height = !verticalLayout && !*enabled ? availableSize.y : 0.f;
        if (ImGui::Button("##nothing", { width, height })) {
            *enabled = !*enabled;
        }
        ImGui::PopStyleColor();

        setItemTooltip(p.label.c_str());

        ImGui::SetCursorPos(curpos + ImVec2(style.FramePadding.x, style.FramePadding.y));
        ImGui::RenderArrow(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), textColor, *enabled ? ImGuiDir_Down : ImGuiDir_Right, 1.0f);

        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        if (*enabled || verticalLayout) {
            ImGui::TextUnformatted(p.label.c_str());
        }

        ImGui::EndGroup();

        if (!verticalLayout) {
            ImGui::SameLine();
        }

        ImGui::PopID();
    }
    ImGui::PopID();
}

} // namespace ImGuiUtils
