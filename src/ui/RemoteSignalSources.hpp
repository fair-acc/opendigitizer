#ifndef OPENDIGITIZER_REMOTESIGNALSOURCES_H
#define OPENDIGITIZER_REMOTESIGNALSOURCES_H

#include <list>
#include <refl.hpp>

#include "common/ImguiWrap.hpp"

#include "RestClient.hpp"
#include "services/dns_client.hpp"
#include "services/dns_types.hpp"
#include "settings.hpp"

class QueryFilterElementList;

/*     Draws a Combo box, to choose the field to filter, the filter keyword and delete button for it     */
struct QueryFilterElement {
    QueryFilterElementList&     list;
    static constexpr std::array field_names = [] {
        constexpr auto                descriptor = refl::reflect<opencmw::service::dns::QueryEntry>();
        constexpr auto                size       = descriptor.members.size;
        std::array<const char*, size> arr;
        std::size_t                   index = 0;
        refl::util::for_each(descriptor.members, [&](auto member) { arr[index++] = member.name.c_str(); });
        return arr;
    }();
    std::string _keyIdentifier;
    std::string _valueIdentifier;
    std::size_t _selectedIndex{1};
    std::string _buttonIdentifier;
    std::string filterText;

    std::string selectedField() const { return field_names[_selectedIndex]; }

    QueryFilterElement(QueryFilterElementList& list);

    QueryFilterElement(const QueryFilterElement& other) : list(other.list), _keyIdentifier(other._keyIdentifier), _valueIdentifier(other._valueIdentifier), _selectedIndex(other._selectedIndex), _buttonIdentifier(other._buttonIdentifier) {}
    QueryFilterElement& operator=(const QueryFilterElement& other);

    bool operator==(const QueryFilterElement& rhs) const { return &list == &rhs.list && _keyIdentifier == rhs._keyIdentifier && _valueIdentifier == rhs._valueIdentifier; };
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

        void operator()() { function(); }
        bool operator==(const Hook& other) const { return id == other.id; }
    };
    std::vector<Hook> onChange;
    void              triggerChange();
    void              pop(QueryFilterElement& element) {
        // TODO: this was wrong
        // auto toDelete     = std::remove(begin(), end(), element);
        marked_for_delete = std::remove(begin(), end(), element);
    }
    void add() {
        // we don't trigger
    }

    void drawFilters();

protected:
    iterator marked_for_delete;
};

class SignalList {
    Digitizer::Settings            settings;
    opencmw::client::ClientContext clientContext = []() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY)));
        return opencmw::client::ClientContext{std::move(clients)};
    }();
    opencmw::service::dns::DnsClient dnsClient{clientContext, settings.serviceUrl().path("/dns").build()};

    QueryFilterElementList&      filters;
    QueryFilterElementList::Hook myOnChange{[this]() { this->update(); }};

    std::vector<opencmw::service::dns::Entry> signals;
    std::mutex                                signalsMutex;

public:
    std::function<void(opencmw::service::dns::Entry)> addRemoteSignalCallback;
    std::function<void(const std::vector<opencmw::service::dns::Entry>&)> updateSignalsCallback;

    explicit SignalList(QueryFilterElementList& filters);
    ~SignalList();

    void update();

    void drawElements();
    void drawElement(const opencmw::service::dns::Entry& entry, int idx);
};

#endif // OPENDIGITIZER_REMOTESIGNALSOURCES_H
