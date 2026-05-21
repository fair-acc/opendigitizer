#ifndef OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP
#define OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP

#include <algorithm>
#include <span>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace opendigitizer {

/**
 * @brief Forwards a contiguous window of `input_chunk_size` samples, then skips ahead by `stride` samples.
 *
 * With `stride` larger than the window it takes one window every `stride` input samples (the gr4 FFT block has
 * no `Stride<>`, so this provides the strided windowing in front of it). Skipped samples are consumed
 * incrementally, so it drains a fast stream cheaply — e.g. one 2^19-sample window per second of a 4 MS/s signal.
 */
template<typename T>
struct StridedWindow : gr::Block<StridedWindow<T>, gr::Resampling<1U, 1U, false>, gr::Stride<0U, false>> {
    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    GR_MAKE_REFLECTABLE(StridedWindow, in, out);

    [[nodiscard]] gr::work::Status processBulk(std::span<const T> input, std::span<T> output) const noexcept {
        std::copy_n(input.begin(), std::min(input.size(), output.size()), output.begin());
        return gr::work::Status::OK;
    }
};

} // namespace opendigitizer

#endif // OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP
