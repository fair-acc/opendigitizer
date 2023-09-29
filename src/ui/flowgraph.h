#pragma once

#include <filesystem>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef EMSCRIPTEN
#include "emscripten_compat.h"
#endif
#include <plf_colony.h>

// #include <dataset.hpp>
#include <graph.hpp>
#include <node_traits.hpp>

namespace DigitizerUi {

class FlowGraph;
class Connection;
class Block;
template<template<typename> typename T>
class DefaultGPBlock;

namespace meta
{

template<typename T>
struct is_dataset { constexpr inline static bool value = false; };

template<typename T>
struct is_dataset<fair::graph::DataSet<T>> { constexpr inline static bool value = true; };

template<typename T>
constexpr inline bool is_dataset_v = is_dataset<T>::value;

}

class BlockType {
public:
    struct PortDefinition {
        std::string type;
        std::string name;
        bool dataset = false;
    };

    struct EnumParameter {
        const int size;

        using Options = std::vector<std::string>;
        Options                                  options;
        std::unordered_map<std::string, Options> optionsAttributes;
        std::vector<std::string>                 optionsLabels;

        std::string                              defaultValue;
    };
    template<typename T>
    struct NumberParameter {
        inline explicit NumberParameter(T v)
            : defaultValue(v) {}
        T defaultValue;
    };
    struct StringParameter {
        std::string defaultValue;
    };
    struct Parameter {
        const std::string                                                                       id;
        const std::string                                                                       label;
        std::variant<EnumParameter, NumberParameter<int>, NumberParameter<float>, StringParameter> impl;
    };

    explicit BlockType(std::string_view n, std::string_view label = {}, std::string_view cat = {}, bool source = false);

    const std::string                                       name;
    const std::string                                       label;
    std::vector<Parameter>                                  parameters;
    std::vector<PortDefinition>                             inputs;
    std::vector<PortDefinition>                             outputs;
    const std::string                                       category;
    const bool                                              isSource;
    std::unique_ptr<fair::graph::settings_base>             settings;

    std::function<std::unique_ptr<Block>(std::string_view)> createBlock;

    template<typename T>
    void initPort(auto &vec)
    {
        vec.push_back({});
        auto &p = vec.back();
        p.name = T::static_name();
        p.type = "float";
        if (meta::is_dataset_v<typename T::value_type>) {
            p.dataset = true;
        }
    }

    struct Registry {
        void               loadBlockDefinitions(const std::filesystem::path &dir);
        void               addBlockType(std::unique_ptr<BlockType> &&t);

        //automatically create a BlockType from a graph prototype node template class
        template<template<typename> typename T>
        void               addBlockType(std::string_view typeName)
        {
            auto t         = std::make_unique<DigitizerUi::BlockType>(typeName);
            t->createBlock = [t = t.get()](std::string_view name) {
                return std::make_unique<DefaultGPBlock<T>>(name, t);
            };
            using Node = T<float>;
            namespace meta = fair::graph::traits::node;

            constexpr std::size_t input_port_count = meta::template input_port_types<Node>::size;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (t->template initPort<typename meta::template input_ports<Node>::template at<Is>>(t->inputs), ...);
            }(std::make_index_sequence<input_port_count>());

            constexpr std::size_t output_port_count = meta::template output_port_types<Node>::size;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (t->template initPort<typename meta::template output_ports<Node>::template at<Is>>(t->outputs), ...);
            }(std::make_index_sequence<output_port_count>());

            addBlockType(std::move(t));
        }

        BlockType         *get(std::string_view id) const;

        inline const auto &types() const { return m_types; }

    private:
        // This stuff is to enable looking up in the m_types map with string_view
        template<typename... Keys>
        struct transparent_hash : std::hash<Keys>... {
            using is_transparent = void;
            using std::hash<Keys>::operator()...;
        };

        using transparent_string_hash = transparent_hash<std::string, std::string_view, const char *, char *>;

        std::unordered_map<std::string, std::unique_ptr<BlockType>, transparent_string_hash, std::equal_to<>> m_types;
    };

    static Registry &registry();
};

