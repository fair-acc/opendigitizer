#ifndef OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_SIGNAL_SELECTOR_HPP_

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"

#include "Dialog.hpp"
#include "FilterComboBoxes.hpp"
#include "SelectedLabelsView.hpp"

#include "../Flowgraph.hpp"
#include "../RemoteSignalSources.hpp"

#include <misc/cpp/imgui_stdlib.h>

namespace DigitizerUi {

class SignalSelector {
private:
    std::string            m_windowName = "addSignalPopup";
    QueryFilterElementList m_querySignalFilters;
    SignalList             m_signalList{m_querySignalFilters};

    enum class Category { Domain = 0, DeviceType = 1, DAQ_M = 2, Status = 3, Quantity = 4 };

    struct FilterData {
        Category    category;
        std::string title;
        bool        isActive;

        auto operator<=>(const FilterData& other) const = default;
    };

    struct CategoryData {
        std::string             id;
        std::string             label;
        std::array<ImColor, 2>  color;
        std::vector<FilterData> items;
    };

    SelectedLabelsView<FilterData*> m_selectedFilters;
    FilterComboBoxes<CategoryData>  m_filterCombos;

    bool        m_addRemoteSignal             = false;
    bool        m_addRemoteSignalDialogOpened = false;
    std::string m_addRemoteSignalUri;

    opencmw::service::dns::QueryEntry m_queryFilter;

    static constexpr std::array             colorsForLight = {ImColor(163, 217, 255), ImColor(189, 146, 221), ImColor(229, 99, 153), ImColor(238, 207, 109), ImColor(44, 165, 141)};
    static constexpr std::array             colorsForDark  = {ImColor(164, 130, 19), ImColor(0, 107, 184), ImColor(98, 44, 140), ImColor(157, 27, 81), ImColor(39, 145, 124)};
    static constexpr std::array<ImColor, 2> colorForCategory(Category category) {
        return {
            //
            colorsForLight[static_cast<std::size_t>(category)], //
            colorsForDark[static_cast<std::size_t>(category)]   //
        };
    }

    struct SignalData {
        std::string device;
        std::string frontend;
        std::string _ignore;
        std::string comment;
        std::string signalName;
        std::string subDeviceProperty;
        std::string quantity;
        std::string sampleRate;
        std::string unit;
        std::string accelerator;
        std::string deviceClass;
    };

