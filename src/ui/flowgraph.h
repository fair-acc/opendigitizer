#pragma once

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <URI.hpp>

namespace DigitizerUi {

class BlockType {
public:
    struct PortDefinition {
        std::string type;
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

    std::vector<Parameter>      parameters;
    std::vector<PortDefinition> inputs;
    std::vector<PortDefinition> outputs;
};

class Block {
public:
    class Connection {
    public:
        Block   *block;
        uint32_t portNumber;
    };

    class Port {
    public:
        const std::string       m_rawType;

        const uint32_t          id;
        std::string             type;
        std::vector<Connection> connections;
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

    using Parameter      = std::variant<EnumParameter, IntParameter, RawParameter>;

    using ParameterValue = std::variant<std::string, int>;

    Block(std::string_view name, BlockType *type);

    void              connectTo(uint32_t srcPort, Block *dst, uint32_t dstPort);

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

    void               addBlock(std::unique_ptr<Block> &&block);

private:
    std::vector<std::unique_ptr<Block>>                         m_blocks;
    std::unordered_map<std::string, std::unique_ptr<BlockType>> m_types;
};

} // namespace ImChart
