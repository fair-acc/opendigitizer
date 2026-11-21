#include "../include/fair/DeviceNameHelper.hpp"
#include <boost/ut.hpp>

#include <format>
#include <numeric>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace fair {

void printOut(const DeviceInfo& deviceInfo) {                                                                    //
    std::println("device name: {:10} -> machine: {:10}, section: {:15}, seqID: {:>3}, function: {:30}, pos: {}", //
        deviceInfo.name, deviceInfo.location, deviceInfo.section, deviceInfo.sequence, deviceInfo.deviceFunction, deviceInfo.devicePosition);
}

void printDeviceInfo(std::string_view deviceName) { printOut(getDeviceInfo(deviceName)); }

struct SignalInfo {
    std::string deviceName;
    std::string digitizerDevice;
    std::string fullSignalName;
    std::string quantity;
    std::string sampleFrequency;
    std::string siUnit;
    std::string accelerator;
    std::string deviceFunction;
};

inline std::size_t estimateTotalMemoryUsage(const std::vector<SignalInfo>& table) {
    auto stringMemoryUsage = [](const std::string& str) { return str.capacity(); };

    return sizeof(SignalInfo) * table.size() + std::accumulate(table.begin(), table.end(), std::size_t{0}, [stringMemoryUsage](std::size_t sum, const SignalInfo& info) { return sum + stringMemoryUsage(info.deviceName) + stringMemoryUsage(info.digitizerDevice) + stringMemoryUsage(info.fullSignalName) + stringMemoryUsage(info.quantity) + stringMemoryUsage(info.sampleFrequency) + stringMemoryUsage(info.siUnit) + stringMemoryUsage(info.accelerator) + stringMemoryUsage(info.deviceFunction); });
}

inline std::size_t estimateMinimalMemoryUsage(const std::vector<SignalInfo>& table) {
    auto stringMemoryUsage = [](const std::string& str) { return str.size(); };

    return std::accumulate(table.begin(), table.end(), std::size_t{0}, [stringMemoryUsage](std::size_t sum, const SignalInfo& info) { return sum + stringMemoryUsage(info.deviceName) + stringMemoryUsage(info.digitizerDevice) + stringMemoryUsage(info.fullSignalName) + stringMemoryUsage(info.quantity) + stringMemoryUsage(info.sampleFrequency) + stringMemoryUsage(info.siUnit) + stringMemoryUsage(info.accelerator) + stringMemoryUsage(info.deviceFunction); });
}

constexpr std::array<std::string_view, 2>  kSubDeviceProperties = {"gap", "generator"};
constexpr std::array<std::string_view, 4>  kMagnetQuantities    = {"voltage", "current", "voltage_diff", "current_diff"};
constexpr std::array<std::string_view, 3>  kCommonQuantities    = {"temperature", "cpu_load", "n_user"};
constexpr std::array<std::string_view, 3>  kRfQuantities        = {"frequency", "phase", "amplitude"};
constexpr std::array<std::string_view, 15> kSampleFrequencies   = {"1Hz", "10Hz", "25Hz", "100Hz", "1kHz", "10kHz", "Injection1", "Injection2", "Injection3", "Injection4", "RampStart", "Extraction", "Diag1", "Diag2", "Diag3"};

inline std::vector<SignalInfo> generateSignalTable() {
    std::vector<SignalInfo> table;

    auto containsAny = [](std::string_view str, const auto& keywords) { //
        return std::ranges::any_of(keywords, [str](std::string_view keyword) { return str.find(keyword) != std::string_view::npos; });
    };

    for (const auto& name : fair::kTestDeviceNames) {
        auto deviceInfo = fair::getDeviceInfo(name);

        auto addEntry = [&](std::string_view subDevice, std::string_view quantity, std::string_view frequency, std::string_view unit) {
            table.emplace_back(SignalInfo{
                //
                std::string(deviceInfo.name),                                                                                            //
                std::format("{}.{}.test-domain.io", deviceInfo.location, deviceInfo.name),                                               //
                std::format("{}:{}{}@{}", deviceInfo.name, subDevice.empty() ? "" : std::format("{}:", subDevice), quantity, frequency), //
                std::string(quantity), std::string(frequency), std::string(unit),                                                        //
                std::string(deviceInfo.location), std::string(deviceInfo.deviceFunction)                                                 //
            });
        };

        for (const auto& quantity : kCommonQuantities) {
            addEntry("IO", quantity, "1Hz", quantity == "temperature" ? "°C" : (quantity == "cpu_load" ? "%" : "#"));
        }

        if (containsAny(deviceInfo.deviceFunction, std::array{"magnet", "dipole", "quad", "sextupole", "octopole", "multipole", "pole", "solenoid", "toroid", "septum", "steerer", "source", "tube", "voltage", "power supply"})) {
            for (const auto& quantity : kMagnetQuantities) {
                for (const auto& frequency : kSampleFrequencies) {
                    addEntry("", quantity, frequency, quantity.starts_with("voltage") ? "V" : "A");
                }
            }
        }

        if (containsAny(deviceInfo.deviceFunction, std::array{"RF", "cavity", "Alvarez"})) {
            for (const auto& subDevice : kSubDeviceProperties) {
                for (const auto& quantity : kRfQuantities) {
                    for (const auto& frequency : kSampleFrequencies) {
                        addEntry(subDevice, quantity, frequency, quantity == "frequency" ? "Hz" : (quantity == "phase" ? "degree" : "V"));
                    }
                }
            }
        }
    }

    return table;
}

