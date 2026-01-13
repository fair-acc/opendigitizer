#ifndef OPENDIGITIZER_CHARTS_STREAMINGSINKBLOCK_HPP
#define OPENDIGITIZER_CHARTS_STREAMINGSINKBLOCK_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/HistoryBuffer.hpp>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

namespace opendigitizer::charts {

/**
 * @brief Streaming signal sink block for continuous data visualization.
 *
 * This is a GR4 Block that:
 * - Receives streaming data via processBulk()
 * - Stores data in thread-safe circular buffers
 * - Provides data access methods for chart rendering
 *
 * Use with SinkWrapper<StreamingSinkBlock<T>> for SignalSink interface.
 *
 * @tparam T The sample data type (float, double, etc.)
 */
template<typename T = float>
struct StreamingSinkBlock : gr::Block<StreamingSinkBlock<T>, gr::BlockingIO<false>> {
    using Description = gr::Doc<R""(
Streaming signal sink for continuous data visualization.
Receives data from flowgraph and stores in circular buffer for chart rendering.
)"">;

    // Annotated type alias for cleaner declarations
    template<typename U, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<U, description, Arguments...>;

    // --- Input port ---
    gr::PortIn<T> in;

    // --- Reflectable settings ---
    A<std::string, "signal name", gr::Visible> signal_name;
    A<std::uint32_t, "signal color">           signal_color = 0x1f77b4; // Default blue
    A<float, "sample rate", gr::Visible>       sample_rate  = 1000.0f;
    A<std::string, "signal quantity">          signal_quantity;
    A<std::string, "signal unit">              signal_unit;
    A<std::string, "abscissa quantity">        abscissa_quantity = "time";
    A<std::string, "abscissa unit">            abscissa_unit     = "s";
    A<float, "signal min">                     signal_min        = std::numeric_limits<float>::lowest();
    A<float, "signal max">                     signal_max_val    = std::numeric_limits<float>::max();
    A<std::size_t, "buffer capacity">          buffer_capacity   = 4096;

    GR_MAKE_REFLECTABLE(StreamingSinkBlock, in, signal_name, signal_color, sample_rate, signal_quantity, signal_unit, abscissa_quantity, abscissa_unit, signal_min, signal_max_val, buffer_capacity);

    // Constructor
    explicit StreamingSinkBlock(gr::property_map initParameters = {}) : gr::Block<StreamingSinkBlock<T>, gr::BlockingIO<false>>(std::move(initParameters)) {
        _xValues.reserve(buffer_capacity.value);
        _yValues.reserve(buffer_capacity.value);
    }

    // --- Data access methods (for SinkWrapper) ---

    [[nodiscard]] std::string_view signalName() const noexcept { return signal_name.value.empty() ? this->name.value : signal_name.value; }

    [[nodiscard]] std::uint32_t    signalColor() const noexcept { return signal_color.value; }
    [[nodiscard]] float            sampleRate() const noexcept { return sample_rate.value; }
    [[nodiscard]] std::string_view signalQuantity() const noexcept { return signal_quantity.value; }
    [[nodiscard]] std::string_view signalUnit() const noexcept { return signal_unit.value; }
    [[nodiscard]] std::string_view abscissaQuantity() const noexcept { return abscissa_quantity.value; }
    [[nodiscard]] std::string_view abscissaUnit() const noexcept { return abscissa_unit.value; }
    [[nodiscard]] float            signalMin() const noexcept { return signal_min.value; }
    [[nodiscard]] float            signalMax() const noexcept { return signal_max_val.value; }

    [[nodiscard]] std::size_t dataSize() const noexcept {
        std::lock_guard lock(_dataMutex);
        return _yValues.size();
    }

    [[nodiscard]] double xAt(std::size_t index) const {
        std::lock_guard lock(_dataMutex);
        if (index >= _xValues.size()) {
            return 0.0;
        }
        return _xValues[index];
    }

    [[nodiscard]] float yAt(std::size_t index) const {
        std::lock_guard lock(_dataMutex);
        if (index >= _yValues.size()) {
            return 0.0f;
        }
        return static_cast<float>(_yValues[index]);
    }

    [[nodiscard]] std::mutex& dataMutex() const noexcept { return _dataMutex; }

    [[nodiscard]] std::size_t bufferCapacity() const noexcept {
        std::lock_guard lock(_dataMutex);
        return _effectiveCapacity;
    }

    void requestCapacity(void* consumer, std::size_t capacity) {
        std::lock_guard lock(_dataMutex);
        _capacityRequirements[consumer] = capacity;
        updateEffectiveCapacity();
    }

    void releaseCapacity(void* consumer) {
        std::lock_guard lock(_dataMutex);
        _capacityRequirements.erase(consumer);
        updateEffectiveCapacity();
    }

    // --- Block processing ---

    gr::work::Status processBulk(gr::ConsumableSpan auto& inputSpan) {
        if (inputSpan.size() == 0) {
            return gr::work::Status::OK;
        }

        std::lock_guard lock(_dataMutex);

        const double dt = 1.0 / static_cast<double>(sample_rate.value);

        for (const auto& sample : inputSpan) {
            // Calculate time based on sample count
            double t = _sampleCount * dt;

            // Add to buffers (circular behavior)
            if (_xValues.size() >= _effectiveCapacity) {
                _xValues.erase(_xValues.begin());
                _yValues.erase(_yValues.begin());
            }
            _xValues.push_back(t);
            _yValues.push_back(sample);

            ++_sampleCount;
        }

        std::ignore = inputSpan.consume(inputSpan.size());
        return gr::work::Status::OK;
    }

private:
    void updateEffectiveCapacity() {
        // Find maximum requested capacity
        std::size_t maxRequested = buffer_capacity.value;
        for (const auto& [_, capacity] : _capacityRequirements) {
            maxRequested = std::max(maxRequested, capacity);
        }
        _effectiveCapacity = maxRequested;
    }

    mutable std::mutex                     _dataMutex;
    std::vector<double>                    _xValues;
    std::vector<T>                         _yValues;
    std::size_t                            _sampleCount       = 0;
    std::size_t                            _effectiveCapacity = 4096;
    std::unordered_map<void*, std::size_t> _capacityRequirements;
};

} // namespace opendigitizer::charts

// Register block types
GR_REGISTER_BLOCK("opendigitizer::charts::StreamingSinkBlock<float>", opendigitizer::charts::StreamingSinkBlock<float>)
GR_REGISTER_BLOCK("opendigitizer::charts::StreamingSinkBlock<double>", opendigitizer::charts::StreamingSinkBlock<double>)

#endif // OPENDIGITIZER_CHARTS_STREAMINGSINKBLOCK_HPP
