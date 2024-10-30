#ifndef OPENDIGITIZER_REMOTESOURCE_HPP
#define OPENDIGITIZER_REMOTESOURCE_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/DataSet.hpp>

#include <daq_api.hpp>

#include <IoSerialiserYaS.hpp>
#include <MdpMessage.hpp>
#include <RestClient.hpp>
#include <opencmw.hpp>
#include <type_traits>

namespace opendigitizer {

template<typename T>
requires std::is_floating_point_v<T>
struct RemoteStreamSource : public gr::Block<RemoteStreamSource<T>> {
    gr::PortOut<T>              out;
    std::string                 remote_uri;
    std::string                 signal_name;
    std::string                 signal_unit;
    float                       signal_min;
    float                       signal_max;
    opencmw::client::RestClient _client;

    GR_MAKE_REFLECTABLE(RemoteStreamSource, out, remote_uri, signal_name, signal_unit, signal_min, signal_max);

    struct Data {
        opendigitizer::acq::Acquisition acq;
        std::size_t                     read = 0;
    };

    struct Queue {
        std::deque<Data> data;
        std::mutex       mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    void updateSettingsFromAcquisition(const opendigitizer::acq::Acquisition& acq) {
        if (signal_name != acq.channelName.value() || signal_unit != acq.channelUnit.value() || signal_min != acq.channelRangeMin.value() || signal_max != acq.channelRangeMax.value()) {
            this->settings().set({{"signal_name", acq.channelName.value()}, {"signal_unit", acq.channelUnit.value()}, {"signal_min", acq.channelRangeMin.value()}, {"signal_max", acq.channelRangeMax.value()}});
        }
    }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        std::size_t     written = 0;
        std::lock_guard lock(_queue->mutex);
        while (written < output.size() && !_queue->data.empty()) {
            auto& d = _queue->data.front();
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

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
        const auto oldValue = old_settings.find("remote_uri");
        if (oldValue != old_settings.end()) {
            const auto oldUri = std::get<std::string>(oldValue->second);
            if (!oldUri.empty()) {
                fmt::print("Unsubscribing from {}\n", oldUri);
                opencmw::client::Command command;
                command.command  = opencmw::mdp::Command::Unsubscribe;
                command.topic    = opencmw::URI<>(remote_uri);
                command.callback = [oldUri](const opencmw::mdp::Message&) {
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

        std::weak_ptr maybeQueue = _queue;

        command.callback = [maybeQueue](const opencmw::mdp::Message& rep) {
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
                queue->data.push_back({std::move(acq), 0});
            } catch (opencmw::ProtocolException& e) {
                fmt::print(std::cerr, "{}\n", e.what());
                return;
            }
        };
        _client.request(command);
    }
};

template<typename T>
requires std::is_floating_point_v<T>
struct RemoteDataSetSource : public gr::Block<RemoteDataSetSource<T>> {
    gr::PortOut<gr::DataSet<T>> out;
    std::string                 remote_uri;
    opencmw::client::RestClient _client;

    GR_MAKE_REFLECTABLE(RemoteDataSetSource, out, remote_uri);
    struct Queue {
        std::deque<gr::DataSet<T>> data;
        std::mutex                 mutex;
    };

    std::shared_ptr<Queue> _queue = std::make_shared<Queue>();

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        std::lock_guard           lock(_queue->mutex);
        const auto                n   = std::min(_queue->data.size(), output.size());
        std::span<gr::DataSet<T>> out = output;
        for (auto i = 0UZ; i < n; ++i) {
            out[i] = std::move(_queue->data.front());
            _queue->data.pop_front();
        }
        output.publish(n);
        return gr::work::Status::OK;
    }

    void settingsChanged(const gr::property_map& old_settings, const gr::property_map& /*new_settings*/) {
        const auto oldValue = old_settings.find("remote_uri");
        if (oldValue != old_settings.end()) {
            const auto oldUri = std::get<std::string>(oldValue->second);
            if (!oldUri.empty()) {
                fmt::print("Unsubscribing from {}\n", oldUri);
                opencmw::client::Command command;
                command.command  = opencmw::mdp::Command::Unsubscribe;
                command.topic    = opencmw::URI<>(remote_uri);
                command.callback = [oldUri](const opencmw::mdp::Message&) {
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

        std::weak_ptr maybeQueue = _queue;

        command.callback = [maybeQueue](const opencmw::mdp::Message& rep) {
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
                gr::DataSet<T> ds;
                ds.extents       = {1, static_cast<int32_t>(acq.channelValue.size())};
                ds.signal_names  = {acq.channelName};
                ds.signal_units  = {acq.channelUnit};
                auto convert     = [](float v) { return static_cast<T>(v); };
                auto values      = acq.channelValue | std::views::transform(convert);
                ds.signal_values = {values.begin(), values.end()};
                auto errors      = acq.channelError | std::views::transform(convert);
                ds.signal_errors = {errors.begin(), errors.end()};
                ds.signal_ranges = {{convert(acq.channelRangeMin.value()), convert(acq.channelRangeMax.value())}};
                std::lock_guard lock(queue->mutex);
                queue->data.push_back(std::move(ds));
            } catch (opencmw::ProtocolException& e) {
                fmt::print(std::cerr, "{}\n", e.what());
                return;
            }
        };
        _client.request(command);
    }
};

} // namespace opendigitizer

auto registerRemoteStreamSource  = gr::registerBlock<opendigitizer::RemoteStreamSource, float, double>(gr::globalBlockRegistry());
auto registerRemoteDataSetSource = gr::registerBlock<opendigitizer::RemoteDataSetSource, float, double>(gr::globalBlockRegistry());

#endif