void printSignalTable(const std::vector<SignalInfo>& table) {
    constexpr int kDeviceNameWidth      = 12;
    constexpr int kDigitizerDeviceWidth = 35;
    constexpr int kFullSignalNameWidth  = 70;
    constexpr int kQuantityWidth        = 15;
    constexpr int kSampleFrequencyWidth = 15;
    constexpr int kSiUnitWidth          = 10;
    constexpr int kAcceleratorWidth     = 10;
    constexpr int kDeviceFunctionWidth  = 30;

    std::print("{:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}}\n", //
        "device", kDeviceNameWidth,                                         //
        "digitizer_device", kDigitizerDeviceWidth,                          //
        "full_signal_name", kFullSignalNameWidth,                           //
        "quantity", kQuantityWidth,                                         //
        "sample_frequency", kSampleFrequencyWidth,                          //
        "SI unit", kSiUnitWidth,                                            //
        "accelerator", kAcceleratorWidth,                                   //
        "deviceFunction", kDeviceFunctionWidth);                            //

    for (const auto& entry : table) {
        std::print("{:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}}\n", //
            entry.deviceName, kDeviceNameWidth,                                 //
            entry.digitizerDevice, kDigitizerDeviceWidth,                       //
            entry.fullSignalName, kFullSignalNameWidth,                         //
            entry.quantity, kQuantityWidth,                                     //
            entry.sampleFrequency, kSampleFrequencyWidth,                       //
            entry.siUnit, kSiUnitWidth,                                         //
            entry.accelerator, kAcceleratorWidth,                               //
            entry.deviceFunction, kDeviceFunctionWidth);
    }
    std::print("number of devices: {}\n", table.size());
    std::print("minimum memory usage: {} MB\n", (estimateMinimalMemoryUsage(table) >> 20));
    std::print("total memory usage: {} MB\n", (estimateTotalMemoryUsage(table) >> 20));
}

} // namespace fair

const static boost::ut::suite<"FAIR Device Name Mapping"> deviceMappingTests = [] {
    using namespace boost::ut;

    "single device tests"_test = [] {
        fair::printDeviceInfo("GTK7DS2HR"); // example with all fields
        fair::printDeviceInfo("FPF2KM4S");
        fair::printDeviceInfo("1S00KS2CV"); // device with a digitizer class instance
        fair::printDeviceInfo("GE01BU2F");
        fair::printDeviceInfo("YR02KH");
    };

    "bulk device tests"_test = [] {
        for (auto& name : fair::kTestDeviceNames) {
            fair::printDeviceInfo(name);
        }
    };

    "special cases"_test = [] {
        auto deviceInfo = fair::getDeviceInfo("GECD001");
        expect(deviceInfo.deviceFunction == "gen. digitizer DAQ");
        expect(deviceInfo.sequence == "001");

        deviceInfo = fair::getDeviceInfo("GECD002");
        expect(deviceInfo.deviceFunction == "gen. digitizer DAQ");
        expect(deviceInfo.sequence == "002");

        deviceInfo = fair::getDeviceInfo("GS02BE1F");
        expect(deviceInfo.location == "SIS18");
        expect(deviceInfo.section == "2nd period");
        expect(deviceInfo.sequence == "1");
        expect(deviceInfo.deviceFunction == "RF cavity frequency ramp");
        expect(deviceInfo.devicePosition.empty());

        deviceInfo = fair::getDeviceInfo("GE01KP02");
        expect(deviceInfo.location == "ESR");
        expect(deviceInfo.section == "1st arc");
        expect(deviceInfo.sequence == "02");
        expect(deviceInfo.deviceFunction == "pole face coil winding");
        expect(deviceInfo.devicePosition.empty());

        deviceInfo = fair::getDeviceInfo("1S11KH1");
        expect(deviceInfo.location == "SIS100");
        expect(deviceInfo.section == "11");
        expect(deviceInfo.sequence == "1");
        expect(deviceInfo.deviceFunction == "horizontal correction dipole (steerer)");
        expect(deviceInfo.devicePosition.empty());
    };

    "signal table generation"_test = [] {
        auto signalTable = fair::generateSignalTable();
        fair::printSignalTable(signalTable);
    };
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
