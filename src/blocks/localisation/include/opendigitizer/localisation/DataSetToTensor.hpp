#ifndef OPENDIGITIZER_LOCALISATION_DATASETTOTENSOR_HPP
#define OPENDIGITIZER_LOCALISATION_DATASETTOTENSOR_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/DataSet.hpp>
#include <gnuradio-4.0/Port.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

namespace opendigitizer::localisation {

GR_REGISTER_BLOCK(opendigitizer::localisation::DataSetToTensor, [T], [ float, double ])

template<typename T>
struct DataSetToTensor : gr::Block<DataSetToTensor<T>, gr::NoTagPropagation> {
    using Description = gr::Doc<R""(@brief one tensor [S,N] per DataSet of S signals of N samples, signal s in row s (e.g. a MultiChannelRecorder window for PairCrossCorrelation),
each frame tagged gr:trigger_time (the DataSet's timestamp) and gr:sample_rate (its axis spacing).
A DataSet without signals, whose values are not S x N, or whose axis gives no rate, is refused and counted in `n_refused`.)"">;

    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    gr::PortIn<gr::DataSet<T>> in;
    gr::TensorPortOut<T>       out;

    A<gr::Size_t, "n_refused", gr::Doc<"DataSets not of S signals x N samples with a time axis">> n_refused = 0U;

    GR_MAKE_REFLECTABLE(DataSetToTensor, in, out, n_refused);

    gr::work::Status processBulk(gr::InputSpanLike auto& input, gr::OutputSpanLike auto& output) {
        std::size_t nFrames = 0UZ;
        std::size_t k       = 0UZ;
        for (; k < input.size() && nFrames < output.size(); ++k) {
            const gr::DataSet<T>& set    = input[k];
            const std::size_t     S      = set.signal_names.size();
            const std::size_t     N      = set.extents.size() == 1UZ && set.extents[0] > 0 ? static_cast<std::size_t>(set.extents[0]) : 0UZ;
            const double          rateHz = sampleRateOf(set, N);
            if (S == 0UZ || N == 0UZ || set.signal_values.size() != S * N || rateHz <= 0.) {
                n_refused = n_refused + 1U;
                continue;
            }
            auto frame = gr::emit(*this, output, nFrames, std::array{S, N});
            if (!frame) {
                break;
            }
            std::ranges::copy(set.signal_values, frame->data());
            output.publishTag(gr::property_map{{std::string(gr::tag::kTriggerTime.key()), static_cast<std::uint64_t>(set.timestamp)}, //
                                  {std::string(gr::tag::kSampleRate.key()), static_cast<float>(rateHz)}},
                nFrames);
            ++nFrames;
        }
        std::ignore = input.consume(k);
        output.publish(nFrames);
        return gr::work::Status::OK;
    }

    /// Hz, from the first axis over its whole span; 0 without one
    [[nodiscard]] static double sampleRateOf(const gr::DataSet<T>& set, std::size_t N) {
        if (set.axis_values.empty() || set.axis_values[0].size() != N || N < 2UZ) {
            return 0.;
        }
        const std::span<const T> axis{set.axis_values[0]};
        const double             span = static_cast<double>(axis.back()) - static_cast<double>(axis.front());
        return span > 0. ? static_cast<double>(N - 1UZ) / span : 0.;
    }
};

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_DATASETTOTENSOR_HPP
