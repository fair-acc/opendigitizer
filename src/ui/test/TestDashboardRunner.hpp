#ifndef OPENDIGITIZER_TEST_TEST_SCHEDULER_HPP
#define OPENDIGITIZER_TEST_TEST_SCHEDULER_HPP

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#include "imgui_test_engine/imgui_te_context.h"
#pragma GCC diagnostic pop

#include "ImGuiTestApp.hpp"

#include <Dashboard.hpp>

#include <boost/ut.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <print>
#include <source_location>
#include <string_view>
#include <vector>

CMRC_DECLARE(ui_test_assets);

namespace gr::od::test {
inline const auto     defaultGRCFilesystem = cmrc::sample_dashboards::get_filesystem();
constexpr const char* kDefaultGrcPath      = "assets/sampleDashboards/DemoDashboard.grc";

// built, not started: a .grc key that is no setting is dropped silently at load
inline void expectGrcResolves(gr::BlockRegistry& registry, std::string_view source, std::string_view what, const std::function<void(std::string_view, const gr::property_map&)>& checkBlock = {}) {
    using namespace boost::ut;
    gr::PluginLoader loader(registry, gr::globalSchedulerRegistry(), {});
    const auto       graph = gr::loadGrc(loader, source);
    if (!graph) {
        expect(false) << std::format("{}: {}", what, graph.error().message);
        return;
    }
    const auto blocks = gr::pmt::yaml::deserialize(source).value_or(gr::property_map{}).value_or<gr::Tensor<gr::pmt::Value>>("blocks", {});
    expect(gt(blocks.size(), 0UZ)) << what;
    for (const gr::pmt::Value& definition : blocks) {
        const auto parameters = definition.value_or(gr::property_map{}).value_or<gr::property_map>("parameters", {});
        const auto name       = parameters.value_or<std::string>("name", "");
        const auto block      = std::ranges::find_if((*graph)->blocks(), [&name](const auto& b) { return b->name() == name; });
        if (block == (*graph)->blocks().end()) {
            expect(false) << std::format("{}: block '{}' not in the loaded graph", what, name);
            continue;
        }
        const gr::property_map settings = (*block)->settings().get();
        for (const auto& [key, _] : parameters) {
            expect(key == "name" || settings.contains(key)) << std::format("{}: {}: '{}' is no setting", what, name, std::string_view(key));
        }
        if (checkBlock) {
            checkBlock(name, settings);
        }
    }
}

inline std::vector<const ImGuiWindow*> chartWindows(const Dashboard& dashboard) {
    std::vector<const ImGuiWindow*> windows;
    for (const auto& uiWindow : dashboard.uiWindows) {
        if (uiWindow.block) {
            windows.push_back(ImGui::FindWindowByName(uiWindow.window->name.c_str()));
        }
    }
    return windows;
}

inline void waitForStableCharts(ImGuiTestContext* ctx, const Dashboard& dashboard) {
    constexpr std::size_t kStableFrames = 5UZ;
    std::vector<ImRect>   previous;
    for (std::size_t stableFrames = 0UZ; stableFrames < kStableFrames;) {
        ctx->Yield();
        std::vector<ImRect> current;
        for (const ImGuiWindow* window : chartWindows(dashboard)) {
            current.push_back(window ? ImRect(window->Pos, window->Pos + window->Size) : ImRect());
        }
        const bool same = !current.empty() && std::ranges::equal(current, previous, [](const ImRect& a, const ImRect& b) { return a.Min == b.Min && a.Max == b.Max; });
        stableFrames    = same ? stableFrames + 1UZ : 0UZ;
        previous        = std::move(current);
    }
}

inline void waitUntilAllSamplesDrawn(ImGuiTestContext* ctx, const Dashboard& dashboard) {
    const auto isChart     = [&dashboard](const auto& block) { return std::ranges::contains(dashboard.uiWindows, block, &Dashboard::UIWindow::block); };
    const auto hasFinished = [&isChart](const auto& block) { return isChart(block) || block->state() == gr::lifecycle::State::STOPPED; }; // chart blocks run until the scheduler stops
    while (!std::ranges::all_of(dashboard.session.graph().blocks(), hasFinished)) {
        ctx->Yield();
    }
    waitForStableCharts(ctx, dashboard);
}

/// an item of an ImControl drawn in `windowRef`; ImGui::BeginCombo does not report its label to the test engine, so "**/##Label" does not find a control's dropdown
inline ImGuiID controlItemId(ImGuiTestContext* ctx, const char* windowRef, std::string_view controlUniqueName, const char* itemLabel) {
    const ImGuiWindow* window = ctx->WindowInfo(windowRef).Window;
    IM_CHECK_RETV(window != nullptr, 0U);
    return ImHashStr(itemLabel, 0UZ, ImHashStr(controlUniqueName.data(), controlUniqueName.size(), window->ID));
}

struct TestDashboardRunner {
    std::shared_ptr<opencmw::client::RestClient> restClient = std::make_shared<opencmw::client::RestClient>();
    std::unique_ptr<Dashboard>                   dashboard;

