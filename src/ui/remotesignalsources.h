#ifndef OPENDIGITIZER_REMOTESIGNALSOURCES_H
#define OPENDIGITIZER_REMOTESIGNALSOURCES_H


/*     Draws a Combo box, to choose the field to filter, the filter keyword and delete button for it     */
//#include "ClientContext.hpp"
#include <services/dns_client.hpp>
#include "imgui.h"
#include "RestClient.hpp"
#include "services/dns_types.hpp"
#include <refl.hpp>
#include <list>

struct QueryFilterElementList;
struct QueryFilterElement {
    QueryFilterElementList     &list;
    static constexpr std::array field_names = [] {
        constexpr auto                 descriptor = refl::reflect<opencmw::service::dns::QueryEntry>();
        constexpr auto                 size       = descriptor.members.size;
        std::array<const char *, size> arr;
        int                            index = 0;
        refl::util::for_each(descriptor.members, [&](auto member) {
            arr[index++] = member.name.c_str();
        });
        return arr;
    }();
    int         _selectedIndex{ 1 };
    std::string _keyIdentifier;
    std::string _valueIdentifier;
    std::string _buttonIdentifier;
    std::string filterText;

    std::string selectedField() const {
        return field_names[_selectedIndex];
    }

    QueryFilterElement(QueryFilterElementList &list)
        : list(list) {
        static int counter = 0;
        _keyIdentifier     = "##queryKey_" + std::to_string(counter);
        _valueIdentifier   = "##queryValue_" + std::to_string(counter);
        _buttonIdentifier  = "X##filterDelete_" + std::to_string(counter++);
        filterText.resize(255);
    }

    QueryFilterElement(const QueryFilterElement &other)
        : list(other.list), _keyIdentifier(other._keyIdentifier), _valueIdentifier(other._valueIdentifier), _selectedIndex(other._selectedIndex), _buttonIdentifier(other._buttonIdentifier) {}
    QueryFilterElement &operator=(const QueryFilterElement &other);

    bool operator==(const QueryFilterElement &rhs) const {
        return &list == &rhs.list
            && _keyIdentifier == rhs._keyIdentifier
            && _valueIdentifier == rhs._valueIdentifier;
    };
    void drawFilterLine();
};
class QueryFilterElementList : public std::list<QueryFilterElement> {
public:
    struct Hook {
        int                   id;
        std::function<void()> function;

        Hook(std::function<void()> func) {
            static int _id = 0;
            id             = _id++;
            function       = func;
        }

        void operator()() {
            function();
        }
        bool operator==(const Hook &other) const { return id == other.id; }
    };
    std::vector<Hook> onChange;
    void              triggerChange();
    void              pop(QueryFilterElement &element) {
        auto toDelete     = std::remove(begin(), end(), element);
        marked_for_delete = std::remove(begin(), end(), element);
    }
    void add() {
        // we don't trigger
    }

    void drawFilters() {
        marked_for_delete = end();
        std::for_each(begin(), end(), [](auto &f) { f.drawFilterLine(); });
        if (marked_for_delete != end()) {
            erase(marked_for_delete);
            triggerChange();
        }
    }

protected:
    iterator marked_for_delete;
};

class SignalList {
    opencmw::client::ClientContext clientContext = []() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY)));
        return opencmw::client::ClientContext{ std::move(clients) };
    }();
    opencmw::service::dns::DnsClient dnsClient{ clientContext, opencmw::URI<>{ "http://localhost:8055/dns" } };

    QueryFilterElementList          &filters;
    QueryFilterElementList::Hook     myOnChange{ [this]() { this->update(); } };

public:
    std::vector<opencmw::service::dns::Entry> signals;

    SignalList(QueryFilterElementList &filters)
        : filters(filters) {
        filters.onChange.emplace_back(myOnChange);
        update();
    }
    ~SignalList() {
        auto it = std::find_if(filters.onChange.begin(), filters.onChange.end(), [this](auto e) { return e == myOnChange; });
        assert(it != filters.onChange.end());
        filters.onChange.erase(it);

        // TODO test this
    }

    void update() {
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
        signals = dnsClient.querySignals(queryEntry);
    }

    void drawElements() {
        ImGui::BeginTable("Signals", refl::reflect<opencmw::service::dns::QueryEntry>().members.size + 1, ImGuiTableFlags_BordersInnerV);

        ImGui::TableHeader("SignalsHeader");
        refl::util::for_each(refl::reflect<opencmw::service::dns::QueryEntry>().members, [](auto m) {
            ImGui::TableSetupColumn(static_cast<const char *>(m.name));
        });
        ImGui::TableSetupColumn("Add Signal");
        ImGui::TableHeadersRow();
        std::for_each(signals.begin(), signals.end(), [this, idx = 0](const auto &e) mutable { drawElement(e, idx++); });
        ImGui::EndTable();
    }
    void drawElement(const opencmw::service::dns::Entry &entry, int idx) {
        ImGui::TableNextRow();
        refl::util::for_each(refl::reflect<opencmw::service::dns::QueryEntry>().members, [&entry](auto m) {
            auto value = m(entry);
            ImGui::TableNextColumn();
            if constexpr (std::is_integral_v<decltype(value)> || std::is_floating_point_v<decltype(value)>) {
                ImGui::Text(std::to_string(m(entry)).c_str());
            } else {
                ImGui::Text(value.c_str());
            }
        });
        ImGui::TableNextColumn();
        if (ImGui::Button(("+##" + std::to_string(idx)).c_str())) {
            std::cout << "foobar" << std::endl;
        }
    }
};

#endif // OPENDIGITIZER_REMOTESIGNALSOURCES_H
