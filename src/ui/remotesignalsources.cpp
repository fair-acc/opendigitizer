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
QueryFilterElement::QueryFilterElement(QueryFilterElementList &list)
    : list(list) {
    static int counter = 0;
    _keyIdentifier     = "##queryKey_" + std::to_string(counter);
    _valueIdentifier   = "##queryValue_" + std::to_string(counter);
    _buttonIdentifier  = "X##filterDelete_" + std::to_string(counter++);
    filterText.resize(255);
}

void QueryFilterElementList::triggerChange() {
    std::for_each(onChange.begin(), onChange.end(), [](auto &f) { f(); });
}
void QueryFilterElementList::drawFilters() {
    marked_for_delete = end();
    std::for_each(begin(), end(), [](auto &f) { f.drawFilterLine(); });
    if (marked_for_delete != end()) {
        erase(marked_for_delete);
        triggerChange();
    }
}

SignalList::SignalList(QueryFilterElementList &filters)
    : filters(filters) {
    filters.onChange.emplace_back(myOnChange);
    update();
}
SignalList::~SignalList() {
    auto it = std::find_if(filters.onChange.begin(), filters.onChange.end(), [this](auto e) { return e == myOnChange; });
    assert(it != filters.onChange.end());
    filters.onChange.erase(it);

    // TODO test this
}
void SignalList::update() {
    opencmw::service::dns::QueryEntry queryEntry;

    refl::util::for_each(refl::reflect(queryEntry).members, [&](auto member) {
        // TODO maybe pick the last instead of the first

        auto it = std::find_if(filters.begin(), filters.end(), [&member](const auto &f) {
            return f.selectedField() == refl::descriptor::get_display_name(member);
        });
        // we pick the first
        auto &strValue = it->filterText;
        if (it != filters.end() && strValue != "") {
            if constexpr (std::is_integral_v<std::remove_cvref_t<decltype(member(queryEntry))>>)
                member(queryEntry) = std::atoi(strValue.c_str());
            else if constexpr (std::is_floating_point_v<std::remove_cvref_t<decltype(member(queryEntry))>>)
                member(queryEntry) = std::atof(strValue.c_str());
            else
                member(queryEntry) = strValue;
        }
    });
    try {
        std::unique_lock l{ signalsMutex };
        dnsClient.querySignalsAsync([this](const std::vector<opencmw::service::dns::Entry> &entries) {
            signals = entries;
        },
                queryEntry);
    } catch (const std::exception &e) {
        std::cerr << "Error loading signals: " << e.what() << std::endl;
    }
}
void SignalList::drawElements() {
    ImGui::BeginTable("Signals", refl::reflect<opencmw::service::dns::QueryEntry>().members.size + 1, ImGuiTableFlags_BordersInnerV);

    ImGui::TableHeader("SignalsHeader");
    refl::util::for_each(refl::reflect<opencmw::service::dns::QueryEntry>().members, [](auto m) {
        ImGui::TableSetupColumn(static_cast<const char *>(m.name));
    });
    ImGui::TableSetupColumn("Add Signal");
    ImGui::TableHeadersRow();
    {
        std::unique_lock l{ signalsMutex };
        std::for_each(signals.begin(), signals.end(), [this, idx = 0](const auto &e) mutable { drawElement(e, idx++); });
    }
    ImGui::EndTable();
}
void SignalList::drawElement(const opencmw::service::dns::Entry &entry, int idx) {
    ImGui::TableNextRow();
    refl::util::for_each(refl::reflect<opencmw::service::dns::QueryEntry>().members, [&entry](auto m) {
        auto value = m(entry);
        ImGui::TableNextColumn();
        if constexpr (std::is_integral_v<decltype(value)> || std::is_floating_point_v<decltype(value)>) {
            ImGui::TextUnformatted(std::to_string(m(entry)).c_str());
        } else {
            ImGui::TextUnformatted(value.c_str());
        }
    });
    ImGui::TableNextColumn();
    if (ImGui::Button(("+##" + std::to_string(idx)).c_str())) {
        if (addRemoteSignalCallback) {
            addRemoteSignalCallback(entry);
        }
    }
}
