#ifndef OPENDIGITIZER_SERVICE_MDSPAN_SUPPORT
#define OPENDIGITIZER_SERVICE_MDSPAN_SUPPORT

#include <IoSerialiserCmwLight.hpp>
#include <IoSerialiserJson.hpp>
#include <IoSerialiserYaS.hpp>
#include <mdspan.hpp>
#include <variant>

namespace stdx = std::experimental;

namespace opendigitizer::acq {

template<typename ElementType>
struct data_handle {
    std::shared_ptr<std::vector<ElementType>> ptr;
    std::size_t                               offset = 0;

    ElementType                              &operator[](std::size_t i) { return (*ptr)[offset + i]; }

    data_handle                               operator+(const std::size_t addend) { return data_handle{ .ptr = ptr, .offset = offset + addend }; }

    data_handle(std::vector<ElementType> data)
        : ptr(std::make_shared<std::vector<ElementType>>(std::move(data))), offset(0){};

    data_handle() = default;
};

/**
 * An accessor which stores a shared pointer to std::vector inside of the mdspan instead of a pointer.
 */
template<typename ElementType>
struct owning_accessor {
    std::variant<std::vector<ElementType>, std::reference_wrapper<std::vector<ElementType>>> data;

    using element_type                            = ElementType;
    using reference                               = ElementType &;
    using offset_policy                           = owning_accessor<ElementType>;
    using data_handle_type                        = data_handle<ElementType>;
    using pointer                                 = data_handle_type;

    constexpr explicit owning_accessor() noexcept = default;

    [[nodiscard]] constexpr reference access(pointer p, size_t i) const noexcept {
        return p[i];
    }

    [[nodiscard]] constexpr data_handle_type offset(pointer p, size_t i) const noexcept {
        return p + i;
    }
};

static_assert(std::copyable<owning_accessor<float>>);
static_assert(std::is_nothrow_move_constructible_v<owning_accessor<float>>);
static_assert(std::is_nothrow_move_assignable_v<owning_accessor<float>>);
static_assert(std::is_nothrow_swappable_v<owning_accessor<float>>);
} // namespace opendigitizer::acq

// implement serialisers for mdspan
template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::YaS, stdx::mdspan<T, extents, layout, accessor>> {
    using mdspan_t = stdx::mdspan<T, extents, layout, accessor>;
    forceinline static constexpr uint8_t getDataTypeId() {
        return yas::ARRAY_TYPE_OFFSET + yas::getDataTypeId<T>();
    }
    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const mdspan_t &value) noexcept {
        std::array<int32_t, extents::rank()> dims;
        std::size_t                          n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        buffer.put(dims);
        std::span<T> data(&value[std::array<int, extents::rank()>{}], n);
        buffer.put(std::vector<T>{ data.begin(), data.end() }); // todo: account for strides and offsets (possibly use iterators?)
    }
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, T & /*value*/) noexcept {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};

template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::CmwLight, stdx::mdspan<T, extents, layout, accessor>> {
    using mdspan_t = stdx::mdspan<T, extents, layout, accessor>;
    inline static constexpr uint8_t getDataTypeId() {
        // clang-format off
    if      constexpr (extents::rank() == 1) { return cmwlight::getTypeIdVector<T>(); }
    else if constexpr (std::is_same_v<bool   , T> && extents::rank() == 2) { return 17; }
    else if constexpr (std::is_same_v<int8_t , T> && extents::rank() == 2) { return 18; }
    else if constexpr (std::is_same_v<int16_t, T> && extents::rank() == 2) { return 19; }
    else if constexpr (std::is_same_v<int32_t, T> && extents::rank() == 2) { return 20; }
    else if constexpr (std::is_same_v<int64_t, T> && extents::rank() == 2) { return 21; }
    else if constexpr (std::is_same_v<float  , T> && extents::rank() == 2) { return 22; }
    else if constexpr (std::is_same_v<double , T> && extents::rank() == 2) { return 23; }
    else if constexpr (std::is_same_v<char   , T> && extents::rank() == 2) { return 203; }
    else if constexpr (opencmw::is_stringlike<T>  && extents::rank() == 2) { return 24; }
    else if constexpr (std::is_same_v<bool   , T>) { return 25; }
    else if constexpr (std::is_same_v<int8_t , T>) { return 26; }
    else if constexpr (std::is_same_v<int16_t, T>) { return 27; }
    else if constexpr (std::is_same_v<int32_t, T>) { return 28; }
    else if constexpr (std::is_same_v<int64_t, T>) { return 29; }
    else if constexpr (std::is_same_v<float  , T>) { return 30; }
    else if constexpr (std::is_same_v<double , T>) { return 31; }
    else if constexpr (std::is_same_v<char   , T>) { return 204; }
    else if constexpr (opencmw::is_stringlike<T> ) { return 32; }
    else { static_assert(opencmw::always_false<T>); }
        // clang-format on
    }
    constexpr static void serialise(IoBuffer &buffer, FieldDescription auto const & /*field*/, const mdspan_t &value) noexcept {
        std::array<int32_t, extents::rank()> dims;
        std::size_t                          n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        buffer.put(dims);
        std::span<T> data(&value[std::array<int, extents::rank()>{}], n);
        buffer.put(std::vector<T>{ data.begin(), data.end() }); // todo: account for strides and offsets (possibly use iterators?)
    }
    constexpr static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, T & /*value*/) noexcept {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};

