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

    // sometimes, a UI testing interaction such as MouseClick will both cause a
    // message to be sent and then some frames to be processed, resulting in a
    // missed message. For this reason, we listen to all messages since the last
    // wait-for-reply (which clears the collection), or the start of the test case
    std::vector<gr::Message> collectedMessages;

    virtual ~TestDashboardRunner() = default;

    // blocks until `condition` holds, processing scheduler replies and UI frames. There is deliberately no deadline:
    // the outcome must not depend on machine load, and a condition that never holds is caught by the ctest timeout.
    template<typename Condition>
    void waitUntil(ImGuiTestContext* ctx, std::string_view what, Condition&& condition, std::source_location location = std::source_location::current()) {
        if (condition()) {
            return;
        }
        std::println("\twaiting until {} ({}:{})", what, location.file_name(), location.line());
        while (!condition()) {
            dashboard->handleMessages();
            ctx->Yield();
        }
    }

    virtual void waitForScheduler(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
        // this is a bit weird to put here semantically, but we need to remember to call it at the start of each test
        // case and in practice any test that needs to listen for messages will be calling waitForScheduler once at
        // start of test anyways, so just put this here
        clearMessages();

        if (!dashboard || !dashboard->scheduler || dashboard->scheduler->state() == gr::lifecycle::State::STOPPED) {
            reload(previousReloadFilesystem, previousReloadGRCPath.c_str());
        }

        waitUntil(ctx, "the scheduler is active and its root graph is known", [this] { return gr::lifecycle::isActive(dashboard->scheduler->state()) && !dashboard->graphModel.rootBlock.blockUniqueName.empty(); }, location);
        waitForAllSubgraphs(ctx, location);
    }

    void waitForAllSubgraphs(ImGuiTestContext* ctx, std::source_location location) {
        assert(!dashboard->graphModel.rootBlock.blockUniqueName.empty() && "root scheduler must be loaded before waitForAllSubgraphs()");

        // every subgraph scheduler must report at least one child
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
                waitUntil(ctx, std::format("subgraph scheduler '{}' reports its children", scheduler->blockName), [scheduler] { return !scheduler->childBlocks.empty(); }, location);
            }
        }
    }

    virtual void onDashboardLoaded() { /* Handler, for qa_flowgraph and potentially other tests, which need to set the flowgraph's Dashboard* every time a reload occurs */ }
    virtual void onDashboardAboutToBeUnloaded() { /* Handler where qa_flowgraph should unregister connections/pointers to dashboard */ }

    void reloadFromYamlString(const std::string& yamlContents, const char* dashboardName = "empty") {
        auto dashBoardDescription = DigitizerUi::DashboardDescription::createEmpty(dashboardName);
        onDashboardAboutToBeUnloaded();
        dashboard = DigitizerUi::Dashboard::create(restClient, dashBoardDescription);

        dashboard->loadAndThen(yamlContents, [this](gr::Graph&& grGraph) { //
            dashboard->emplaceGraph(std::move(grGraph));
        });

        assert(dashboard->scheduler);

        onDashboardLoaded();

        // loading a new dashboard, listen to messages sent to it. the
        // subscription will stick around until the dashboard is destroyed,
        // that's okay because we only have one dashboard at a time.
        clearMessages();
        std::ignore = this->dashboard->graphModel.subscribeToResponses( //
            [this](const gr::Message& reply) { collectedMessages.push_back(reply); });
    }

    /// Creates a fresh Scheduler and Graph so that tests are more individual and deterministics (i.e. not influenced by previous test runs)
    void reload(const cmrc::embedded_filesystem& fs = defaultGRCFilesystem, const char* grc = defaultGRCPath, const char* dashboardName = "empty") {
        previousReloadFilesystem = fs;
        previousReloadGRCPath    = grc;

        auto grcFile = fs.open(grc);
        reloadFromYamlString(std::string(grcFile.begin(), grcFile.end()), dashboardName);
    }

    void unloadDashboard() {
        onDashboardAboutToBeUnloaded();
        dashboard.reset();
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

    void clearMessages() { this->collectedMessages.clear(); }
};
} // namespace opendigitizer::test

#endif