struct DataType {
    enum Id {
        ComplexFloat64,
        ComplexFloat32,
        ComplexInt64,
        ComplexInt32,
        ComplexInt16,
        ComplexInt8,
        Float64,
        Float32,
        DataSetFloat32,
        Int64,
        Int32,
        Int16,
        Int8,
        Bits,
        AsyncMessage,
        BusConnection,
        Wildcard,
        Untyped,
    };

    template<typename T>
    constexpr static DataType of()
    {
        if constexpr (std::is_same_v<T, float>) {
            return Float32;
        } else if constexpr (std::is_same_v<T, double>) {
            return Float64;
        } else if constexpr (std::is_same_v<T, int8_t>) {
            return Int8;
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return Int16;
        } else if constexpr (std::is_same_v<T, int32_t>) {
            return Int32;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            return Int64;
        } else if constexpr (std::is_same_v<T, fair::graph::DataSet<float>>) {
            return DataSetFloat32;
        } else {
            static_assert(!std::is_same_v<T, T>, "DataType of T is not known");
        }
    }

    template<typename F>
    auto asType(F fun)
    {
        switch (m_id) {
            // case ComplexFloat64: return fun.template operator()<std::complex<double>>();
            // case ComplexFloat32: return fun.template operator()<std::complex<float>>();
            // case ComplexInt64: return fun.template operator()<std::complex<int64_t>>();
            // case ComplexInt32: return fun.template operator()<std::complex<int32_t>>();
            // case ComplexInt16: return fun.template operator()<std::complex<int16_t>>();
            // case ComplexInt8: return fun.template operator()<std::complex<int8_t>>();
            case Float64: return fun.template operator()<double>();
            case Float32: return fun.template operator()<float>();
            case DataSetFloat32: return fun.template operator()<fair::graph::DataSet<float>>();
            // case Int64: return fun.template operator()<int64_t>();
            // case Int32: return fun.template operator()<int32_t>();
            // case Int16: return fun.template operator()<int16_t>();
            // case Int8: return fun.template operator()<int8_t>();
            case Bits:
            case AsyncMessage:
            case BusConnection:
            case Wildcard:
            case Untyped: break;
            default: break;
        }
        return decltype(fun.template operator()<float>()){};
    }

    constexpr inline DataType() {}
    constexpr inline DataType(Id id)
        : m_id(id) {}

    const std::string &toString() const;

    inline operator Id() const { return m_id; }

private:
    Id m_id = Id::Untyped;
};

struct EmptyDataSet {};

using DataSetBase = std::variant<EmptyDataSet, std::span<const float>, std::span<const double>, std::reference_wrapper<fair::graph::DataSet<float>>>;
class DataSet : DataSetBase {
public:
    using DataSetBase::DataSetBase;

    inline bool empty() const { return std::holds_alternative<EmptyDataSet>(*this); }

    inline std::span<const float>  asFloat32() const { return std::get<std::span<const float>>(*this); }
    inline std::span<const double> asFloat64() const { return std::get<std::span<const double>>(*this); }
    inline const auto &asDataSetFloat32() const { return std::get<std::reference_wrapper<fair::graph::DataSet<float>>>(*this).get(); }
};

class Block {
public:
    class Port {
    public:
        enum class Kind {
            Input,
            Output,
        };

        Block                    *block;
        const std::string         m_rawType;
        bool                      dataset;
        const Kind                kind;

        DataType                  type;
        std::vector<Connection *> connections;
    };

    class OutputPort : public Port {
    public:
        DataSet dataSet;
    };

    struct EnumParameter {
        const BlockType::EnumParameter &definition;
        int                             optionIndex;

        std::string                     toString() const;

        inline EnumParameter           &operator=(const EnumParameter &p) {
            optionIndex = p.optionIndex;
            return *this;
        }
    };
    template<typename T>
    struct NumberParameter {
        T value;
    };
    struct RawParameter {
        std::string value;
    };

    struct Parameter : std::variant<EnumParameter, NumberParameter<int>, NumberParameter<float>, RawParameter> {
        using Super = std::variant<EnumParameter, NumberParameter<int>, NumberParameter<float>, RawParameter>;

        using Super::Super;
        std::string toString() const;
    };

    // using ParameterValue = std::variant<std::string, int, float>;

    Block(std::string_view name, std::string_view id, BlockType *type);
    virtual ~Block() {}

    const auto                   &inputs() const { return m_inputs; }
    const auto                   &outputs() const { return m_outputs; }