template<typename T, typename extents, typename layout, typename accessor>
struct opencmw::IoSerialiser<opencmw::Json, stdx::mdspan<T, extents, layout, accessor>> {
    using mdspan_t = stdx::mdspan<T, extents, layout, accessor>;
    static std::vector<T> to_vec(mdspan_t data, std::size_t n) {
        std::span<T> spandata(&data[std::array<int, extents::rank()>{ 0 }], n);
        return std::vector<T>{ spandata.begin(), spandata.end() };
    }
    inline static constexpr uint8_t getDataTypeId() { return IoSerialiser<Json, START_MARKER>::getDataTypeId(); } // because the object is serialised as a subobject, we have to emmit START_MARKER
    constexpr static void           serialise(IoBuffer &buffer, FieldDescription auto const &field, const mdspan_t &value) noexcept {
        using namespace std::string_view_literals;
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("{\n"sv);
        std::array<int32_t, mdspan_t::rank()> dims;
        std::size_t                           n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            dims[i] = static_cast<int32_t>(value.extent(i));
            n *= dims[i];
        }
        FieldDescriptionShort memberField{ .headerStart = 0, .dataStartPosition = 0, .dataEndPosition = 0, .subfields = 0, .fieldName = ""sv, .intDataType = IoSerialiser<Json, T>::getDataTypeId(), .hierarchyDepth = static_cast<uint8_t>(field.hierarchyDepth + 1U) };
        memberField.fieldName = "dims"sv;
        FieldHeaderWriter<Json>::template put<IoBuffer::WITHOUT>(buffer, memberField, dims);
        memberField.fieldName = "values"sv;
        FieldHeaderWriter<Json>::template put<IoBuffer::WITHOUT>(buffer, memberField, to_vec(value, n));
        buffer.resize(buffer.size() - 2); // remove trailing comma
        buffer.put<opencmw::IoBuffer::MetaInfo::WITHOUT>("\n}\n"sv);
    }
    static void deserialise(IoBuffer & /*buffer*/, FieldDescription auto const & /*field*/, T & /*value*/) {
        throw ProtocolException("Deserialisation of mdspan not implemented yet");
    }
};

template<typename T, typename extents, typename layout, typename accessor>
class opencmw::mustache::mustache_data<stdx::mdspan<T, extents, layout, accessor>> : public mustache_data<std::vector<T>> {
    using mdspan_t = stdx::mdspan<T, extents, layout, accessor>;
    static std::vector<T> to_vec(mdspan_t data) {
        std::size_t n = 1;
        for (uint32_t i = 0U; i < mdspan_t::rank(); i++) {
            n *= static_cast<int32_t>(data.extent(i));
        }
        std::span<T> spandata(&data[std::array<int, extents::rank()>{ 0 }], n);
        return std::vector<T>{ spandata.begin(), spandata.end() };
    }

public:
    explicit mustache_data(mdspan_t val)
        : mustache_data<std::vector<T>>(to_vec(val)) {}
};
#endif // OPENDIGITIZER_SERVICE_MDSPAN_SUPPORT
