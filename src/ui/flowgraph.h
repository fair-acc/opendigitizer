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

#include <URI.hpp>

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
    struct IntParameter {
        int defaultValue;
    };
    struct RawParameter {
        std::string defaultValue;
    };
    struct Parameter {
        const std::string                                       id;
        const std::string                                       label;
        std::variant<EnumParameter, IntParameter, RawParameter> impl;
    };

    inline BlockType(const std::string &n)
        : name(n) {}

    const std::string                                       name;
    std::vector<Parameter>                                  parameters;
    std::vector<PortDefinition>                             inputs;
    std::vector<PortDefinition>                             outputs;

    std::function<std::unique_ptr<Block>(std::string_view)> createBlock;
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

    inline             operator Id() const { return m_id; }

private:
    Id m_id = Id::Untyped;
};

class DataSet : public std::variant<std::span<const float>, std::span<const double>> {
public:
    using Super = std::variant<std::span<const float>, std::span<const double>>;
    using Super::Super;

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
    struct IntParameter {
        int value;
    };
    struct RawParameter {
        std::string value;
    };

    struct Parameter : std::variant<EnumParameter, IntParameter, RawParameter> {
        using Super = std::variant<EnumParameter, IntParameter, RawParameter>;

        using Super::Super;
        std::string toString() const;
    };

    using ParameterValue = std::variant<std::string, int>;

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

    const BlockType              *type;
    const std::string             name;
    const std::string             id;

protected:
    auto &outputs() { return m_outputs; }

private:
    std::vector<Port>       m_inputs;
    std::vector<OutputPort> m_outputs;
    std::vector<Parameter>  m_parameters;
    bool                    m_updated = false;

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
    void                         loadBlockDefinitions(const std::filesystem::path &dir);
    void                         parse(const std::filesystem::path &file);
    void                         parse(const std::string &str);
    void                         parse(const opencmw::URI<opencmw::STRICT> &uri);

    Block                       *findBlock(std::string_view name) const;

    inline const auto           &blocks() const { return m_blocks; }
    inline const auto           &sourceBlocks() const { return m_sourceBlocks; }
    inline const auto           &sinkBlocks() const { return m_sinkBlocks; }
    inline const auto           &blockTypes() const { return m_types; }
    inline const auto           &connections() const { return m_connections; }

    void                         addBlockType(std::unique_ptr<BlockType> &&t);

    void                         addBlock(std::unique_ptr<Block> &&block);
    void                         deleteBlock(Block *block);

    void                         addSourceBlock(std::unique_ptr<Block> &&block);
    void                         addSinkBlock(std::unique_ptr<Block> &&block);

    void                         connect(Block::Port *a, Block::Port *b);

    void                         disconnect(Connection *c);

    void                         update();

    void                         save();

    std::function<void(Block *)> blockDeletedCallback;

private:
    std::vector<std::unique_ptr<Block>>                         m_sourceBlocks;
    std::vector<std::unique_ptr<Block>>                         m_sinkBlocks;
    std::vector<std::unique_ptr<Block>>                         m_blocks;
    std::unordered_map<std::string, std::unique_ptr<BlockType>> m_types;
    plf::colony<Connection>                                     m_connections; // We're using plf::colony because it guarantees pointer/iterator stability
};

} // namespace DigitizerUi