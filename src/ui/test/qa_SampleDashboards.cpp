#include "App.hpp"
#include "DashboardBlocks.hpp"
#include "TestDashboardRunner.hpp"

#include <boost/ut.hpp>

#include <cmrc/cmrc.hpp>

#include <cstdio>
#include <print>
#include <set>
#include <string>
#include <vector>

CMRC_DECLARE(sample_dashboards);

using namespace gr::od;

int main() {
    using namespace boost::ut;
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    gr::BlockRegistry registry;
    registerDashboardBlocks(registry);
    registerAppBlocks(registry);

    // gr-digitizers' Picoscope destructor dereferences its device, which exists only after start(): ids checked, not built
    const std::set<std::string> kIdsOnly{"PulsedPowerDemo.grc"};

    const auto               filesystem = cmrc::sample_dashboards::get_filesystem();
    std::vector<std::string> dashboards;
    for (const auto& entry : filesystem.iterate_directory("assets/sampleDashboards")) {
        if (entry.is_file() && entry.filename().ends_with(".grc")) {
            dashboards.push_back(entry.filename());
        }
    }

    "every sample dashboard is embedded"_test = [&dashboards] { expect(ge(dashboards.size(), 11UZ)) << "dashboards found"; };

    "a sample dashboard's blocks, settings and connections resolve with the application's blocks"_test = [&](const std::string& dashboard) {
        const auto             file = filesystem.open("assets/sampleDashboards/" + dashboard);
        const std::string_view source(file.begin(), file.end());
        std::println(stderr, "resolving {}", dashboard);
        if (!kIdsOnly.contains(dashboard)) {
            test::expectGrcResolves(registry, source, dashboard);
            return;
        }
        const auto blocks = gr::pmt::yaml::deserialize(source).value_or(gr::property_map{}).value_or<gr::Tensor<gr::pmt::Value>>("blocks", {});
        expect(gt(blocks.size(), 0UZ)) << dashboard;
        for (const gr::pmt::Value& definition : blocks) {
            const auto id = definition.value_or(gr::property_map{}).value_or<std::string>("id", "");
            expect(registry.contains(id)) << std::format("{}: block type '{}' is not registered", dashboard, id);
        }
    } | dashboards;
    std::println(stderr, "all sample dashboards resolved");
}
