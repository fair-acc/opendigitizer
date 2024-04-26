#pragma once

#include <filesystem>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#ifdef EMSCRIPTEN
#include "emscripten_compat.hpp"
#endif
#include <plf_colony.h>

#include <fmt/format.h>
#include <gnuradio-4.0/BlockTraits.hpp>
#include <gnuradio-4.0/Graph.hpp>
#include <gnuradio-4.0/PluginLoader.hpp>

#include "blocks/meta.hpp"

namespace DigitizerUi {

class FlowGraph;
class Connection;
class Block;

class BlockType {
public:
    struct PortDefinition {
        std::string type;
        std::string name;
        bool        dataset = false;
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
        const std::string                                                                          id;
        const std::string                                                                          label;
        std::variant<EnumParameter, NumberParameter<int>, NumberParameter<float>, StringParameter> impl;
    };

    explicit BlockType(std::string_view name_, std::string_view label = {}, std::string_view cat = {});

    std::unique_ptr<Block>      createBlock(std::string_view name) const;

    const std::string           name;
    const std::string           label;
    std::vector<Parameter>      parameters;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;
    const std::string           category;
    gr::property_map            defaultParameters;

    auto                        data_inputs() {
        return inputs | std::views::filter([](const PortDefinition &p) { return p.type != "message"; });
    }
    auto message_inputs() {
        return inputs | std::views::filter([](const PortDefinition &p) { return p.type == "message"; });
    }
    auto data_outputs() {
        return outputs | std::views::filter([](const PortDefinition &p) { return p.type != "message"; });
    }
    auto message_outputs() {
        return outputs | std::views::filter([](const PortDefinition &p) { return p.type == "message"; });
    }

    bool isSource() const {
        return inputs.empty() && !outputs.empty();
    }

    bool isSink() const {
        return !inputs.empty() && outputs.empty();
    }

    bool isPlotSink() const {
        // TODO make this smarter once metaInformation() is statically available
        return name == "opendigitizer::ImPlotSink";
    }

    template<typename T>
    void initPort(auto &vec) {
        vec.push_back({});
        auto &p = vec.back();
        p.name  = T::static_name();
        p.type  = T::kPortType == gr::PortType::STREAM ? "float" : "message";
        if (opendigitizer::meta::is_dataset_v<typename T::value_type>) {
            p.dataset = true;
        }
    }

    struct Registry {
        void loadBlockDefinitions(const std::filesystem::path &dir);
        void addBlockType(std::unique_ptr<BlockType> &&t);

        // automatically create a BlockType from a graph prototype node template class
        template<template<typename...> typename T>
        void addBlockType(std::string_view typeName) {
            using Node           = T<float>;
            auto t               = std::make_unique<DigitizerUi::BlockType>(typeName);
            t->defaultParameters = [] {
                Node instance;
                instance.settings().applyStagedParameters();
                return instance.settings().get();
            }();
            namespace meta                         = gr::traits::block;

            constexpr std::size_t input_port_count = meta::template all_input_port_types<Node>::size;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (t->template initPort<typename meta::template all_input_ports<Node>::template at<Is>>(t->inputs), ...);
            }(std::make_index_sequence<input_port_count>());

            constexpr std::size_t output_port_count = meta::template all_output_port_types<Node>::size;
            [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                (t->template initPort<typename meta::template all_output_ports<Node>::template at<Is>>(t->outputs), ...);
            }(std::make_index_sequence<output_port_count>());

            addBlockType(std::move(t));
        }

        const BlockType   *get(std::string_view id) const;

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

    static constexpr std::string_view name(Id id) {
        switch (id) {
        case ComplexFloat64: return "std::complex<double>";
        case ComplexFloat32: return "std::complex<float>";
        case ComplexInt64: return "std::complex<int64_t>";
        case ComplexInt32: return "std::complex<int32_t>";
        case ComplexInt16: return "std::complex<int16_t>";
        case ComplexInt8: return "std::complex<int8_t>";
        case Float64: return "double";
        case Float32: return "float";
        case DataSetFloat32: return "gr::DataSet_float";
        case Int64: return "int64_t";
        case Int32: return "int32_t";
        case Int16: return "int16_t";
        case Int8: return "int8_t";
        }
        return "unknown";
    }

    template<typename T>
    constexpr static DataType of() {
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
        } else if constexpr (std::is_same_v<T, std::complex<double>>) {
            return ComplexFloat64;
        } else if constexpr (std::is_same_v<T, std::complex<float>>) {
            return ComplexFloat32;
        } else if constexpr (std::is_same_v<T, std::complex<int64_t>>) {
            return ComplexInt64;
        } else if constexpr (std::is_same_v<T, std::complex<int32_t>>) {
            return ComplexInt32;
        } else if constexpr (std::is_same_v<T, std::complex<int16_t>>) {
            return ComplexInt16;
        } else if constexpr (std::is_same_v<T, std::complex<int8_t>>) {
            return ComplexInt8;
        } else if constexpr (std::is_same_v<T, gr::DataSet<float>>) {
            return DataSetFloat32;
        } else {
            static_assert(!std::is_same_v<T, T>, "DataType of T is not known");
        }
    }