    cmrc::embedded_filesystem previousReloadFilesystem = defaultGRCFilesystem;
    std::string               previousReloadGRCPath    = kDefaultGrcPath;

    // since the last wait-for-reply (which clears them) or the test start: an interaction such as MouseClick may send a message and process frames before the wait
    std::vector<gr::Message> collectedMessages;
    std::size_t              _sentinelCount = 0UZ;

    virtual ~TestDashboardRunner() = default;

    template<typename Condition>
    bool waitUntil(ImGuiTestContext* ctx, std::string_view what, Condition&& condition, std::source_location location = std::source_location::current()) {
        return gr::od::test::waitUntil(
            ctx, what,
            [this, &condition] {
                if (condition()) {
                    return true;
                }
                dashboard->session.receiveMessages();
                return false;
            },
            location);
    }

    // since the last wait or clearMessages()
    bool waitForReplies(ImGuiTestContext* ctx, std::string_view endpoint, std::size_t count = 1UZ, std::source_location location = std::source_location::current()) {
        waitUntil(ctx, std::format("{} replies on {}", count, endpoint), [this, endpoint, count] { return static_cast<std::size_t>(std::ranges::count(collectedMessages, endpoint, &gr::Message::endpoint)) >= count; }, location);
        clearMessages();
        return true;
    }

    // every setting sent to `blockName` before the call has been applied: a Get of its staged settings, which queues behind them, replies with none left;
    // a second round covers the settings that control blocks send via the UI, which forwards them before the first reply
    void waitUntilApplied(ImGuiTestContext* ctx, std::string_view blockName, std::source_location location = std::source_location::current()) {
        bool applied = false;
        for (std::size_t round = 0UZ; round < 2UZ || !applied; ++round) {
            const std::string requestId = std::format("applied-{}-{}", blockName, ++_sentinelCount);
            dashboard->session.sendMessage(makeMessage(gr::message::Command::Get, std::string(blockName), std::string(gr::block::property::kStagedSetting), {}, requestId));
            const auto reply = [this, &requestId] { return std::ranges::find(collectedMessages, requestId, &gr::Message::clientRequestID); };
            waitUntil(ctx, std::format("{} replies with its staged settings", blockName), [&] { return reply() != collectedMessages.end(); }, location);
            applied = reply()->data.has_value() && reply()->data->empty();
        }
    }

    void sendSetting(std::string blockName, gr::property_map data) { dashboard->session.sendMessage(makeMessage(gr::message::Command::Set, std::move(blockName), std::string(gr::block::property::kSetting), std::move(data))); }

    // the chart block of the first dashboard window whose block type contains `type`
    template<typename TChart>
    [[nodiscard]] TChart* chart(std::string_view type) const {
        const auto window = std::ranges::find_if(dashboard->uiWindows, [type](const auto& w) { return w.block && w.block->typeName().find(type) != std::string_view::npos; });
        return window == dashboard->uiWindows.end() ? nullptr : static_cast<TChart*>(window->block->raw());
    }

    virtual void waitForScheduler(ImGuiTestContext* ctx, std::source_location location = std::source_location::current()) {
        // every test that listens for messages waits for the scheduler first
        clearMessages();

        if (!dashboard || !dashboard->session || dashboard->session.state() == gr::lifecycle::State::STOPPED) {
            reload(previousReloadFilesystem, previousReloadGRCPath.c_str());
        }

        waitUntil(ctx, "the scheduler is active and its root graph is known", [this] { return gr::lifecycle::isActive(dashboard->session.state()) && !dashboard->session.graphModel.rootBlock.blockUniqueName.empty(); }, location);
        waitForAllSubgraphs(ctx, location);
    }

