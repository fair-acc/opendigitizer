#ifndef OPENDIGITIZER_REMOTESOURCE_HPP
#define OPENDIGITIZER_REMOTESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>

#include <daq_api.hpp>

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>
#include <type_traits>

#include "../app.hpp"

namespace opendigitizer {

template<typename T>
    requires std::is_floating_point_v<T>
struct RemoteSource : public gr::Block<RemoteSource<T>> {
    gr::PortOut<T>              out;
    std::string                 remote_uri;
    std::string                 signal_name;
    std::string                 signal_unit;
    float                       signal_min;
    float                       signal_max;
    opencmw::client::RestClient _client;

    struct Data {
        opendigitizer::acq::Acquisition acq;
        std::size_t                     read = 0;
    };

    struct Queue {
        std::deque<Data> data;
        std::mutex       mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    void                   updateSettingsFromAcquisition(const opendigitizer::acq::Acquisition &acq) {
        if (signal_name != acq.channelName.value() || signal_unit != acq.channelUnit.value() || signal_min != acq.channelRangeMin.value() || signal_max != acq.channelRangeMax.value()) {
            this->settings().set({ { "signal_name", acq.channelName.value() }, { "signal_unit", acq.channelUnit.value() }, { "signal_min", acq.channelRangeMin.value() }, { "signal_max", acq.channelRangeMax.value() } });
        }
    }

    auto processBulk(gr::PublishableSpan auto &output) noexcept {
        std::size_t     written = 0;
        std::lock_guard lock(_queue->mutex);
        while (written < output.size() && !_queue->data.empty()) {
            auto &d = _queue->data.front();
            updateSettingsFromAcquisition(d.acq);
            auto in = std::span<const float>(d.acq.channelValue.begin(), d.acq.channelValue.end());
            in      = in.subspan(d.read, std::min(output.size() - written, in.size() - d.read));

            if constexpr (std::is_same_v<T, float>) {
                std::ranges::copy(in, output.begin() + written);
            } else {
                std::ranges::transform(in, output.begin() + written, [](float v) { return static_cast<T>(v); });
            }
            written += in.size();
            d.read += in.size();
            if (d.read == d.acq.channelValue.size()) {
                _queue->data.pop_front();
            }
        }
        output.publish(written);
        return gr::work::Status::OK;
    }

    void
    settingsChanged(const gr::property_map &old_settings, const gr::property_map & /*new_settings*/) {
        const auto oldValue = old_settings.find("remote_uri");
        if (oldValue != old_settings.end()) {
            const auto oldUri = std::get<std::string>(oldValue->second);
            if (!oldUri.empty()) {
                fmt::print("Unsubscribing from {}\n", oldUri);
                opencmw::client::Command command;
                command.command  = opencmw::mdp::Command::Unsubscribe;
                command.topic    = opencmw::URI<>(remote_uri);
                command.callback = [oldUri](const opencmw::mdp::Message &) {
                    // TODO: Add cleanup once openCMW starts calling the callback
                    // on successful unsubscribe
                    fmt::print("Unsubscribed from {} successfully\n", oldUri);
                };
            }
        }

        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Subscribe;
        command.topic   = opencmw::URI<>(remote_uri);
        fmt::print("Subscribing to {}\n", remote_uri);

        auto &d = DigitizerUi::App::instance().dashboard;
        if (d) {
            d->addRemoteService(remote_uri);
        }

        std::weak_ptr maybeQueue = _queue;

        command.callback         = [maybeQueue](const opencmw::mdp::Message &rep) {
            if (rep.data.empty()) {
                return;
            }
            try {
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                auto                            buf = rep.data;
                opendigitizer::acq::Acquisition acq;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);
                std::lock_guard lock(queue->mutex);
                queue->data.push_back({ std::move(acq), 0 });
            } catch (opencmw::ProtocolException &e) {
                fmt::print(std::cerr, "{}\n", e.what());
                return;
            }
        };
        _client.request(command);
    }
};

} // namespace opendigitizer

ENABLE_REFLECTION_FOR_TEMPLATE(opendigitizer::RemoteSource, out, remote_uri, signal_name, signal_unit, signal_min, signal_max)

auto registerRemoteSource = gr::registerBlock<opendigitizer::RemoteSource, float, double>(gr::globalBlockRegistry());

#endif
