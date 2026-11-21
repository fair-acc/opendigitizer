#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_

#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <refl.hpp>

#include <ClientCommon.hpp>
#include <RestClient.hpp>

#include <gnuradio-4.0/Message.hpp>

#include "../common/Imw.hpp"
#include <misc/cpp/imgui_stdlib.h>

#include "FilterComboBoxes.hpp"
#include "SelectedLabelsView.hpp"

#include "services/dns_client.hpp"
#include "services/dns_types.hpp"
#include "settings.hpp"

namespace gr::od {

struct UiGraphModel;

struct QueryFilter {
    static constexpr std::array kFieldNames = [] {
        constexpr auto                                    kDescriptor = ::refl::reflect<opencmw::service::dns::QueryEntry>();
        std::array<const char*, kDescriptor.members.size> names;
        std::size_t                                       index = 0;
        ::refl::util::for_each(kDescriptor.members, [&](auto member) { names[index++] = member.name.c_str(); });
        return names;
    }();

    std::string keyId;
    std::string valueId;
    std::string deleteButtonId;
    std::size_t selectedIndex = 1;
    std::string filterText;

    [[nodiscard]] std::string selectedField() const { return kFieldNames[selectedIndex]; }
};

struct QueryFilters {
    std::vector<QueryFilter> elements;
    std::function<void()>    onChange;

    void draw(); // a combo box for the field, the filter text and a delete button per filter
    void changed() {
        if (onChange) {
            onChange();
        }
    }
};

struct SignalList {
    std::function<void(const std::vector<opencmw::service::dns::Entry>&)> updateSignalsCallback;

    Digitizer::Settings&           _settings      = Digitizer::Settings::instance();
    opencmw::client::ClientContext _clientContext = [this]() {
        std::vector<std::unique_ptr<opencmw::client::ClientBase>> clients;
        clients.emplace_back(std::make_unique<opencmw::client::RestClient>(opencmw::client::DefaultContentTypeHeader(opencmw::MIME::BINARY), opencmw::client::VerifyServerCertificates(_settings.checkCertificates)));
        return opencmw::client::ClientContext{std::move(clients)};
    }();
    opencmw::service::dns::DnsClient _dnsClient{_clientContext, _settings.serviceUrl().path("/dns").build()};

    QueryFilters& _filters;

    std::vector<opencmw::service::dns::Entry> _signals;
    std::mutex                                _signalsMutex;

    explicit SignalList(QueryFilters& queryFilters) : _filters(queryFilters) {
        _filters.onChange = [this] { update(); };
    }
    ~SignalList() { _filters.onChange = {}; }
    SignalList(const SignalList&)            = delete;
    SignalList& operator=(const SignalList&) = delete;

    void update();
};

struct SignalData {
    std::string device;
    std::string frontend;
    std::string comment;
    std::string signalName;
    std::string subDeviceProperty;
    std::string quantity;
    std::string sampleRate;
    std::string unit;
    std::string accelerator;
    std::string deviceClass;
    std::string hostname;
    std::string protocol;
    std::string serviceName;
    int         port = -1;

    std::string uri() const { return opencmw::URI<>::UriFactory().scheme(protocol).hostName(hostname).port(static_cast<uint16_t>(port)).path(serviceName).addQueryParameter("channelNameFilter", signalName).build().str(); }
};

struct SignalSelector {
    using SignalsIndexMap = std::map<std::string, std::vector<SignalData*>>;

    enum class Category { Domain = 0, DeviceType = 1, DaqM = 2, Status = 3, Quantity = 4 };

    static constexpr std::size_t kCategoriesCount = 5;
    static constexpr std::array  kColorsForLight  = {ImColor(163, 217, 255), ImColor(189, 146, 221), ImColor(229, 99, 153), ImColor(238, 207, 109), ImColor(44, 165, 141)};
    static constexpr std::array  kColorsForDark   = {ImColor(164, 130, 19), ImColor(0, 107, 184), ImColor(98, 44, 140), ImColor(157, 27, 81), ImColor(39, 145, 124)};

    struct CategorySearch {
        std::deque<std::vector<SignalData*>> signalsToProcess;
        std::vector<FilterTag*>              filters;
    };

    std::string   _windowName = "Add Device Signals";
    QueryFilters  _querySignalFilters;
    SignalList    _signalList{_querySignalFilters};
    UiGraphModel* _graphModel = nullptr;

    SelectedLabelsView _selectedFilters;
    FilterComboBoxes   _filterCombos;

    bool _forceRefresh = false;

    std::vector<SignalData> _signals;

    SignalsIndexMap _deviceIndex;
    SignalsIndexMap _frontendIndex;
    SignalsIndexMap _signalNameIndex;
    SignalsIndexMap _subDevicePropertyIndex;
    SignalsIndexMap _quantityIndex;
    SignalsIndexMap _sampleRateIndex;
    SignalsIndexMap _unitIndex;
    SignalsIndexMap _acceleratorIndex;
    SignalsIndexMap _deviceClassIndex;

    std::array<SignalsIndexMap*, kCategoriesCount> _categoryIndices{&_acceleratorIndex, &_deviceClassIndex, &_frontendIndex, &_sampleRateIndex, &_quantityIndex};

    std::string              _shownSearchString;
    std::string              _searchString;
    std::vector<SignalData*> _filteredItems;

    std::optional<CategorySearch> _categorySearch;
    std::size_t                   _nextItemToFilter = 0UZ;

    explicit SignalSelector(UiGraphModel& graphModel);
    SignalSelector(const SignalSelector&)            = delete;
    SignalSelector& operator=(const SignalSelector&) = delete;

    friend constexpr std::size_t            operator+(Category category) { return static_cast<std::size_t>(category); }
    static constexpr std::array<ImColor, 2> colorForCategory(std::size_t category) { return {kColorsForLight[category], kColorsForDark[category]}; }

    static const std::string& categoryFieldForSignal(std::size_t category, const SignalData& signal) {
        switch (static_cast<Category>(category)) {
        case Category::Domain: return signal.accelerator;
        case Category::DaqM: return signal.frontend;
        case Category::Quantity: return signal.quantity;
        case Category::Status: return signal.sampleRate;
        case Category::DeviceType: return signal.deviceClass;
        default: throw gr::exception(std::format("unknown category {} for Signal device: {}", gr::meta::enumName(static_cast<Category>(category)).value_or("?"), signal.device));
        }
    }

    void startSearching(const std::string& searchString, std::vector<FilterTag*> filters);

    bool signalMatchesSearchString(const SignalData& signal) const;

    bool signalMatchesActiveFilters(const SignalData& signal) const;

    bool loadMoreItems();

    void buildIndex();

    [[nodiscard]] std::vector<SignalData> drawSignalSelector();

    void open() { ImGui::OpenPopup(_windowName.c_str()); }
    void close() { ImGui::CloseCurrentPopup(); }

    void drawElement(const SignalData& entry, std::size_t idx, const ImGuiSelectionBasicStorage& selection);

    [[nodiscard]] std::vector<SignalData> drawAndReturnSelected();
};
} // namespace gr::od

#endif
