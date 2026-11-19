#ifndef OPENDIGITIZER_UI_GRAPHSESSION_HPP
#define OPENDIGITIZER_UI_GRAPHSESSION_HPP

#include <memory>

#include <gnuradio-4.0/Graph.hpp>

#include "GraphModel.hpp"

namespace DigitizerUi {

/// a running flowgraph as the UI sees it: GR4 scheduler, graph model, frame pacer
class GraphSession {
    class Run;

    std::unique_ptr<Run> _run;

public:
    UiGraphModel graphModel;

    GraphSession();
    ~GraphSession();
    GraphSession(const GraphSession&)            = delete;
    GraphSession& operator=(const GraphSession&) = delete;

    void emplaceGraph(gr::Graph&& graph);

    [[nodiscard]] explicit             operator bool() const noexcept { return static_cast<bool>(_run); }
    [[nodiscard]] gr::lifecycle::State state() const;
    [[nodiscard]] std::string_view     schedulerUniqueName() const;
    [[nodiscard]] bool                 isExchangingGraph() const;
    [[nodiscard]] gr::Graph&           graph() const;

    void start();
    void stop();
    void pause();

    void sendToScheduler(std::string_view endpoint, gr::property_map data) { sendMessage(makeMessage(gr::message::Command::Set, std::string(schedulerUniqueName()), std::string(endpoint), std::move(data))); }

    void sendMessage(gr::Message message);
    void handleMessages();
};

} // namespace DigitizerUi

#endif