    // ParameterValue                getParameterValue(const std::string &par) const;

    void                          setParameter(const std::string &name, const pmtv::pmt &par);
    const auto                   &parameters() const { return m_parameters; }

    void                          update();
    void                          updateInputs();

    // virtual void                  processData() {}
    virtual std::unique_ptr<fair::graph::node_model> createGraphNode() = 0;

    inline FlowGraph             *flowGraph() const { return m_flowGraph; }
    const BlockType              *type;
    const std::string             name;
    const std::string             id;

    // protected:
    auto &inputs() { return m_inputs; }
    auto &outputs() { return m_outputs; }

protected:
    std::vector<Port>       m_inputs;
    std::vector<Port>         m_outputs;
    fair::graph::property_map m_parameters;
    bool                    m_updated = false;
    FlowGraph              *m_flowGraph;
    fair::graph::node_model  *m_node = nullptr;

    friend FlowGraph;
};

namespace meta
{

template<template<typename> typename T, typename D>
concept is_creatable = requires {
    { new T<D> };
};

}

class Connection {
public:
    struct {
        Block      *block;
        std::size_t index;
    } src, dst;

private:
    inline Connection(Block *s, std::size_t srcIndex, Block *d, std::size_t dstIndex)
        : src{s, srcIndex}, dst{d, dstIndex} {}

    friend FlowGraph;
};

template<template<typename> typename T>
class DefaultGPBlock : public Block
{
public:
    DefaultGPBlock(std::string_view typeName, BlockType *t)
        : Block(typeName, t->name, t) {
        T<float> node;
        node.settings().update_active_parameters();
        m_parameters = node.settings().get();
    }

    std::unique_ptr<fair::graph::node_model> createGraphNode() final
    {
        DataType t = DataType::Float32;
        if (inputs().size() > 0) {
            if (auto &in = inputs().front(); in.connections.size() > 0) {
                auto &src = in.connections[0]->src;
                t = src.block->outputs()[src.index].type;
            }
        }
        return t.asType([]<typename D>() -> std::unique_ptr<fair::graph::node_model> {
            if constexpr (meta::is_creatable<T, D>) {
                return std::make_unique<fair::graph::node_wrapper<T<D>>>();
            } else {
                return nullptr;
            }
        });
    }
};

class FlowGraph {
public:
    void                         parse(const std::filesystem::path &file);
    void                         parse(const std::string &str);
    void                         clear();

    Block                       *findBlock(std::string_view name) const;
    Block                       *findSourceBlock(std::string_view name) const;
    Block                       *findSinkBlock(std::string_view name) const;

    inline const auto           &blocks() const { return m_blocks; }
    inline const auto           &sourceBlocks() const { return m_sourceBlocks; }
    inline const auto           &sinkBlocks() const { return m_sinkBlocks; }
    inline const auto           &connections() const { return m_connections; }

    void                         addBlock(std::unique_ptr<Block> &&block);
    void                         deleteBlock(Block *block);

    void                         addSourceBlock(std::unique_ptr<Block> &&block);
    void                         addSinkBlock(std::unique_ptr<Block> &&block);

    Connection                  *connect(Block::Port *a, Block::Port *b);

    void                         disconnect(Connection *c);

    void                         update();

    inline bool                  graphChanged() const { return m_graphChanged; }
    fair::graph::graph           createGraph();

    int                          save(std::ostream &stream);
    void                         addRemoteSource(std::string_view uri);
    void                         registerRemoteSource(std::unique_ptr<BlockType> &&type, std::string_view uri);

    std::function<void(Block *)> sourceBlockAddedCallback;
    std::function<void(Block *)> sinkBlockAddedCallback;
    std::function<void(Block *)> blockDeletedCallback;

private:
    std::vector<std::unique_ptr<Block>> m_sourceBlocks;
    std::vector<std::unique_ptr<Block>> m_sinkBlocks;
    std::vector<std::unique_ptr<Block>> m_blocks;
    plf::colony<Connection>             m_connections; // We're using plf::colony because it guarantees pointer/iterator stability
    struct RemoteSource {
        BlockType  *type;
        std::string uri;
    };
    std::vector<RemoteSource> m_remoteSources;
    bool                      m_graphChanged = true;

    // TODO add remote sources here?
};

} // namespace DigitizerUi
