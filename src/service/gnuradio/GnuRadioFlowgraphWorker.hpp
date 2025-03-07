#ifndef OPENDIGITIZER_SERVICE_GNURADIOFLOWGRAPHWORKER_H
#define OPENDIGITIZER_SERVICE_GNURADIOFLOWGRAPHWORKER_H

#include "gnuradio-4.0/Message.hpp"
#include <daq_api.hpp>

#include <majordomo/Worker.hpp>

#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/Scheduler.hpp>
#include <gnuradio-4.0/basic/DataSink.hpp>

#include <chrono>
#include <memory>
#include <ranges>
#include <string_view>
#include <utility>

namespace opendigitizer::gnuradio {

using namespace gr;
using namespace gr::message;
using enum gr::message::Command;
using namespace opencmw::majordomo;
using namespace std::chrono_literals;
using namespace std::string_literals;

template<typename TAcquisitionWorker, units::basic_fixed_string serviceName, typename... Meta>
class GnuRadioFlowGraphWorker : public Worker<serviceName, flowgraph::FilterContext, flowgraph::SerialisedFlowgraphMessage, flowgraph::SerialisedFlowgraphMessage, Meta...> {
    gr::PluginLoader*    _pluginLoader;
    TAcquisitionWorker&  _acquisitionWorker;
    std::mutex           _flowgraphLock;
    flowgraph::Flowgraph _flowgraph;

public:
    using super_t = Worker<serviceName, flowgraph::FilterContext, flowgraph::SerialisedFlowgraphMessage, flowgraph::SerialisedFlowgraphMessage, Meta...>;

    explicit GnuRadioFlowGraphWorker(opencmw::URI<opencmw::STRICT> brokerAddress, const opencmw::zmq::Context& context, gr::PluginLoader* pluginLoader, flowgraph::Flowgraph initialFlowGraph, TAcquisitionWorker& acquisitionWorker, Settings settings = {}) //
        : super_t(std::move(brokerAddress), {}, context, std::move(settings)), _pluginLoader(pluginLoader), _acquisitionWorker(acquisitionWorker) {
        init(std::move(initialFlowGraph));
    }

    template<typename BrokerType>
    explicit GnuRadioFlowGraphWorker(const BrokerType& broker, gr::PluginLoader* pluginLoader, flowgraph::Flowgraph initialFlowGraph, TAcquisitionWorker& acquisitionWorker) //
        : super_t(broker, {}), _pluginLoader(pluginLoader), _acquisitionWorker(acquisitionWorker) {
        init(std::move(initialFlowGraph));
    }

    GnuRadioFlowGraphWorker(const GnuRadioFlowGraphWorker&)            = delete;
    GnuRadioFlowGraphWorker& operator=(const GnuRadioFlowGraphWorker&) = delete;

private:
    void init(flowgraph::Flowgraph initialFlowGraph) {

        super_t::setCallback([this](const RequestContext& rawCtx, const flowgraph::FilterContext& /*filterIn*/, const flowgraph::SerialisedFlowgraphMessage& in, flowgraph::FilterContext& /*filterOut*/, flowgraph::SerialisedFlowgraphMessage& out) {
            if (rawCtx.request.command == opencmw::mdp::Command::Get) {
                flowgraph::Flowgraph outFlowgraph;
                handleGetRequest(outFlowgraph);

                gr::Message message;
                message.endpoint = std::string(gr::graph::property::kGraphInspected) + "GRC";

                storeFlowgraphToMessage(outFlowgraph, message);

                out.data = serialiseMessage(message);

            } else if (rawCtx.request.command == opencmw::mdp::Command::Set) {
                gr::Message message     = deserialiseMessage(in.data);
                auto        messageType = message.endpoint;

                if (messageType == "ReplaceGraphGRC") {
                    auto registerError = [&message](std::string errorMessage) { message.data = std::unexpected(gr::Error(std::move(errorMessage))); };

                    message.endpoint = "UpdatedGraphGRC"s;

                    if (!message.data) {
                        registerError("Message data not specified"s);
                    } else {
                        flowgraph::Flowgraph outFlowgraph;
                        auto                 sourceData = flowgraph::getFlowgraphFromMessage(message);
                        if (sourceData) {
                            message.endpoint = "UpdatedGraphGRC"s;
                            replaceGraphGRC(*sourceData, outFlowgraph);
                            storeFlowgraphToMessage(outFlowgraph, message);
                        } else {
                            registerError("Can not parse the graph from the request");
                        }
                    }

                    // Sending the message, or an error
                    out.data = serialiseMessage(message);

                } else {
                    // If this is not a message to replace the whole graph,
                    // it is a message to be sent to the graph
                    WriterSpanLike auto msgSpan = _acquisitionWorker.messagesToScheduler().streamWriter().template reserve<SpanReleasePolicy::ProcessAll>(1UZ);
                    msgSpan[0]                  = std::move(message);
                    msgSpan.publish(1UZ);
                }
            }
        });

        if (initialFlowGraph.serialisedFlowgraph.empty()) {
            return;
        }

        try {
            std::lock_guard lockGuard(_flowgraphLock);
            auto            grGraph = std::make_unique<gr::Graph>(gr::loadGrc(*_pluginLoader, initialFlowGraph.serialisedFlowgraph));
            _flowgraph              = std::move(initialFlowGraph);
            _acquisitionWorker.scheduleGraphChange(std::move(grGraph));
        } catch (const std::string& e) {
            throw std::invalid_argument(fmt::format("Could not parse flow graph: {}", e));
        }
    }

    void handleGetRequest(flowgraph::Flowgraph& out) {
        std::lock_guard lockGuard(_flowgraphLock);
        out = _flowgraph;

        auto serialisedFlowgraph = _acquisitionWorker.withGraph([this](const auto& graph) { return gr::saveGrc(*_pluginLoader, graph); });

        out.serialisedFlowgraph = serialisedFlowgraph.value_or("");
    }

    void replaceGraphGRC(const flowgraph::Flowgraph& in, flowgraph::Flowgraph& out) {
        {
            std::lock_guard lockGuard(_flowgraphLock);
            try {
                auto grGraph = std::make_unique<gr::Graph>(gr::loadGrc(*_pluginLoader, in.serialisedFlowgraph));
                _flowgraph   = in;
                out          = in;

                _acquisitionWorker.scheduleGraphChange(std::move(grGraph));

            } catch (const std::string& e) {
                throw std::invalid_argument(fmt::format("Could not parse flow graph: {}", e));
            }
        }
        notifyUpdate();
    }

    void sendMessage(const gr::Message& message) {
        for (auto subTopic : super_t::activeSubscriptions()) {
            const auto           queryMap  = subTopic.params();
            const auto           filterIn  = opencmw::query::deserialise<flowgraph::FilterContext>(queryMap);
            auto                 filterOut = filterIn;
            flowgraph::Flowgraph subscriptionReply;
            super_t::notify(filterOut, flowgraph::SerialisedFlowgraphMessage{.data = serialiseMessage(message)});
        }
    }

    void notifyUpdate() {
        gr::Message updateMessage;
        updateMessage.endpoint = "UpdatedGraphGRC"s;
        flowgraph::Flowgraph subscriptionReply;
        handleGetRequest(subscriptionReply);
        storeFlowgraphToMessage(subscriptionReply, updateMessage);
        sendMessage(updateMessage);
    }
};

} // namespace opendigitizer::gnuradio

#endif // OPENDIGITIZER_SERVICE_GNURADIOWORKER_H
