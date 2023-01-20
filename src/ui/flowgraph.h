#pragma once

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <plf_colony.h>

#include <URI.hpp>

namespace DigitizerUi {

class FlowGraph;
class Connection;

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
        const std::string                         id;
        const std::string                         label;
        std::variant<EnumParameter, IntParameter, RawParameter> impl;
    };

    inline BlockType(const std::string &n) : name(n) {}


    const std::string name;
    std::vector<Parameter>      parameters;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;
};

struct DataType
{
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
    inline DataType(Id id) : m_id(id) {}

    const std::string &toString() const;

    inline operator Id() const { return m_id; }

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

        const std::string       m_rawType;
        const Kind              kind;

        DataType                type;
        std::vector<Connection *> connections;
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

    struct Parameter : std::variant<EnumParameter, IntParameter, RawParameter>
    {
        using Super = std::variant<EnumParameter, IntParameter, RawParameter>;

        using Super::Super;
        std::string toString() const;
    };

    using ParameterValue = std::variant<std::string, int>;

    Block(std::string_view name, BlockType *type);

    const auto       &inputs() const { return m_inputs; }
    const auto       &outputs() const { return m_outputs; }

    ParameterValue    getParameterValue(const std::string &par) const;

    void                          setParameter(int index, const Parameter &par);
    const std::vector<Parameter> &parameters() const { return m_parameters; }

    void                          update();

    const uint32_t    id;
    const BlockType  *type;
    const std::string name;

    std::vector<Port> m_inputs;
    std::vector<Port> m_outputs;
    std::vector<Parameter> m_parameters;
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
    void               loadBlockDefinitions(const std::filesystem::path &dir);
    void               parse(const std::filesystem::path &file);
    void               parse(const std::string &str);
    void parse(const opencmw::URI<opencmw::STRICT> &uri);

    Block             *findBlock(std::string_view name) const;
    Block             *findBlock(uint32_t id) const;

    inline const auto &blocks() const { return m_blocks; }
    inline const auto &blockTypes() const { return m_types; }
    inline const auto &connections() const { return m_connections; }

    void               addBlock(std::unique_ptr<Block> &&block);
    void               deleteBlock(Block *block);

    void               connect(Block::Port *a, Block::Port *b);

    void               disconnect(Connection *c);

private:
    std::vector<std::unique_ptr<Block>>                         m_blocks;
    std::unordered_map<std::string, std::unique_ptr<BlockType>> m_types;
    plf::colony<Connection>                                     m_connections; // We're using plf::colony because it guarantees pointer/iterator stability
};

} // namespace ImChart
