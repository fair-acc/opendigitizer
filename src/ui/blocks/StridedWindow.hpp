#ifndef OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP
#define OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP

#include <algorithm>
#include <span>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::od {

template<typename T>
struct StridedWindow : gr::Block<StridedWindow<T>, gr::Resampling<1U, 1U, false>, gr::Stride<0U, false>> {
    using Description = gr::Doc<"Forwards a contiguous window of input_chunk_size samples, then skips ahead by stride samples, for the GR4 FFT block, which has no Stride. Skipped samples are consumed incrementally, so a fast stream drains cheaply.">;

    gr::PortIn<T>  in;
    gr::PortOut<T> out;

    GR_MAKE_REFLECTABLE(StridedWindow, in, out);

    [[nodiscard]] gr::work::Status processBulk(std::span<const T> input, std::span<T> output) const noexcept {
        std::copy_n(input.begin(), std::min(input.size(), output.size()), output.begin());
        return gr::work::Status::OK;
    }
};

} // namespace gr::od

#endif // OPENDIGITIZER_BLOCKS_STRIDEDWINDOW_HPP
