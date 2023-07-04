#include "imguiutils.h"
#include "app.h"
#include "flowgraph.h"

#include <fmt/format.h>

#include <imgui_internal.h>

#include "app.h"
#include "flowgraph/datasink.h"

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

    const auto cursor = vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS;
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(cursor);
    }

    if (ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(cursor);
        const auto delta = ImGui::GetMouseDragDelta();
        *ratio           = startRatio - (vertical ? delta.x : delta.y) / s;
    } else {
        storage->SetFloat(ctxid, *ratio);
    }
    ImGui::EndChild();
    return *ratio;
}

void drawBlockControlsPanel(BlockControlsPanel &ctx, const ImVec2 &pos, const ImVec2 &frameSize, bool verticalLayout) {
    using namespace DigitizerUi;

    auto size = frameSize;
    if (ctx.block) {
        if (ctx.closeTime < std::chrono::system_clock::now()) {
            ctx = {};
            return;
        }

        auto &app = App::instance();
        ImGui::PushFont(app.fontIconsSolid);
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing() * 1.5f;
        ImGui::PopFont();

        auto resetTime = [&]() {
            ctx.closeTime = std::chrono::system_clock::now() + app.editPaneCloseDelay;
        };

        const auto itemSpacing    = ImGui::GetStyle().ItemSpacing;

        auto       calcButtonSize = [&](int numButtons) -> ImVec2 {
            if (verticalLayout) {
                return { (size.x - float(numButtons - 1) * itemSpacing.x) / float(numButtons), lineHeight };
            }
            return { lineHeight, (size.y - float(numButtons - 1) * itemSpacing.y) / float(numButtons) };
        };

        ImGui::SetCursorPos(pos);

        if (ImGui::BeginChildFrame(1, size, ImGuiWindowFlags_NoScrollbar)) {
            size          = ImGui::GetContentRegionAvail();

            // don't close the panel while the mouse is hovering it.
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                resetTime();
            }

            auto duration = float(std::chrono::duration_cast<std::chrono::milliseconds>(ctx.closeTime - std::chrono::system_clock::now()).count()) / float(std::chrono::duration_cast<std::chrono::milliseconds>(app.editPaneCloseDelay).count());
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Button]));
            ImGui::ProgressBar(1.f - duration, { size.x, 3 });
            ImGui::PopStyleColor();

            auto minpos = ImGui::GetCursorPos();
            size          = ImGui::GetContentRegionAvail();

            int outputsCount = 0;
            {
                const char *prevString = verticalLayout ? "\uf062" : "\uf060";
                for (const auto &out: ctx.block->outputs()) {
                    outputsCount += out.connections.size();
                }
                if (outputsCount == 0) {
                    ImGuiUtils::DisabledGuard dg;
                    ImGui::PushFont(app.fontIconsSolid);
                    ImGui::Button(prevString, calcButtonSize(1));
                    ImGui::PopFont();
                } else {
                    const auto buttonSize = calcButtonSize(outputsCount);

                    ImGui::BeginGroup();
                    int id = 1;
                    for (auto &out : ctx.block->outputs()) {
                        for (const auto *conn: out.connections) {
                            ImGui::PushID(id++);

                            ImGui::PushFont(app.fontIconsSolid);
                            if (ImGui::Button(prevString, buttonSize)) {
                                ctx.block = conn->ports[1]->block;
                            }
                            ImGui::PopFont();
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("%s", conn->ports[1]->block->name.c_str());
                            }
                            ImGui::PopID();
                            if (verticalLayout) {
                                ImGui::SameLine();
                            }
                        }
                    }
                    ImGui::EndGroup();
                }

            }

            if (!verticalLayout) {
                ImGui::SameLine();
            }

            {
                // Draw the two add block buttons
                ImGui::BeginGroup();
                const auto                buttonSize = calcButtonSize(2);
                {
                    ImGuiUtils::DisabledGuard dg(ctx.mode != BlockControlsPanel::Mode::None || outputsCount == 0);
                    ImGui::PushFont(app.fontIconsSolid);
                    if (ImGui::Button("\uf055", buttonSize)) {
                        if (outputsCount > 1) {
                            ImGui::OpenPopup("insertBlockPopup");
                        } else {
                            [&](){
                                int index = 0;
                                for (auto &out : ctx.block->outputs()) {
                                    for (auto *conn: out.connections) {
                                        ctx.insertFrom = conn->ports[0];
                                        ctx.insertBefore = conn->ports[1];
                                        ctx.breakConnection = conn;
                                        return;
                                    }
                                    ++index;
                                }
                            }();
                            ctx.mode = BlockControlsPanel::Mode::Insert;
                        }
                    }
                    ImGui::PopFont();
                    setItemTooltip("Insert new block before the next");

                    if (ImGui::BeginPopup("insertBlockPopup")) {
                        int index = 0;
                        for (auto &out : ctx.block->outputs()) {
                            for (auto *conn: out.connections) {
                                auto text = fmt::format("Before block '{}'", conn->ports[1]->block->name);
                                if (ImGui::Selectable(text.c_str())) {
                                    ctx.insertBefore = conn->ports[1];
                                    ctx.mode = BlockControlsPanel::Mode::Insert;
                                    ctx.insertFrom = conn->ports[0];
                                    ctx.breakConnection = conn;
                                }
                            }
                            ++index;
                        }
                        ImGui::EndPopup();
                    }

                    if (verticalLayout) {
                        ImGui::SameLine();
                    }
                }

                ImGui::PushFont(app.fontIconsSolid);
                DisabledGuard dg(ctx.mode != BlockControlsPanel::Mode::None || ctx.block->outputs().empty());
                if (ImGui::Button("\uf0fe", buttonSize)) {
                    if (ctx.block->outputs().size() > 1) {
                        ImGui::OpenPopup("addBlockPopup");
                    } else {
                        ctx.mode = BlockControlsPanel::Mode::AddAndBranch;
                        ctx.insertFrom = &ctx.block->outputs()[0];
                    }
                }
                ImGui::PopFont();
                setItemTooltip("Add new block");

                if (ImGui::BeginPopup("addBlockPopup")) {
                    int index = 0;
                    for (const auto &out: ctx.block->type->outputs) {
                        if (ImGui::Selectable(out.name.c_str())) {
                            ctx.insertFrom = &ctx.block->outputs()[index];
                            ctx.mode = BlockControlsPanel::Mode::AddAndBranch;
                        }
                        ++index;
                    }
                }

                ImGui::EndGroup();

                if (!verticalLayout) {
                    ImGui::SameLine();
                }
            }

            if (ctx.mode != BlockControlsPanel::Mode::None) {
                ImGui::BeginGroup();

                auto listSize = verticalLayout ? ImVec2(size.x, 200) : ImVec2(200, size.y - ImGui::GetFrameHeightWithSpacing());
                auto ret      = filteredListBox(
                             "blocks", BlockType::registry().types(), [](auto &it) -> std::pair<BlockType *, std::string> {
                            if (it.second->inputs.size() != 1 || it.second->outputs.size() != 1) {
                                return {};
                            }
                            return std::pair{ it.second.get(), it.first };
                             },
                             listSize);

                {
                    DisabledGuard dg(!ret.has_value());
                    if (ImGui::Button("Ok")) {
                        BlockType  *selected = ret->first;
                        auto        name     = fmt::format("{}({})", selected->name, ctx.block->name);
                        auto        block    = selected->createBlock(name);

                        Connection *c1;
                        if (ctx.mode == BlockControlsPanel::Mode::Insert) {
                            // mode Insert means that the new block should be added in between this block and the next one.
                            // put the new block in between this block and the following one
                            c1 = app.dashboard->localFlowGraph.connect(&block->outputs()[0], ctx.insertBefore);
                            app.dashboard->localFlowGraph.connect(ctx.insertFrom, &block->inputs()[0]);
                            app.dashboard->localFlowGraph.disconnect(ctx.breakConnection);
                            ctx.breakConnection = nullptr;
                        } else {
                            // mode AddAndBranch means the new block should feed its data to a new sink to be also plotted together with the old one.
                            auto *newsink = app.dashboard->createSink();
                            c1            = app.dashboard->localFlowGraph.connect(&block->outputs()[0], &newsink->inputs()[0]);
                            app.dashboard->localFlowGraph.connect(ctx.insertFrom, &block->inputs()[0]);

                            auto source = std::find_if(app.dashboard->sources().begin(), app.dashboard->sources().end(), [&](const auto &s) {
                                return s.block == newsink;
                            });

                            app.dashboardPage.newPlot(app.dashboard.get());
                            app.dashboard->plots().back().sources.push_back(&*source);

                        }
                        ctx.block = block.get();

                        app.dashboard->localFlowGraph.addBlock(std::move(block));
                        ctx.mode = BlockControlsPanel::Mode::None;
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ctx.mode = BlockControlsPanel::Mode::None;
                }

                ImGui::EndGroup();

                if (!verticalLayout) {
                    ImGui::SameLine();
                }
            }

            ImGui::BeginChild("Settings", verticalLayout ? ImVec2(size.x, ImGui::GetContentRegionAvail().y - lineHeight - itemSpacing.y) : ImVec2(ImGui::GetContentRegionAvail().x - lineHeight - itemSpacing.x, size.y), true,
                    ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(ctx.block->name.c_str());
            ImGuiUtils::blockParametersControls(ctx.block, verticalLayout);

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                resetTime();
            }
            ImGui::EndChild();

            ImGui::SetCursorPos(minpos);

            // draw the button(s) that go to the previous block(s).
            const char *nextString = verticalLayout ? "\uf063" : "\uf061";
            ImGui::PushFont(app.fontIconsSolid);
            if (ctx.block->inputs().empty()) {
                auto buttonSize = calcButtonSize(1);
                if (verticalLayout) {
                    ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
                } else {
                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
                }
                ImGuiUtils::DisabledGuard dg;
                ImGui::Button(nextString, buttonSize);
            } else {
                auto buttonSize = calcButtonSize(ctx.block->inputs().size());
                if (verticalLayout) {
                    ImGui::SetCursorPosY(ImGui::GetContentRegionMax().y - buttonSize.y);
                } else {
                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - buttonSize.x);
                }

                ImGui::BeginGroup();
                int id = 1;
                for (auto &in : ctx.block->inputs()) {
                    ImGui::PushID(id++);
                    ImGuiUtils::DisabledGuard dg(in.connections.empty());

                    if (ImGui::Button(nextString, buttonSize)) {
                        ctx.block = in.connections.front()->ports[0]->block;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::PopFont();
                        ImGui::SetTooltip("%s", in.connections.front()->ports[0]->block->name.c_str());
                        ImGui::PushFont(app.fontIconsSolid);
                    }
                    ImGui::PopID();
                    if (verticalLayout) {
                        ImGui::SameLine();
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::PopFont();
        }

        ImGui::EndChild();
    }
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