    std::vector<SignalData> m_signals{
        {"GE01KP02", "scuxl0181", "ESR", "RampedPS", "GE01KP02:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP02", "scuxl0181", "ESR", "RampedPS", "GE01KP02:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP02", "scuxl0181", "ESR", "RampedPS", "GE01KP02:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP02", "scuxl0181", "ESR", "RampedPS", "GE01KP02:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP03", "scuxl0158", "ESR", "RampedPS", "GE01KP03:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP03", "scuxl0158", "ESR", "RampedPS", "GE01KP03:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP03", "scuxl0158", "ESR", "RampedPS", "GE01KP03:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP03", "scuxl0158", "ESR", "RampedPS", "GE01KP03:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP04", "scuxl0181", "ESR", "RampedPS", "GE01KP04:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP04", "scuxl0181", "ESR", "RampedPS", "GE01KP04:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP04", "scuxl0181", "ESR", "RampedPS", "GE01KP04:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP04", "scuxl0181", "ESR", "RampedPS", "GE01KP04:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP05", "scuxl0181", "ESR", "RampedPS", "GE01KP05:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP05", "scuxl0181", "ESR", "RampedPS", "GE01KP05:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP05", "scuxl0181", "ESR", "RampedPS", "GE01KP05:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP05", "scuxl0181", "ESR", "RampedPS", "GE01KP05:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP06", "scuxl0153", "ESR", "RampedPS", "GE01KP06:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP06", "scuxl0153", "ESR", "RampedPS", "GE01KP06:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP06", "scuxl0153", "ESR", "RampedPS", "GE01KP06:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP06", "scuxl0153", "ESR", "RampedPS", "GE01KP06:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP07", "scuxl0211", "ESR", "RampedPS", "GE01KP07:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP07", "scuxl0211", "ESR", "RampedPS", "GE01KP07:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP07", "scuxl0211", "ESR", "RampedPS", "GE01KP07:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP07", "scuxl0211", "ESR", "RampedPS", "GE01KP07:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP08", "scuxl0153", "ESR", "RampedPS", "GE01KP08:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP08", "scuxl0153", "ESR", "RampedPS", "GE01KP08:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP08", "scuxl0153", "ESR", "RampedPS", "GE01KP08:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP08", "scuxl0153", "ESR", "RampedPS", "GE01KP08:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP09", "scuxl0153", "ESR", "RampedPS", "GE01KP09:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP09", "scuxl0153", "ESR", "RampedPS", "GE01KP09:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP09", "scuxl0153", "ESR", "RampedPS", "GE01KP09:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP09", "scuxl0153", "ESR", "RampedPS", "GE01KP09:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP10", "scuxl0153", "ESR", "RampedPS", "GE01KP10:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP10", "scuxl0153", "ESR", "RampedPS", "GE01KP10:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP10", "scuxl0153", "ESR", "RampedPS", "GE01KP10:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP10", "scuxl0153", "ESR", "RampedPS", "GE01KP10:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP17", "scuxl0211", "ESR", "RampedPS", "GE01KP17:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP17", "scuxl0211", "ESR", "RampedPS", "GE01KP17:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP17", "scuxl0211", "ESR", "RampedPS", "GE01KP17:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP17", "scuxl0211", "ESR", "RampedPS", "GE01KP17:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP18", "scuxl0211", "ESR", "RampedPS", "GE01KP18:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP18", "scuxl0211", "ESR", "RampedPS", "GE01KP18:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP18", "scuxl0211", "ESR", "RampedPS", "GE01KP18:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP18", "scuxl0211", "ESR", "RampedPS", "GE01KP18:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP19", "scuxl0211", "ESR", "RampedPS", "GE01KP19:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP19", "scuxl0211", "ESR", "RampedPS", "GE01KP19:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP19", "scuxl0211", "ESR", "RampedPS", "GE01KP19:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP19", "scuxl0211", "ESR", "RampedPS", "GE01KP19:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP20", "scuxl0211", "ESR", "RampedPS", "GE01KP20:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP20", "scuxl0211", "ESR", "RampedPS", "GE01KP20:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP20", "scuxl0211", "ESR", "RampedPS", "GE01KP20:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP20", "scuxl0211", "ESR", "RampedPS", "GE01KP20:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP21", "scuxl0153", "ESR", "RampedPS", "GE01KP21:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP21", "scuxl0153", "ESR", "RampedPS", "GE01KP21:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP21", "scuxl0153", "ESR", "RampedPS", "GE01KP21:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP21", "scuxl0153", "ESR", "RampedPS", "GE01KP21:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP22", "scuxl0181", "ESR", "RampedPS", "GE01KP22:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP22", "scuxl0181", "ESR", "RampedPS", "GE01KP22:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP22", "scuxl0181", "ESR", "RampedPS", "GE01KP22:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP22", "scuxl0181", "ESR", "RampedPS", "GE01KP22:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP23", "scuxl0158", "ESR", "RampedPS", "GE01KP23:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP23", "scuxl0158", "ESR", "RampedPS", "GE01KP23:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP23", "scuxl0158", "ESR", "RampedPS", "GE01KP23:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP23", "scuxl0158", "ESR", "RampedPS", "GE01KP23:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KP24", "scuxl0158", "ESR", "RampedPS", "GE01KP24:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01KP24", "scuxl0158", "ESR", "RampedPS", "GE01KP24:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01KP24", "scuxl0158", "ESR", "RampedPS", "GE01KP24:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01KP24", "scuxl0158", "ESR", "RampedPS", "GE01KP24:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01KS1", "scuxl0170", "ESR", "RampedPS", "GE01KS1:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KS1", "scuxl0170", "ESR", "RampedPS", "GE01KS1:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KS1", "scuxl0170", "ESR", "RampedPS", "GE01KS1:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KS1", "scuxl0170", "ESR", "RampedPS", "GE01KS1:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KS2", "scuxl0170", "ESR", "RampedPS", "GE01KS2:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KS2", "scuxl0170", "ESR", "RampedPS", "GE01KS2:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KS2", "scuxl0170", "ESR", "RampedPS", "GE01KS2:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KS2", "scuxl0170", "ESR", "RampedPS", "GE01KS2:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KS3", "scuxl0170", "ESR", "RampedPS", "GE01KS3:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KS3", "scuxl0170", "ESR", "RampedPS", "GE01KS3:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KS3", "scuxl0170", "ESR", "RampedPS", "GE01KS3:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KS3", "scuxl0170", "ESR", "RampedPS", "GE01KS3:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KS4", "scuxl0170", "ESR", "RampedPS", "GE01KS4:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KS4", "scuxl0170", "ESR", "RampedPS", "GE01KS4:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KS4", "scuxl0170", "ESR", "RampedPS", "GE01KS4:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KS4", "scuxl0170", "ESR", "RampedPS", "GE01KS4:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX1", "scuxl0206", "ESR", "RampedPS", "GE01KX1:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX1", "scuxl0206", "ESR", "RampedPS", "GE01KX1:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX1", "scuxl0206", "ESR", "RampedPS", "GE01KX1:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX1", "scuxl0206", "ESR", "RampedPS", "GE01KX1:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX2", "scuxl0158", "ESR", "RampedPS", "GE01KX2:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX2", "scuxl0158", "ESR", "RampedPS", "GE01KX2:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX2", "scuxl0158", "ESR", "RampedPS", "GE01KX2:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX2", "scuxl0158", "ESR", "RampedPS", "GE01KX2:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX3", "scuxl0206", "ESR", "RampedPS", "GE01KX3:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX3", "scuxl0206", "ESR", "RampedPS", "GE01KX3:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX3", "scuxl0206", "ESR", "RampedPS", "GE01KX3:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX3", "scuxl0206", "ESR", "RampedPS", "GE01KX3:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX4", "scuxl0206", "ESR", "RampedPS", "GE01KX4:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX4", "scuxl0206", "ESR", "RampedPS", "GE01KX4:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX4", "scuxl0206", "ESR", "RampedPS", "GE01KX4:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX4", "scuxl0206", "ESR", "RampedPS", "GE01KX4:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX5", "scuxl0206", "ESR", "RampedPS", "GE01KX5:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX5", "scuxl0206", "ESR", "RampedPS", "GE01KX5:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX5", "scuxl0206", "ESR", "RampedPS", "GE01KX5:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX5", "scuxl0206", "ESR", "RampedPS", "GE01KX5:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KX6", "scuxl0206", "ESR", "RampedPS", "GE01KX6:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KX6", "scuxl0206", "ESR", "RampedPS", "GE01KX6:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KX6", "scuxl0206", "ESR", "RampedPS", "GE01KX6:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KX6", "scuxl0206", "ESR", "RampedPS", "GE01KX6:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KY1", "scuxl0169", "ESR", "RampedPS", "GE01KY1:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KY1", "scuxl0169", "ESR", "RampedPS", "GE01KY1:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KY1", "scuxl0169", "ESR", "RampedPS", "GE01KY1:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KY1", "scuxl0169", "ESR", "RampedPS", "GE01KY1:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KY2", "scuxl0169", "ESR", "RampedPS", "GE01KY2:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KY2", "scuxl0169", "ESR", "RampedPS", "GE01KY2:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KY2", "scuxl0169", "ESR", "RampedPS", "GE01KY2:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KY2", "scuxl0169", "ESR", "RampedPS", "GE01KY2:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KY3", "scuxl0169", "ESR", "RampedPS", "GE01KY3:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KY3", "scuxl0169", "ESR", "RampedPS", "GE01KY3:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KY3", "scuxl0169", "ESR", "RampedPS", "GE01KY3:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KY3", "scuxl0169", "ESR", "RampedPS", "GE01KY3:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01KY4", "scuxl0169", "ESR", "RampedPS", "GE01KY4:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},                 //
        {"GE01KY4", "scuxl0169", "ESR", "RampedPS", "GE01KY4:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},             //
        {"GE01KY4", "scuxl0169", "ESR", "RampedPS", "GE01KY4:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},        //
        {"GE01KY4", "scuxl0169", "ESR", "RampedPS", "GE01KY4:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"},   //
        {"GE01MU0R", "scuxl0198", "ESR", "RampedPS", "GE01MU0R:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedPS"},               //
        {"GE01MU0R", "scuxl0198", "ESR", "RampedPS", "GE01MU0R:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedPS"},           //
        {"GE01MU0R", "scuxl0198", "ESR", "RampedPS", "GE01MU0R:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedPS"},      //
        {"GE01MU0R", "scuxl0198", "ESR", "RampedPS", "GE01MU0R:gap:voltage@Injection1", "gap", "voltage", "Injection1", "V", "ESR", "RampedPS"}, //
        {"GE01MU1", "scuxl0174", "ESR", "RampedHvPS", "GE01MU1:gap:voltage@1Hz", "gap", "voltage", "1Hz", "V", "ESR", "RampedHvPS"},             //
        {"GE01MU1", "scuxl0174", "ESR", "RampedHvPS", "GE01MU1:gap:voltage@10kHz", "gap", "voltage", "10kHz", "V", "ESR", "RampedHvPS"},         //
        {"GE01MU1", "scuxl0174", "ESR", "RampedHvPS", "GE01MU1:gap:frequency@10kHz", "gap", "frequency", "10kHz", "Hz", "ESR", "RampedHvPS"}     //
    };

    using SignalsIndexMap = std::map<std::string, std::vector<SignalData*>>;
    SignalsIndexMap deviceIndex;
    SignalsIndexMap frontendIndex;
    SignalsIndexMap signalNameIndex;
    SignalsIndexMap subDevicePropertyIndex;
    SignalsIndexMap quantityIndex;
    SignalsIndexMap sampleRateIndex;
    SignalsIndexMap unitIndex;
    SignalsIndexMap acceleratorIndex;
    SignalsIndexMap deviceClassIndex;

    std::array<SignalsIndexMap*, 5> categoryIndices{&acceleratorIndex, &deviceClassIndex, &subDevicePropertyIndex, &unitIndex, &quantityIndex};

    // Filtering state
    std::string                           m_shownSearchString;
    std::string                           m_searchString;
    std::vector<SignalData*>              m_filteredItems;

    SignalsIndexMap*                      m_mainFilterCategoryIndex = nullptr;
    std::vector<std::vector<SignalData*>> m_signalsToProcess;
    std::size_t                           m_nextItemToFilter = 0;
    std::vector<FilterData*>              m_filters;

    void startSearching(const std::string& _searchString, std::vector<FilterData*> _filters) {
        m_filteredItems.clear();
        m_searchString = _searchString;
        m_filters      = std::move(_filters);
        m_signalsToProcess.clear();

        if (!m_filters.empty()) {
            std::ranges::sort(m_filters, [](const auto* left, const auto* right) { return *left < *right; });
            auto mainCategory         = m_filters.front()->category;
            m_mainFilterCategoryIndex = categoryIndices[static_cast<std::size_t>(mainCategory)];

            for (const auto& filter : m_filters) {
                if (filter->category != mainCategory) {
                    break;
                }
                m_signalsToProcess.push_back((*m_mainFilterCategoryIndex)[filter->title]);
            }
        } else {
            m_mainFilterCategoryIndex = nullptr;
        }
        m_nextItemToFilter = 0UZ;
    }

    bool loadMoreItems() {
        if (m_mainFilterCategoryIndex) {
            if (m_nextItemToFilter >= m_mainFilterCategoryIndex->size()) {
                return false;
            }

        } else {
            // We list everything that matches the search string,
            // no extra filters have been defined
            if (m_nextItemToFilter >= m_signals.size()) {
                return false;
            }

            if (m_signals[m_nextItemToFilter].signalName.find(m_searchString) != std::string::npos || //
                m_signals[m_nextItemToFilter].comment.find(m_searchString) != std::string::npos) {
                m_filteredItems.push_back(&m_signals[m_nextItemToFilter]);
            }

            m_nextItemToFilter++;
            return true;
        }
    }

    void buildIndex() {
        m_mainFilterCategoryIndex = nullptr;
        m_nextItemToFilter        = 0UZ;
        m_searchString.clear();
        m_filters.clear();
        m_filteredItems.clear();

        for (SignalData& signal : m_signals) {
            auto* signalPtr = std::addressof(signal);
            deviceIndex[signal.device].push_back(signalPtr);
            frontendIndex[signal.frontend].push_back(signalPtr);
            signalNameIndex[signal.signalName].push_back(signalPtr);
            subDevicePropertyIndex[signal.subDeviceProperty].push_back(signalPtr);
            quantityIndex[signal.quantity].push_back(signalPtr);
            sampleRateIndex[signal.sampleRate].push_back(signalPtr);
            unitIndex[signal.unit].push_back(signalPtr);
            acceleratorIndex[signal.accelerator].push_back(signalPtr);
            deviceClassIndex[signal.deviceClass].push_back(signalPtr);
        }

        auto itemsForIndex = [this](Category category) {
            const auto&             index = *categoryIndices[static_cast<std::size_t>(category)];
            std::vector<FilterData> result;
            result.resize(index.size());
            std::ranges::transform(index, result.begin(), [category](const auto& kvp) { return FilterData{category, kvp.first, false}; });
            return result;
        };

        m_filterCombos.setData({
            {"##comboDomain", "Domain", colorForCategory(Category::Domain), itemsForIndex(Category::Domain)},                //
            {"##comboDeviceType", "Dev. type", colorForCategory(Category::DeviceType), itemsForIndex(Category::DeviceType)}, //
            {"##comboDAQ", "DAQ-M.", colorForCategory(Category::DAQ_M), {}},                                                 //
            {"##comboStatus", "Status", colorForCategory(Category::Status), {}},                                             //
            {"##comboQuantity", "Quantity", colorForCategory(Category::Quantity), itemsForIndex(Category::Quantity)}         //
        });
    }

public:
    SignalSelector() { buildIndex(); }

    void open() { ImGui::OpenPopup(m_windowName.c_str()); }

    void drawRemoteSignalsInput(FlowGraph* fg) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("URI:");
        ImGui::SameLine();
        if (m_addRemoteSignalDialogOpened) {
            ImGui::SetKeyboardFocusHere();
            m_addRemoteSignalDialogOpened = false;
        }
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::InputText("##uri", &m_addRemoteSignalUri);

        if (ImGui::Button("Add")) {
            m_addRemoteSignal = false;
            fg->addRemoteSource(m_addRemoteSignalUri);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            m_addRemoteSignal = false;
        }
    }

    void drawSourceBlocksChooser(FlowGraph* fg) {
        struct Cat {
            std::string             name;
            std::vector<BlockType*> types;
        };
        static std::vector<Cat> cats;
        cats.clear();
        cats.push_back({"Remote signals", {}});
        for (const auto& t : BlockType::registry().types()) {
            if (t.second->isSource() && !t.second->category.empty()) {
                auto it = std::find_if(cats.begin(), cats.end(), [&](const auto& c) { return c.name == t.second->category; });
                if (it == cats.end()) {
                    cats.push_back({t.second->category, {t.second.get()}});
                } else {
                    it->types.push_back(t.second.get());
                }
            }
        }
    }

    void drawSignalSelector(FlowGraph* fg) {
        m_querySignalFilters.drawFilters();

        bool filtersChanged = false;

        if (auto comboSelectedItem = m_filterCombos.draw()) {
            auto* item      = *comboSelectedItem;
            bool  wasActive = item->isActive;
            item->isActive  = !wasActive;

            if (wasActive) {
                m_selectedFilters.removeLabel(item);
            } else {
                m_selectedFilters.addLabel({item->title, item, colorForCategory(item->category)});
            }

            filtersChanged = true;
        }

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

        ImGui::InputText("##textFilter", &m_shownSearchString);
        if (m_shownSearchString != m_searchString) {
            filtersChanged = true;
        }

        auto contentWidth          = ImGui::GetContentRegionAvail().x;
        auto remainingContentWidth = contentWidth;
        bool first                 = true;

        if (auto removedLabel = m_selectedFilters.draw()) {
            (*removedLabel)->isActive = false;
            filtersChanged            = true;
        }

        if (filtersChanged) {
            std::vector<FilterData*> filters;
            filters.resize(m_selectedFilters.labels().size());
            std::ranges::transform(m_selectedFilters.labels(), filters.begin(), [](const auto& filter) { return filter.data; });
            startSearching(m_shownSearchString, filters);
        }

        if (filtersChanged) // TODO remove this condition
        while( // TODO - remove while
            loadMoreItems()
        );
        fmt::print("--vvvv---------------------- | search string {} | {}\n", m_searchString, m_shownSearchString);
        for (const auto* signal : m_filteredItems) {
            fmt::print("    {} {}\n", signal->signalName, signal->comment);
        }
        fmt::print("--^^^^----------------------\n");

        // if (ImGui::BeginCombo("##baseTypeCombo", DataType::name(context.block->datatype()).data())) {
        //     for (auto e : typeNames) {
        //         if (ImGui::Selectable(e.c_str(), e == typeName)) {
        //             context.block->flowGraph()->changeBlockType(context.block, DataType::fromString(e));
        //         }
        //         if (e == typeName) {
        //             ImGui::SetItemDefaultFocus();
        //         }
        //     }
        //     ImGui::EndCombo();
        // }

        float windowWidth = ImGui::GetWindowWidth();
        float buttonPosX  = windowWidth - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Add Filter").x;
        ImGui::SetCursorPosX(buttonPosX);
        if (ImGui::Button("Add Filter")) {
            m_querySignalFilters.emplace_back(QueryFilterElement{m_querySignalFilters});
        }
        ImGui::Separator();
        ImGui::SetNextWindowSize(ImGui::GetContentRegionAvail(), ImGuiCond_Once);
        IMW::Child signals("Signals", ImVec2(0, 0), 0, 0);

        m_signalList.addRemoteSignalCallback = [fg](const opencmw::service::dns::Entry& entry) {
            const auto uri = opencmw::URI<>::UriFactory().scheme(entry.protocol).hostName(entry.hostname).port(static_cast<uint16_t>(entry.port)).path(entry.service_name).addQueryParameter("channelNameFilter", entry.signal_name).build();
            fg->addRemoteSource(uri.str());
        };
        m_signalList.drawElements();

        // float refreshButtonPosX = ImGui::GetWindowWidth() - ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Refresh").x;
        // float refreshButtonPosY = ImGui::GetWindowHeight() - ImGui::GetStyle().ItemSpacing.y - ImGui::GetStyle().FramePadding.y - ImGui::CalcTextSize("Refresh").y;
        // ImGui::SetCursorPos({refreshButtonPosX, refreshButtonPosY});
        // if (ImGui::Button("Refresh")) {
        //     m_signalList.update();
        // }
    }

    void draw(FlowGraph* fg) {
        auto parentSize = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(parentSize - ImVec2(32, 32), ImGuiCond_Once);
        if (auto menu = IMW::ModalPopup(m_windowName.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            float windowWidth = ImGui::GetWindowWidth();
            float buttonPosX  = windowWidth - 2 * ImGui::GetStyle().ItemSpacing.x - ImGui::GetStyle().FramePadding.x - ImGui::CalcTextSize("Close").x;
            ImGui::SetCursorPosX(buttonPosX);
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                return;
            }

            // fg->addBlock(sel->createBlock({}));

            drawSignalSelector(fg);

            ImGui::NewLine();
            ImGui::NewLine();
#if 0
            static BlockType* sel = nullptr;
            if (auto child = IMW::ChildWithId(1, ImVec2{0, ImGui::GetContentRegionAvail().y - 50}, 0, 0)) {
                cats.push_back({"Query signals", {}});

                for (const auto& c : cats) {
                    const bool isRemote = c.name == "Remote signals";
                    if (ImGui::TreeNode(c.name.c_str())) {
                        for (auto* t : c.types) {
                            if (ImGui::Selectable(t->label.c_str(), sel == t, ImGuiSelectableFlags_DontClosePopups)) {
                                sel = t;
                            }
                        }

                        if (c.name == "Query signals") {
                        }

                        if (isRemote) {
                            if (!m_addRemoteSignal) {
                                if (ImGui::Button("Add remote signal")) {
                                    m_addRemoteSignal             = true;
                                    m_addRemoteSignalDialogOpened = true;
                                    m_addRemoteSignalUri          = {};
                                }
                            } else {
                            }
                        }
                        ImGui::TreePop();
                    } else if (isRemote) {
                        m_addRemoteSignal = false;
                    }
                }
            }

            if (components::DialogButtons(sel) == components::DialogButton::Ok) {
                fg->addBlock(sel->createBlock({}));
            }
#endif
        }
    }
};
} // namespace DigitizerUi

#endif
