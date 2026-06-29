#ifndef OPENDIGITIZER_TEST_TEST_SCHEDULER_HPP
#define OPENDIGITIZER_TEST_TEST_SCHEDULER_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_context.h"
#pragma GCC diagnostic pop

#include <Dashboard.hpp>

#include <chrono>
#include <memory>

namespace opendigitizer::test {
inline const auto     defaultGRCFilesystem = cmrc::sample_dashboards::get_filesystem();
constexpr const char* defaultGRCPath       = "assets/sampleDashboards/DemoDashboard.grc";

struct TestDashboardRunner {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::unique_ptr<DigitizerUi::Dashboard>      dashboard;

    cmrc::embedded_filesystem previousReloadFilesystem = defaultGRCFilesystem;
    std::string               previousReloadGRCPath    = defaultGRCPath;

    virtual ~TestDashboardRunner() = default;

    virtual void waitForScheduler(                                           //
        ImGuiTestContext*         ctx,                                       //
        std::chrono::milliseconds timeout  = std::chrono::seconds(5),        //
        std::source_location      location = std::source_location::current() //
    ) {
        if (!dashboard || !dashboard->scheduler || dashboard->scheduler->state() == gr::lifecycle::State::STOPPED) {
            reload(previousReloadFilesystem, previousReloadGRCPath.c_str());
        }

        const auto start             = std::chrono::high_resolution_clock::now();
        const auto isLoadedAndActive = [this] { return gr::lifecycle::isActive(dashboard->scheduler->state()) && !dashboard->graphModel.rootBlock.blockUniqueName.empty(); };
        while (!isLoadedAndActive() && std::chrono::high_resolution_clock::now() - start < timeout) {
            dashboard->handleMessages();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!isLoadedAndActive()) {
            throw gr::exception(std::format("waitForScheduler({}): timeout exceeded", timeout), location);
        }

        waitForAllSubgraphs(ctx, timeout, location);
    }

    void waitForAllSubgraphs(ImGuiTestContext* ctx, std::chrono::milliseconds timeout, std::source_location location) {
        assert(!dashboard->graphModel.rootBlock.blockUniqueName.empty() && "root scheduler must be loaded before waitForAllSubgraphs()");

        // load and wait for inspection on all subgraphs
        std::vector<UiGraphBlock*> schedulers;
        const auto                 gatherNotReadySchedulers = [this, &schedulers] {
            schedulers.clear();
            dashboard->graphModel.recursiveForEachBlock([&schedulers](const UiGraphModel::FindBlockResult& findResult) {
                if (findResult.block && findResult.block->isScheduler() && findResult.block->childBlocks.empty()) {
                    schedulers.push_back(findResult.block);
                }
                return UiGraphModel::VisitorResult::Recurse;
            });
            return !schedulers.empty();
        };

        while (gatherNotReadySchedulers()) {
            for (UiGraphBlock* scheduler : schedulers) {
                auto innerStart = std::chrono::high_resolution_clock::now();
                while (scheduler->childBlocks.empty() && std::chrono::high_resolution_clock::now() - innerStart < timeout) {
                    dashboard->handleMessages();
                    ctx->Yield();
                }
                if (scheduler->childBlocks.empty()) {
                    auto msg = std::format("waitForScheduler({}): timeout waiting for scheduler {} exceeeded, or it has"
                                           " no children (TestDashboardRunner requires schedulers to have at least one child)",
                        timeout, scheduler->blockName);
                    throw gr::exception(std::move(msg), location);
                }
            }
        }
    }

    virtual void onDashboardLoaded() { /* Handler, for qa_flowgraph and potentially other tests, which need to set the flowgraph's Dashboard* every time a reload occurs */ }

    /// Creates a fresh Scheduler and Graph so that tests are more individual and deterministics (i.e. not influenced by previous test runs)
    void reload(const cmrc::embedded_filesystem& fs = defaultGRCFilesystem, const char* grc = defaultGRCPath, const char* dashboardName = "empty") {
        previousReloadFilesystem = fs;
        previousReloadGRCPath    = grc;

        auto grcFile = fs.open(grc);

        auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty(dashboardName);
        dashboard                 = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);

        dashboard->loadAndThen(std::string(grcFile.begin(), grcFile.end()), [this](gr::Graph&& grGraph) { //
            dashboard->emplaceGraph(std::move(grGraph));
        });

        assert(dashboard->scheduler);

        onDashboardLoaded();
    }

    const auto& blocks() const {
        assert(dashboard);
        auto& rootChildren = dashboard->graphModel.rootBlock.childBlocks;
        assert(rootChildren.size() == 1);
        return rootChildren[0]->childBlocks;
    }

    bool hasBlocks() const { return dashboard && !dashboard->graphModel.rootBlock.childBlocks.empty() && !blocks().empty(); }

    void stopScheduler() {
        if (dashboard && dashboard->scheduler) {
            std::ignore = dashboard->scheduler->stop();
        }
    }

    void startScheduler() {
        assert(dashboard && dashboard->scheduler && "startScheduler called with no scheduler");
        std::ignore = dashboard->scheduler->stop();
    }
};
} // namespace opendigitizer::test

#endif