    void waitForAllSubgraphs(ImGuiTestContext* ctx, std::source_location location) {
        assert(!dashboard->session.graphModel.rootBlock.blockUniqueName.empty() && "root scheduler must be loaded before waitForAllSubgraphs()");

        // every subgraph scheduler must report at least one child
        std::vector<UiGraphBlock*> schedulers;
        const auto                 gatherNotReadySchedulers = [this, &schedulers] {
            schedulers.clear();
            dashboard->session.graphModel.recursiveForEachBlock([&schedulers](const UiGraphModel::FindBlockResult& findResult) {
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

    virtual void onDashboardLoaded() {}            // e.g. qa_flowgraph sets its Dashboard* at every reload
    virtual void onDashboardAboutToBeUnloaded() {} // e.g. qa_flowgraph drops its pointers into the dashboard

    void reloadFromYamlString(const std::string& yamlContents, const char* dashboardName = "empty") {
        auto dashBoardDescription = DashboardDescription::createEmpty(dashboardName);
        onDashboardAboutToBeUnloaded();
        dashboard = std::make_unique<Dashboard>(restClient, dashBoardDescription);

        dashboard->loadAndThen(yamlContents, [this](gr::Graph&& grGraph) { //
            dashboard->session.emplaceGraph(std::move(grGraph));
        });

        assert(dashboard->session);

        onDashboardLoaded();

        // the subscription to the new dashboard's messages lasts until the dashboard is destroyed: one dashboard at a time
        clearMessages();
        std::ignore = this->dashboard->session.graphModel.subscribeToResponses( //
            [this](const gr::Message& reply) { collectedMessages.push_back(reply); });
    }

    /// a fresh scheduler and graph, so that tests do not depend on earlier runs
    void reload(const cmrc::embedded_filesystem& fs = defaultGRCFilesystem, const char* grc = kDefaultGrcPath, const char* dashboardName = "empty") {
        previousReloadFilesystem = fs;
        previousReloadGRCPath    = grc;

        auto grcFile = fs.open(grc);
        reloadFromYamlString(std::string(grcFile.begin(), grcFile.end()), dashboardName);
    }

    void reloadSubgraph() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_subgraph.grc", "subgraph_test"); }
    void reloadGrouping() { reload(cmrc::ui_test_assets::get_filesystem(), "examples/qa_grouping.grc", "grouping_test"); }

    void unloadDashboard() {
        onDashboardAboutToBeUnloaded();
        dashboard.reset();
    }

    const auto& blocks() const {
        assert(dashboard);
        auto& rootChildren = dashboard->session.graphModel.rootBlock.childBlocks;
        assert(rootChildren.size() == 1);
        return rootChildren[0]->childBlocks;
    }

    bool hasBlocks() const { return dashboard && !dashboard->session.graphModel.rootBlock.childBlocks.empty() && !blocks().empty(); }

    void stopScheduler() {
        if (dashboard && dashboard->session) {
            dashboard->session.stop();
        }
    }

    void clearMessages() { this->collectedMessages.clear(); }

    [[nodiscard]] std::shared_ptr<gr::BlockModel> findBlockInRootGraph(std::string_view name) const {
        const auto blocks = dashboard->session.graph().blocks();
        const auto it     = std::ranges::find(blocks, name, &gr::BlockModel::name);
        return it == blocks.end() ? nullptr : *it;
    }

    [[nodiscard]] gr::pmt::Value setting(std::string_view blockName, std::string_view key) const {
        const auto block = findBlockInRootGraph(blockName);
        return block ? block->settings().get(std::string{key}).value_or(gr::pmt::Value{}) : gr::pmt::Value{};
    }

    template<typename T>
    [[nodiscard]] bool blockHasBasicTypeKeyValuePair(std::string_view blockName, std::string_view key, T expected) const {
        const gr::pmt::Value value = setting(blockName, key);
        const T*             typed = value.get_if<T>();
        return typed != nullptr && *typed == expected;
    }

    [[nodiscard]] bool blockHasStringKeyValuePair(std::string_view blockName, std::string_view key, std::string_view expected) const {
        const gr::pmt::Value            value = setting(blockName, key);
        std::optional<std::string_view> typed = value.get_if<std::string_view>();
        return typed.has_value() && *typed == expected;
    }

    [[nodiscard]] bool blockApproximatelyHasDoubleKeyValuePair(std::string_view blockName, std::string_view key, double expected) const {
        const gr::pmt::Value value = setting(blockName, key);
        const double*        typed = value.get_if<double>();
        return typed != nullptr && std::abs(*typed - expected) < 1e-9;
    }
};
} // namespace gr::od::test

#endif
