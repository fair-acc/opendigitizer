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
    using Parent = gr::Block<RemoteStreamSource<T>>;
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

    std::shared_ptr<Queue> _queue            = std::make_shared<Queue>();
    std::size_t            samples_received  = 0;
    std::size_t            samples_published = 0;
    std::string            subscribed_uri    = "";

    void updateSettingsFromAcquisition(const opendigitizer::acq::Acquisition& acq) {
        if (signal_name != acq.channelName.value() || signal_unit != acq.channelUnit.value() || signal_min != acq.channelRangeMin.value() || signal_max != acq.channelRangeMax.value()) {
            this->settings().set({{"signal_name", acq.channelName.value()}, {"signal_unit", acq.channelUnit.value()}, {"signal_min", acq.channelRangeMin.value()}, {"signal_max", acq.channelRangeMax.value()}});
        }
    }

    void stopSubscription(const std::string uri) {
        fmt::print("Unsubscribing from {}\n", uri);
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.topic    = opencmw::URI<>(remote_uri);
        command.callback = [uri](const opencmw::mdp::Message&) {
            // TODO: Add cleanup once openCMW starts calling the callback on successful unsubscribe
            fmt::print("Unsubscribed from {} successfully\n", uri);
        };
        _client.request(command);
        fmt::print("Unsubscribed from {}\n", uri);
    }

    void startSubscription(const std::string uri) {
        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Subscribe;
        command.topic   = opencmw::URI<>(uri);
        fmt::print("Subscribing to {}\n", uri);

        std::weak_ptr maybeQueue = _queue;

        command.callback = [maybeQueue, uri, this](const opencmw::mdp::Message& rep) {
            fmt::print("RemoteSource: begin callback\n");
            if (!rep.error.empty()) {
                stopSubscription(remote_uri);
                fmt::print("Error in subscription: restarting {}\n", remote_uri);
                startSubscription(remote_uri);
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                opendigitizer::acq::Acquisition acq;
                acq.channelValue.value()      = {0, -5, 5, -5, 5, 0}; // TODO: remove this once the UI supports showing tags and correct time axes
                acq.triggerEventNames.value() = {"SubscriptionInterrupted"};
                acq.triggerIndices.value()    = {0};
                acq.triggerTimestamps.value() = {0};
                std::lock_guard lock(queue->mutex);
                queue->data.push_back({std::move(acq), 0});
                return;
            }
            if (rep.data.empty()) {
                return;
            }
            try {
                auto queue = maybeQueue.lock();
                if (!queue) {
                    return;
                }
                opendigitizer::acq::Acquisition acq;
                auto                            buf = rep.data;
                opencmw::deserialise<opencmw::YaS, opencmw::ProtocolCheck::IGNORE>(buf, acq);
                samples_received += acq.channelValue.size();
                fmt::print("remoteSource queue-length: {}, samples received: {}\n", queue->data.size(), samples_received);
                std::lock_guard lock(queue->mutex);
                queue->data.push_back({std::move(acq), 0});
            } catch (opencmw::ProtocolException& e) {
                fmt::print(std::cerr, "Error in subscription for {}: {}\n", uri, e.what());
                // restart subscription
                return;
            }
        };
        _client.request(command);
        fmt::print("Subscribed to {}\n", uri);
    }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        // update subscription if necessary
        if (subscribed_uri != remote_uri) {
            if (!subscribed_uri.empty()) {
                fmt::print("RemoteSource: stop subscription: {}\n", subscribed_uri);
                stopSubscription(subscribed_uri);
                fmt::print("RemoteSource: stoped subscription: {}\n", subscribed_uri);
            }
            if (!remote_uri.empty()) {
                fmt::print("RemoteSource: start subscription: {}\n", remote_uri);
                startSubscription(remote_uri);
                fmt::print("RemoteSource: started subscription: {}\n", remote_uri);
            }
            subscribed_uri = remote_uri;
        }

#ifdef EMSCRIPTEN
        fmt::print("RemoteSource: handling callbacks in thread: {}\n", std::this_thread::get_id());
        emscripten_sleep(20); // allow for emscripten_fetch callbacks to be executed
        fmt::print("RemoteSource: handled callbacks\n");
