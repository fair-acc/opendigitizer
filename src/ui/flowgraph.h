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

namespace DigitizerUi {

class FlowGraph;
class Connection;
class Block;

class BlockType {
public:
    struct PortDefinition {
        std::string type;
        std::string name;
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
    struct RawParameter {
        std::string defaultValue;
    };
    struct Parameter {
        const std::string                                                                       id;
        const std::string                                                                       label;
        std::variant<EnumParameter, NumberParameter<int>, NumberParameter<float>, RawParameter> impl;
    };

    explicit BlockType(std::string_view n, std::string_view label = {}, std::string_view cat = {}, bool source = false);

    const std::string                                       name;
    const std::string                                       label;
    std::vector<Parameter>                                  parameters;
    std::vector<PortDefinition>                             inputs;
    std::vector<PortDefinition>                             outputs;
    const std::string                                       category;
    const bool                                              isSource;

    std::function<std::unique_ptr<Block>(std::string_view)> createBlock;

    struct Registry {
        void               loadBlockDefinitions(const std::filesystem::path &dir);
        void               addBlockType(std::unique_ptr<BlockType> &&t);
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

    inline DataType() {}
    inline DataType(Id id)
        : m_id(id) {}

    const std::string &toString() const;

    inline operator Id() const { return m_id; }

private:
    Id m_id = Id::Untyped;
};

struct EmptyDataSet {};
class DataSet : public std::variant<EmptyDataSet, std::span<const float>, std::span<const double>> {
public:
    using Super = std::variant<EmptyDataSet, std::span<const float>, std::span<const double>>;
    using Super::Super;

    inline bool empty() const { return std::holds_alternative<EmptyDataSet>(*this); }

    inline auto asFloat32() const { return std::get<std::span<const float>>(*this); }
    inline auto asFloat64() const { return std::get<std::span<const double>>(*this); }
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

    using ParameterValue = std::variant<std::string, int, float>;

    Block(std::string_view name, std::string_view id, BlockType *type);
    virtual ~Block() {}

    const auto                   &inputs() const { return m_inputs; }
    const auto                   &outputs() const { return m_outputs; }

    ParameterValue                getParameterValue(const std::string &par) const;

    void                          setParameter(int index, const Parameter &par);
    const std::vector<Parameter> &parameters() const { return m_parameters; }

    void                          update();
    void                          updateInputs();

    virtual void                  processData() {}

    inline FlowGraph             *flowGraph() const { return m_flowGraph; }
    const BlockType              *type;
    const std::string             name;
    const std::string             id;

    // protected:
    auto &inputs() { return m_inputs; }
    auto &outputs() { return m_outputs; }

private:
    std::vector<Port>       m_inputs;
    std::vector<OutputPort> m_outputs;
    std::vector<Parameter>  m_parameters;
    bool                    m_updated = false;
    FlowGraph              *m_flowGraph;

    friend FlowGraph;
};

class Connection {
public:
    Block::Port *const ports[2];

private:
    inline Connection(Block::Port *a, Block::Port *b)
        : ports{ a, b } {}

    friend FlowGraph;
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

    // TODO add remote sources here?
};

} // namespace DigitizerUi
