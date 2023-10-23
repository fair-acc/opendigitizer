#include "remotedatasource.h"

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>

#include <daq_api.hpp>

#include "../app.h"

using namespace opendigitizer::acq;

template<typename T>
    requires std::is_arithmetic_v<T>
struct RemoteSource : public gr::Block<RemoteSource<T>> {
    gr::PortOut<T> out{};

    RemoteSource() {
    }

    ~RemoteSource() {
    }

    std::make_signed_t<std::size_t>
    available_samples(const RemoteSource & /*d*/) noexcept {
        // std::lock_guard lock(m_type->m_mutex);
        // outputs()[0].dataSet = m_type->m_data[m_type->m_active].channelValue;

        return 0;
    }

    T processOne() {
        return {};
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

    void subscribe() {
        if (m_subscribed++ > 0) {
            return;
        }

        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Subscribe;
        command.endpoint = m_uri;

        command.callback = [this](const opencmw::mdp::Message &rep) {
            if (rep.data.size() == 0) {
                return;
            }

            auto buf = rep.data;

            try {
                std::lock_guard lock(m_mutex);
                m_active = (m_active + 1) % 2;
                opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::IGNORE>(buf, m_data[m_active]);
            } catch (opencmw::ProtocolException &e) {
                fmt::print("{}\n", e.what());
                return;
            }
        };
        m_client.request(command);
    }

    void unsubscribe() {
        assert(m_subscribed > 0);
        if (--m_subscribed > 0) {
            return;
        }

        fmt::print("unSUB\n");
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.endpoint = m_uri;
        m_client.request(command);
    }

    opencmw::URI<>              m_uri;
    opencmw::client::RestClient m_client;
    int                         m_subscribed = 0;

    Acquisition                 m_data[2];

    std::mutex                  m_mutex;
    int                         m_active = 0;
};

RemoteDataSource::RemoteDataSource(std::string_view name, RemoteBlockType *t)
    : Block(name, t->name, t)
    , m_type(t) {
    m_type->subscribe();
}

RemoteDataSource::~RemoteDataSource() {
    m_type->unsubscribe();
}

std::unique_ptr<gr::BlockModel> RemoteDataSource::createGraphNode() {
    return std::make_unique<gr::BlockWrapper<RemoteSource<float>>>();
}

// void RemoteDataSource::processData() {
//     std::lock_guard lock(m_type->m_mutex);
//     outputs()[0].dataSet = m_type->m_data[m_type->m_active].channelValue;
// }

void RemoteDataSource::registerBlockType(FlowGraph *fg, std::string_view uri) {
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Get;
    command.endpoint = opencmw::URI<opencmw::STRICT>::UriFactory().path(uri).build();

    auto *dashboard  = App::instance().dashboard.get();

    command.callback = [fg, dashboard, uri = std::string(uri)](const opencmw::mdp::Message &rep) {
        if (rep.data.size() == 0) {
            return;
        }

        auto        buf = rep.data;
        Acquisition reply;

        try {
            opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::IGNORE>(buf, reply);
        } catch (opencmw::ProtocolException &e) {
            fmt::print("{}\n", e.what());
            return;
        }

        auto t             = std::make_unique<RemoteBlockType>(uri);
        t->outputs[0].name = std::move(reply.channelName);
        App::instance().schedule([uri, fg, dashboard, t = std::move(t)]() mutable {
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