    template<typename F>
    auto asType(F fun) {
        switch (m_id) {
        case ComplexFloat64: return fun.template operator()<std::complex<double>>();
        case ComplexFloat32: return fun.template operator()<std::complex<float>>();
        // unsupported by graph-prototype
        // case ComplexInt64: return fun.template operator()<std::complex<int64_t>>();
        // case ComplexInt32: return fun.template operator()<std::complex<int32_t>>();
        // case ComplexInt16: return fun.template operator()<std::complex<int16_t>>();
        // case ComplexInt8: return fun.template operator()<std::complex<int8_t>>();
        case Float64: return fun.template operator()<double>();
        case Float32: return fun.template operator()<float>();
        case DataSetFloat32: return fun.template operator()<gr::DataSet<float>>();
        // case Int64: return fun.template operator()<int64_t>();
        case Int32: return fun.template operator()<int32_t>();
        case Int16: return fun.template operator()<int16_t>();
        case Int8: return fun.template operator()<int8_t>();
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

    inline             operator Id() const { return m_id; }

private:
    Id m_id = Id::Untyped;
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

    explicit Block(std::string_view name, const BlockType *type, gr::property_map settings = {});

    const BlockType &type() const {
        return *m_type;
    }

    std::string_view typeName() const {
        return m_type->name;
    }

    const auto &inputs() const { return m_inputs; }
    const auto &outputs() const { return m_outputs; }
    auto        dataInputs() const {
        return m_inputs | std::views::filter([](const Port &p) { return p.type != DataType::AsyncMessage; });
    }
    auto dataOutputs() const {
        return m_outputs | std::views::filter([](const Port &p) { return p.type != DataType::AsyncMessage; });
    }
    auto messageInputs() const {
        return m_inputs | std::views::filter([](const Port &p) { return p.type == DataType::AsyncMessage; });
    }
    auto messageOutputs() const {
        return m_outputs | std::views::filter([](const Port &p) { return p.type == DataType::AsyncMessage; });
    }

    void              setParameter(const std::string &name, const pmtv::pmt &par);
    const auto       &parameters() const { return m_parameters; }

    void              update();

    inline FlowGraph *flowGraph() const { return m_flowGraph; }
    const std::string name;

    // protected:
    auto                   &inputs() { return m_inputs; }
    auto                   &outputs() { return m_outputs; }

    void                    updateSettings(const gr::property_map &settings);
    const gr::property_map &metaInformation() const { return m_metaInformation; }

protected:
    std::vector<Port> m_inputs;
    std::vector<Port> m_outputs;
    gr::property_map  m_parameters;
    bool              m_updated   = false;
    FlowGraph        *m_flowGraph = nullptr;
    const BlockType  *m_type;
    std::string       m_uniqueName;
    gr::property_map  m_metaInformation;
    friend FlowGraph;
};

namespace meta {

template<template<typename...> typename T, typename D>
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
        : src{ s, srcIndex }, dst{ d, dstIndex } {}

    friend FlowGraph;
};

struct ExecutionContext {
    gr::Graph                                         graph;
    std::unordered_map<std::string, gr::BlockModel *> plotSinkGrBlocks;
    std::vector<gr::BlockModel *>                     toolbarBlocks;
};

class FlowGraph {
public:
    FlowGraph();
    void               parse(const std::filesystem::path &file);
    void               parse(const std::string &str);
    void               clear();

    Block             *findBlock(std::string_view name) const;

    inline const auto &blocks() const { return m_blocks; }
    inline const auto &connections() const { return m_connections; }

    void               addBlock(std::unique_ptr<Block> &&block);
    void               deleteBlock(Block *block);

    Connection        *connect(Block::Port *a, Block::Port *b);

    void               disconnect(Connection *c);

    inline bool        graphChanged() const { return m_graphChanged; }
    ExecutionContext   createExecutionContext();

    const std::string &grc() const { return m_grc; }

    int                save(std::ostream &stream);
    void               addRemoteSource(std::string_view uri);

    void               handleMessage(const gr::Message &msg);

    void               setPlotSinkGrBlocks(std::unordered_map<std::string, gr::BlockModel *> plotSinkGrBlocks) {
        m_plotSinkGrBlocks = std::move(plotSinkGrBlocks);
    }

    std::function<void(Block *)> plotSinkBlockAddedCallback;
    std::function<void(Block *)> blockDeletedCallback;

    template<typename F>
    void forEachBlock(F &&f) {
        for (auto &b : m_blocks) {
            if (!f(b)) return;
        }
    }

    gr::BlockModel *findPlotSinkGrBlock(std::string_view name) const {
        const auto it = m_plotSinkGrBlocks.find(std::string(name));
        if (it != m_plotSinkGrBlocks.end()) {
            return it->second;
        }
        return nullptr;
    }

private:
    gr::PluginLoader                                  _pluginLoader;
    std::vector<std::unique_ptr<Block>>               m_blocks;
    std::unordered_map<std::string, gr::BlockModel *> m_plotSinkGrBlocks;
    plf::colony<Connection>                           m_connections; // We're using plf::colony because it guarantees pointer/iterator stability
    bool                                              m_graphChanged = true;
    std::string                                       m_grc;

    // TODO add remote sources here?
};

} // namespace DigitizerUi
