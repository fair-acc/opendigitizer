#include "App.hpp"
#include "DashboardBlocks.hpp"
#include "TestDashboardRunner.hpp"

#include <boost/ut.hpp>

#include <cmrc/cmrc.hpp>

#include <cstdio>
#include <print>
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
        test::expectGrcResolves(registry, source, dashboard);
    } | dashboards;
    std::println(stderr, "all sample dashboards resolved");
}
