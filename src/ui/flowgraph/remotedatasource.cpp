#include "remotedatasource.h"

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>

#include <daq_api.hpp>

#include "../app.h"

using namespace opendigitizer::acq;

template<typename T>
struct RemoteSource : public gr::Block<RemoteSource<T>> {
    gr::PortOut<T>                out{};
    DigitizerUi::RemoteBlockType *block;

    RemoteSource(DigitizerUi::RemoteBlockType *b)
        : block(b) {
    }

    void append(const Acquisition &data) {
        std::lock_guard lock(m_mutex);
        m_data.push_back({ data, 0 });
    }

    struct Data {
        Acquisition data;
        std::size_t read = 0;
    };
    std::deque<Data> m_data;
    std::mutex       m_mutex;

    auto             processBulk(gr::PublishableSpan auto &output) noexcept {
        std::size_t     written = 0;
        std::lock_guard lock(m_mutex);
        while (written < output.size() && !m_data.empty()) {
            auto &d  = m_data.front();
            auto  in = std::span<const float>(d.data.channelValue.begin() + d.read, d.data.channelValue.end());
            in       = in.first(std::min(output.size() - written, in.size()));

            std::copy(in.begin(), in.end(), output.begin() + written);
            written += in.size();
            d.read += in.size();
            if (d.read == d.data.channelValue.size()) {
                m_data.pop_front();
            }
        }
        output.publish(written);
        return gr::work::Status::OK;
    }
};

ENABLE_REFLECTION_FOR_TEMPLATE_FULL((typename T), (RemoteSource<T>), out);

namespace DigitizerUi {

class RemoteBlockType : public BlockType {
public:
    explicit RemoteBlockType(std::string_view uri)
        : BlockType(uri, uri, "Remote signals", true)
        , m_uri(opencmw::URI<>::UriFactory().path(uri).build()) {
        outputs.resize(1);
        outputs[0].type = "float";
        createBlock     = [this](std::string_view n) {
            static int created = 0;
            ++created;
            if (n.empty()) {
                std::string name = fmt::format("remote source {}", created);
                return std::make_unique<RemoteDataSource>(name, this);
            }
            return std::make_unique<RemoteDataSource>(n, this);
        };
    }

    void subscribe(RemoteDataSource *block) {
        m_blocks.push_back(block);
        if (m_subscribed++ > 0) {
            return;
        }

        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Subscribe;
        command.topic   = m_uri;

        fmt::print("Subscribing to {}\n", m_uri);

        command.callback = [this](const opencmw::mdp::Message &rep) {
            if (rep.data.size() == 0) {
                return;
            }

            auto buf = rep.data;

            try {
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, m_data);
                for (auto *b : m_blocks) {
                    if (b->graphNode()) {
                        if (auto *n = static_cast<RemoteSource<float> *>(b->graphNode()->raw())) {
                            n->append(m_data);
                        }
                    }
                }
            } catch (opencmw::ProtocolException &e) {
                fmt::print("{}\n", e.what());
                return;
            }
        };
        m_client.request(command);
    }

    void unsubscribe(RemoteDataSource *block) {
        assert(m_subscribed > 0);

        auto it = std::find(m_blocks.begin(), m_blocks.end(), block);
        if (it == m_blocks.end()) {
            return;
        }
        m_blocks.erase(it);

        if (--m_subscribed > 0) {
            return;
        }

        fmt::print("Unsubscribing from {}\n", m_uri);
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.topic    = m_uri;
        command.callback = [uri = m_uri](const opencmw::mdp::Message &rep) {
            // TODO: Add cleanup once openCMW starts calling the callback
            // on successful unsubscribe
            fmt::format("Unsubscribed from {} successfully\n", uri);
        };
        m_client.request(command);
    }

    opencmw::URI<>                  m_uri;
    opencmw::client::RestClient     m_client;
    int                             m_subscribed = 0;

    Acquisition                     m_data;
    std::vector<RemoteDataSource *> m_blocks;
};

RemoteDataSource::RemoteDataSource(std::string_view name, RemoteBlockType *t)
    : Block(name, t->name, t)
    , m_type(t) {
    m_type->subscribe(this);
}

RemoteDataSource::~RemoteDataSource() {
    m_type->unsubscribe(this);
}

std::unique_ptr<gr::BlockModel> RemoteDataSource::createGraphNode() {
    // return nullptr;
    return std::make_unique<gr::BlockWrapper<RemoteSource<float>>>(m_type);
}

void RemoteDataSource::registerBlockType(FlowGraph *fg, std::string_view uri) {
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Get;
    command.topic    = opencmw::URI<opencmw::STRICT>::UriFactory().path(uri).build();

    auto *dashboard  = App::instance().dashboard.get();

    command.callback = [fg, dashboard, uri = std::string(uri)](const opencmw::mdp::Message &rep) {
        if (rep.data.size() == 0) {
            return;
        }

        auto        buf = rep.data;
        Acquisition reply;

        try {
            opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, reply);
        } catch (opencmw::ProtocolException &e) {
            fmt::print("{}\n", e.what());
            return;
        }

        App::instance().executeLater([uri, fg, channelName = reply.channelName, dashboard]() mutable {
            auto t             = std::make_unique<RemoteBlockType>(uri);
            t->outputs[0].name = channelName;
            if (App::instance().dashboard.get() != dashboard) {
                // If the current dashboard in the app is not the same as it was before issuing the request
                // that means that in the mean time that we were waiting for the this callback to be called
                // the dashboard was closed or changed.
                return;
            }
            fg->registerRemoteSource(std::move(t), uri);
            dashboard->addRemoteService(uri);
        });
    };
    static opencmw::client::RestClient client;
    client.request(command);
}

void RemoteDataSource::registerBlockType(FlowGraph *fg, std::string_view uri, std::string_view signalName) {
    auto t             = std::make_unique<RemoteBlockType>(uri);
    t->outputs[0].name = std::move(signalName);
    fg->registerRemoteSource(std::move(t), uri);
    App::instance().dashboard->addRemoteService(uri);
}
} // namespace DigitizerUi
