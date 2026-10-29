#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>
#include <gnuradio-4.0/electrical/PowerEstimators.hpp>
#include <gnuradio-4.0/filter/time_domain_filter.hpp>

#include <boost/ut.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace {

const std::filesystem::path kSourceDir{OD_SOURCE_DIR};

gr::property_map grcBlockParameters(const std::filesystem::path& grcFile, std::string_view blockName) {
    std::ifstream     file{grcFile};
    std::stringstream content;
    content << file.rdbuf();
    const auto yaml = gr::pmt::yaml::deserialize(content.str());
    if (!yaml) {
        return {};
    }
    const auto blocksIt = yaml->find("blocks");
    if (blocksIt == yaml->end()) {
        return {};
    }
    const gr::pmt::Value blocksValue = (*blocksIt).second;
    const auto           blocksView  = blocksValue.get_if<gr::TensorView<gr::pmt::Value>>();
    if (!blocksView) {
        return {};
    }
    for (const gr::pmt::Value& block : blocksView->owned()) {
        const auto grcBlock = block.get_if<gr::property_map>();
        if (grcBlock && gr::detail::getProperty<std::string>(*grcBlock, "parameters", "name") == blockName) {
            return grcBlock->value_or<gr::property_map>("parameters", gr::property_map{});
        }
    }
    return {};
}

template<typename TBlock>
gr::Size_t initialisedDecimation(gr::property_map parameters) {
    TBlock block(std::move(parameters));
    block.init(block.progress);
    return block.decimation.value;
}

using ThreePhasePowerMetrics = gr::electrical::PowerMetrics<float, 3UZ>;
using DecimatingBy40Filter   = gr::filter::BasicFilterProto<float, gr::Resampling<40UZ, 1UZ, false>>;

} // namespace

const boost::ut::suite<"PulsedPowerGrc"> tests = [] {
    using namespace boost::ut;

    "PowerMetrics of pulsed_power_demo.grc outputs every sample"_test = [] {
        const auto parameters = grcBlockParameters(kSourceDir / "service/gnuradio/examples/pulsed_power_demo.grc", "PowerMetrics");
        expect(fatal(!parameters.empty()));
        expect(eq(initialisedDecimation<ThreePhasePowerMetrics>(parameters), gr::Size_t{1}));
    };

    "PowerMetrics of pulsed_power_mock_demo.grc outputs every sample"_test = [] {
        const auto parameters = grcBlockParameters(kSourceDir / "service/gnuradio/examples/pulsed_power_mock_demo.grc", "PowerMetrics");
        expect(fatal(!parameters.empty()));
        expect(eq(initialisedDecimation<ThreePhasePowerMetrics>(parameters), gr::Size_t{1}));
    };

    "PulsedPowerDemo.grc filters decimate by their Resampling<40, 1> ratio"_test = [](std::string_view blockName) {
        const auto parameters = grcBlockParameters(kSourceDir / "ui/assets/sampleDashboards/PulsedPowerDemo.grc", blockName);
        expect(fatal(!parameters.empty())) << blockName;
        expect(eq(initialisedDecimation<DecimatingBy40Filter>(parameters), gr::Size_t{40})) << blockName;
    } | std::vector<std::string_view>{"DecimateVoltage", "DecimateCurrent"};
};

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
