#include "fftblock.h"

#include <complex>

namespace DigitizerUi {

namespace {
template<typename T>
class FFT {
    std::size_t                  _N;
    std::vector<std::complex<T>> _w;

public:
    FFT() = delete;
    explicit FFT(std::size_t N)
        : _N(N), _w(N) {
        assert(N > 1 && "N should be > 0");
        for (std::size_t s = 2; s <= N; s *= 2) {
            const std::size_t m = s / 2;
            _w[m]               = exp(std::complex<T>(0, -2 * M_PI / s));
        }
    }

    void compute(std::vector<std::complex<T>> &X) const noexcept {
        std::size_t rev = 0;
        for (std::size_t i = 0; i < _N; i++) {
            if (rev > i && rev < _N) {
                std::swap(X[i], X[rev]);
            }
            std::size_t mask = _N / 2;
            while (rev & mask) {
                rev -= mask;
                mask /= 2;
            }
            rev += mask;
        }

        for (std::size_t s = 2; s <= _N; s *= 2) {
            const std::size_t m = s / 2;
            // std::complex<T> w = exp(std::complex<T>(0, -2 * M_PI / s));
            for (std::size_t k = 0; k < _N; k += s) {
                std::complex<T> wk = 1;
                for (std::size_t j = 0; j < m; j++) {
                    const std::complex<T> t = wk * X[k + j + m];
                    const std::complex<T> u = X[k + j];
                    X[k + j]                = u + t;
                    X[k + j + m]            = u - t;
                    // wk *= w;
                    wk *= _w[m];
                }
            }
        }
    }

    template<typename C>
    std::vector<T> compute_magnitude_spectrum(C signal) {
        static_assert(std::is_same_v<T, typename C::value_type>, "input type T mismatch");
        std::vector<T>               magnitude_spectrum(_N / 2 + 1);
        std::vector<std::complex<T>> fft_signal(signal.size());
        for (std::size_t i = 0; i < signal.size(); i++) {
            fft_signal[i] = { signal[i], 0.0 };
        }

        compute(fft_signal);

        for (std::size_t n = 0; n < _N / 2 + 1; n++) {
            magnitude_spectrum[n] = std::abs(fft_signal[n]) * 2.0 / _N;
        }

        return magnitude_spectrum;
    }
};

} // namespace

FFTBlock::FFTBlock(std::string_view name, BlockType *type)
    : Block(name, type->name, type) {
}

void FFTBlock::processData() {
    auto &in = inputs()[0];
    if (in.connections.empty()) {
        return;
    }

    auto *p   = static_cast<DigitizerUi::Block::OutputPort *>(in.connections[0]->ports[0]);
    auto  val = p->dataSet.asFloat32();

    m_data.resize(val.size());
    FFT<float> fft(val.size());
    m_data               = fft.compute_magnitude_spectrum(val);
    outputs()[0].dataSet = m_data;
}

} // namespace DigitizerUi
