#include "../include/FAIR/DeviceNameHelper.hpp"
#include <boost/ut.hpp>

#include <fmt/core.h>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace fair {

void printOut(const DeviceInfo& deviceInfo) {                                                                    //
    fmt::println("device name: {:10} -> machine: {:10}, section: {:15}, seqID: {:>3}, function: {:30}, pos: {}", //
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

constexpr std::array<std::string_view, 2>  subDeviceProperties = {"gap", "generator"};
constexpr std::array<std::string_view, 4>  magnetQuantities    = {"voltage", "current", "voltage_diff", "current_diff"};
constexpr std::array<std::string_view, 3>  commonQuantities    = {"temperature", "cpu_load", "n_user"};
constexpr std::array<std::string_view, 3>  rfQuantities        = {"frequency", "phase", "amplitude"};
constexpr std::array<std::string_view, 15> sampleFrequencies   = {"1Hz", "10Hz", "25Hz", "100Hz", "1kHz", "10kHz", "Injection1", "Injection2", "Injection3", "Injection4", "RampStart", "Extraction", "Diag1", "Diag2", "Diag3"};

inline std::vector<SignalInfo> generateSignalTable() {
    std::vector<SignalInfo> table;

    auto containsAny = [](std::string_view str, const auto& keywords) { //
        return std::ranges::any_of(keywords, [str](std::string_view keyword) { return str.find(keyword) != std::string_view::npos; });
    };

    for (const auto& name : fair::testDeviceNames) {
        auto deviceInfo = fair::getDeviceInfo(name);

        auto addEntry = [&](std::string_view subDevice, std::string_view quantity, std::string_view frequency, std::string_view unit) {
            table.emplace_back(SignalInfo{
                //
                std::string(deviceInfo.name),                                                                                            //
                fmt::format("{}.{}.test-domain.io", deviceInfo.location, deviceInfo.name),                                               //
                fmt::format("{}:{}{}@{}", deviceInfo.name, subDevice.empty() ? "" : fmt::format("{}:", subDevice), quantity, frequency), //
                std::string(quantity), std::string(frequency), std::string(unit),                                                        //
                std::string(deviceInfo.location), std::string(deviceInfo.deviceFunction)                                                 //
            });
        };

        for (const auto& quantity : commonQuantities) {
            addEntry("IO", quantity, "1Hz", quantity == "temperature" ? "°C" : (quantity == "cpu_load" ? "%" : "#"));
        }

        if (containsAny(deviceInfo.deviceFunction, std::array{"magnet", "dipole", "quad", "sextupole", "octopole", "multipole", "pole", "solenoid", "toroid", "septum", "steerer", "source", "tube", "voltage", "power supply"})) {
            for (const auto& quantity : magnetQuantities) {
                for (const auto& frequency : sampleFrequencies) {
                    addEntry("", quantity, frequency, quantity.starts_with("voltage") ? "V" : "A");
                }
            }
        }

        if (containsAny(deviceInfo.deviceFunction, std::array{"RF", "cavity", "Alvarez"})) {
            for (const auto& subDevice : subDeviceProperties) {
                for (const auto& quantity : rfQuantities) {
                    for (const auto& frequency : sampleFrequencies) {
                        addEntry(subDevice, quantity, frequency, quantity == "frequency" ? "Hz" : (quantity == "phase" ? "degree" : "V"));
                    }
                }
            }
        }
    }

    return table;
}

void printSignalTable(const std::vector<SignalInfo>& table) {
    constexpr int deviceNameWidth      = 12;
    constexpr int digitizerDeviceWidth = 35;
    constexpr int fullSignalNameWidth  = 70;
    constexpr int quantityWidth        = 15;
    constexpr int sampleFrequencyWidth = 15;
    constexpr int siUnitWidth          = 10;
    constexpr int acceleratorWidth     = 10;
    constexpr int deviceFunctionWidth  = 30;

    fmt::print("{:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}}\n", //
        "device", deviceNameWidth,                                          //
        "digitizer_device", digitizerDeviceWidth,                           //
        "full_signal_name", fullSignalNameWidth,                            //
        "quantity", quantityWidth,                                          //
        "sample_frequency", sampleFrequencyWidth,                           //
        "SI unit", siUnitWidth,                                             //
        "accelerator", acceleratorWidth,                                    //
        "deviceFunction", deviceFunctionWidth);                             //

    for (const auto& entry : table) {
        fmt::print("{:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}} {:<{}}\n", //
            entry.deviceName, deviceNameWidth,                                  //
            entry.digitizerDevice, digitizerDeviceWidth,                        //
            entry.fullSignalName, fullSignalNameWidth,                          //
            entry.quantity, quantityWidth,                                      //
            entry.sampleFrequency, sampleFrequencyWidth,                        //
            entry.siUnit, siUnitWidth,                                          //
            entry.accelerator, acceleratorWidth,                                //
            entry.deviceFunction, deviceFunctionWidth);
    }
    fmt::print("number of devices: {}\n", table.size());
    fmt::print("minimum memory usage: {} MB\n", (estimateMinimalMemoryUsage(table) >> 20));
    fmt::print("total memory usage: {} MB\n", (estimateTotalMemoryUsage(table) >> 20));
}

} // namespace fair

const static boost::ut::suite<"FAIR Device Name Mapping"> deviceMappingTests = [] {
    using namespace boost::ut;

    "single device tests"_test = [] {
        fair::printDeviceInfo("GTK7DS2HR"); // example with all fields
        fair::printDeviceInfo("FPF2KM4S");  // another example
        fair::printDeviceInfo("1S00KS2CV"); // device with a digitizer class instance
        fair::printDeviceInfo("GE01BU2F");
        fair::printDeviceInfo("YR02KH");
    };

    "bulk device tests"_test = [] {
        for (auto& name : fair::testDeviceNames) {
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

int main() { /* not needed for ut */ }