#endif

        // copy data into block TODO: check if this could be done directly in callback
        // emscripten: callback is run on same thread, native: callback is run on dedicated httplib thread
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
            //for (const auto& [idx, trigger, timestamp] : std::views::zip(d.acq.triggerIndices.value(), d.acq.triggerEventNames.value(), d.acq.triggerTimestamps.value())) {
            //    auto map = gr::property_map{{gr::tag::TRIGGER_NAME, {trigger}}, {gr::tag::TRIGGER_TIME, {timestamp}}};
            //    output.publishTag(map, idx - d.read);
            //}
            written += in.size();
            d.read += in.size();
            samples_published += in.size();
            if (d.read == d.acq.channelValue.size()) {
                _queue->data.pop_front();
            }
            fmt::print("remoteSource received/published: {}/{}\n", samples_received, samples_published);
        }
        output.publish(written);
        return gr::work::Status::OK;
    }

    void stop() {
        if (!subscribed_uri.empty()) {
            stopSubscription(remote_uri);
            subscribed_uri.clear();
        }
    }
};

template<typename T>
requires std::is_floating_point_v<T>
struct RemoteDataSetSource : public gr::Block<RemoteDataSetSource<T>> {
    using Parent = gr::Block<RemoteDataSetSource<T>>;
    gr::PortOut<gr::DataSet<T>> out;
    std::string                 remote_uri;
    opencmw::client::RestClient _client;

    GR_MAKE_REFLECTABLE(RemoteDataSetSource, out, remote_uri);
    struct Queue {
        std::deque<gr::DataSet<T>> data;
        std::mutex                 mutex;
    };

    std::shared_ptr<Queue> _queue            = std::make_shared<Queue>();
    std::size_t            samples_received  = 0;
    std::size_t            samples_published = 0;
    std::string            subscribed_uri    = "";

    void stopSubscription(const std::string uri) {
        fmt::print("Unsubscribing(settings change) from {}\n", uri);
        opencmw::client::Command command;
        command.command  = opencmw::mdp::Command::Unsubscribe;
        command.topic    = opencmw::URI<>(remote_uri);
        command.callback = [uri](const opencmw::mdp::Message&) {
            // TODO: Add cleanup once openCMW starts calling the callback on successful unsubscribe
            fmt::print("Unsubscribed from {} successfully\n", uri);
        };
        _client.request(command);
    }

    void startSubscription(const std::string uri) {
        opencmw::client::Command command;
        command.command = opencmw::mdp::Command::Subscribe;
        command.topic   = opencmw::URI<>(uri);
        fmt::print("Subscribing to {}\n", uri);

        std::weak_ptr maybeQueue = _queue;

        command.callback = [maybeQueue, uri, this](const opencmw::mdp::Message& rep) {
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
                // restart subscription
                stopSubscription(remote_uri);
                startSubscription(remote_uri);
                return;
            }
        };
        _client.request(command);
    }

    auto processBulk(gr::OutputSpanLike auto& output) noexcept {
        // update subscription if necessary
        if (subscribed_uri != remote_uri) {
            if (!subscribed_uri.empty()) {
                fmt::print("RemoteSource: stop subscription: {}\n", subscribed_uri);
                stopSubscription(subscribed_uri);
                fmt::print("RemoteSource: stoped subscription: {}\n", subscribed_uri);
            }
            if (!remote_uri.empty()) {
                fmt::print("RemoteSource: start subscription: {}\n", remote_uri);
                startSubscription(remote_uri);
                fmt::print("RemoteSource: started subscription: {}\n", remote_uri);
            }
            subscribed_uri = remote_uri;
        }

#ifdef EMSCRIPTEN
        fmt::print("RemoteSource: handling callbacks\n");
        emscripten_sleep(1); // allow for emscripten_fetch callbacks to be executed
        fmt::print("RemoteSource: handled callbacks\n");
#endif

        // copy data into block TODO: check if this could be done directly in callback
        // emscripten: callback is run on same thread, native: callback is run on dedicated httplib thread
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

    void stop() {
        if (!subscribed_uri.empty()) {
            stopSubscription(remote_uri);
            subscribed_uri.clear();
        }
    }
};

} // namespace opendigitizer

auto registerRemoteStreamSource  = gr::registerBlock<opendigitizer::RemoteStreamSource, float, double>(gr::globalBlockRegistry());
auto registerRemoteDataSetSource = gr::registerBlock<opendigitizer::RemoteDataSetSource, float, double>(gr::globalBlockRegistry());

#endif
