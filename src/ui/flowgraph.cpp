#include "flowgraph.h"

#include <assert.h>

#include <charconv>
#include <fstream>
#include <string_view>
#include <iostream>

#include <opencmw.hpp>
#include <RestClient.hpp>
#include <majordomo/Message.hpp>
#include <MdpMessage.hpp>
#include <IoSerialiserJson.hpp>

#include <yaml-cpp/yaml.h>

struct Reply {
    std::string flowgraph;
};
ENABLE_REFLECTION_FOR(Reply, flowgraph)

namespace DigitizerUi {

namespace {

uint32_t nextId() {
    static uint32_t id = 0;
    return ++id;
}

} // namespace

Block::Block(std::string_view name, BlockType *type)
    : id(nextId())
    , type(type)
    , name(name) {
    if (!type) {
        return;
    }

    m_parameters.reserve(type->parameters.size());
    for (auto &p : type->parameters) {
        if (auto *e = std::get_if<BlockType::EnumParameter>(&p.impl)) {
            m_parameters.push_back(Block::EnumParameter{ *e, 0 });
        } else if (auto *i = std::get_if<BlockType::IntParameter>(&p.impl)) {
            m_parameters.push_back(Block::IntParameter{ 0 });
        } else if (auto *r = std::get_if<BlockType::RawParameter>(&p.impl)) {
            m_parameters.push_back(Block::RawParameter{ r->defaultValue });
        }
    }

    m_outputs.reserve(type->outputs.size());
    m_inputs.reserve(type->inputs.size());
    for (auto &o : type->outputs) {
        m_outputs.push_back({ o.type, nextId() });
    }
    for (auto &o : type->inputs) {
        m_inputs.push_back({ o.type, nextId() });
    }
}

void Block::connectTo(uint32_t srcPort, Block *dst, uint32_t dstPort) {
    if (srcPort < m_outputs.size() && dst->inputs().size() > dstPort) {
        m_outputs[srcPort].connections.push_back({ dst, dstPort });
    }
}

Block::ParameterValue Block::getParameterValue(const std::string &str) const {
    std::string words[2];
    int         numWords = 0;

    int         start    = 0;
    int         last     = str.size() - 1;
    while (str[start] == ' ') {
        ++start;
    }
    while (str[last] == ' ') {
        --last;
    }

    while (start < last && numWords <= 2) {
        int i = str.find('.', start);
        if (i == std::string::npos) {
            i = last + 1;
        }

        int end = i - 1;

        while (str[start] == ' ' && start < end) {
            ++start;
        }
        while (str[end] == ' ' && end > start) {
            --end;
        }

        words[numWords++] = str.substr(start, end - start + 1);
        start             = i + 1;
    }

    if (numWords > 0) {
        for (int i = 0; i < m_parameters.size(); ++i) {
            const auto &p = type->parameters[i];
            if (p.id != words[0]) {
                continue;
            }

            const auto &bp = m_parameters[i];
            if (numWords == 1) {
                switch (p.impl.index()) {
                case 0: return std::get<BlockType::EnumParameter>(p.impl).options[std::get<Block::EnumParameter>(bp).optionIndex];
                case 1: return std::get<Block::IntParameter>(bp).value;
                default:
                    assert(0);
                    break;
                }
            } else {
                assert(p.impl.index() == 0);
                const auto &e  = std::get<BlockType::EnumParameter>(p.impl);
                auto        it = e.optionsAttributes.find(words[1]);
                if (it != e.optionsAttributes.end()) {
                    const auto &attr = it->second;
                    return attr[std::get<Block::EnumParameter>(bp).optionIndex];
                }
            }
        }
    }
    std::cerr << "Failed parsing parameter '" << str << "' for block '" << name << "'\n";
    assert(0);
    return 0;
}

void Block::setParameter(int index, const Parameter &p) {
    if (p.index() != m_parameters[index].index()) {
        assert(0);
        return;
    }

    m_parameters[index] = p;
}

void Block::update() {
    auto getType = [&](const std::string &t) {
        if (t.starts_with("${") && t.ends_with("}")) {
            auto val = getParameterValue(t.substr(2, t.size() - 3));
            if (auto type = std::get_if<std::string>(&val)) {
                return *type;
            }
        }
        return t;
    };
    for (auto &in : m_inputs) {
        in.type = getType(in.m_rawType);
    }
    for (auto &out : m_outputs) {
        out.type = getType(out.m_rawType);
    }
}

std::string Block::EnumParameter::toString() const {
    return definition.optionsLabels[optionIndex];
}

static std::string_view strview(auto &&s) {
    return { s.data(), s.size() };
}

static bool readFile(const std::filesystem::path &file, std::string &str) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        return false;
    }

    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();

    str.resize(size);
    stream.seekg(0);
    stream.read(str.data(), size);
    return true;
}

void FlowGraph::loadBlockDefinitions(const std::filesystem::path &dir) {
    if (!std::filesystem::exists(dir)) {
        std::cerr << "Cannot open directory '" << dir << "'\n";
        return;
    }

    std::filesystem::directory_iterator iterator(dir);
    std::string                         str;

    for (const auto &entry : iterator) {
        const auto &path = entry.path();
        if (!path.native().ends_with(".block.yml")) {
            continue;
        }

        readFile(path, str);
        YAML::Node config = YAML::Load(str);

        auto       id     = config["id"].as<std::string>();

        auto def        = m_types.insert({ id, std::make_unique<BlockType>() }).first->second.get();

        auto parameters = config["parameters"];
        for (const auto &p : parameters) {
            const auto &idNode = p["id"];
            if (!idNode || !idNode.IsScalar()) {
                continue;
            }

            auto dtypeNode = p["dtype"];
            if (!dtypeNode || !dtypeNode.IsScalar()) {
                continue;
            }

            auto dtype = dtypeNode.as<std::string>();
            auto id        = idNode.as<std::string>();
            auto labelNode = p["label"];
            auto defaultNode = p["default"];
            auto label     = labelNode && labelNode.IsScalar() ? labelNode.as<std::string>(id) : id;

            if (dtype == "enum") {
                const auto              &opts = p["options"];
                std::vector<std::string> options;
                if (opts && opts.IsSequence()) {
                    for (const auto &n : opts) {
                        options.push_back(n.as<std::string>());
                    }
                }

                BlockType::EnumParameter par{ int(options.size()) };
                par.options       = std::move(options);

                par.optionsLabels = par.options;

                const auto &attrs = p["option_attributes"];
                if (attrs && attrs.IsMap()) {
                    for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                        auto                     key = it->first.as<std::string>();
                        std::vector<std::string> vals;
                        for (const auto &n : it->second) {
                            vals.push_back(n.as<std::string>());
                        }

                        if (par.size != vals.size()) {
                            std::cerr << "Malformed parameter.\n";
                            continue;
                        }

                        par.optionsAttributes.insert({ key, std::move(vals) });
                    }
                }

                def->parameters.push_back(BlockType::Parameter{ id, label, std::move(par) });
            } else if (dtype == "int") {
                auto defaultValue = defaultNode ? defaultNode.as<int>(0) : 0;
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::IntParameter{ defaultValue } });
            } else if (dtype == "raw") {
                auto defaultValue = defaultNode ? defaultNode.as<std::string>(std::string{}) : "";
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::RawParameter{ defaultValue } });
            }
        }

        auto outputs = config["outputs"];
        for (const auto &o : outputs) {
            auto        n    = o["dtype"];
            std::string type = "unknown";
            if (n) {
                type = n.as<std::string>();
            }
            def->outputs.push_back({ type });
        }

        auto inputs = config["inputs"];
        for (const auto &o : inputs) {
            auto        n    = o["dtype"];
            std::string type = "unknown";
            if (n) {
                type = n.as<std::string>();
            }
            def->inputs.push_back({ type });
        }
    }
}

void FlowGraph::parse(const opencmw::URI<opencmw::STRICT> &uri)
{
    opencmw::client::RestClient client;

    std::atomic<bool> done(false);
    opencmw::client::Command command;
    command.command  = opencmw::mdp::Command::Get;
    command.endpoint = uri;

    std::string result;

    // command.data     = std::move(data);
    command.callback = [&](const opencmw::mdp::Message &rep) {
        auto buf = rep.data;

        Reply reply;
        opencmw::deserialise<opencmw::Json, opencmw::ProtocolCheck::LENIENT>(buf, reply);
        // result = opencmw::json::readString(buf);
        result = std::move(reply.flowgraph);

        done.store(true, std::memory_order_release);
        done.notify_all();
    };
    client.request(command);

    done.wait(false);

    parse(result);
}

void FlowGraph::parse(const std::filesystem::path &file) {
    std::string str;
    if (!readFile(file, str)) {
        std::cerr << "Cannot read file '" << file << "'\n";
        return;
    }
    parse(str);
}

void FlowGraph::parse(const std::string &str) {
    m_blocks.clear();

    YAML::Node tree   = YAML::Load(str);

    auto       blocks = tree["blocks"];
    for (const auto &b : blocks) {
        auto n    = b["name"].as<std::string>();
        auto id   = b["id"].as<std::string>();

        std::cout<<"b"<<n<<id<<"\n";

        auto type = m_types[id].get();
        if (!type) {
            std::cerr << "Block type '" << id << "' is unkown.\n";
        }
        m_blocks.push_back(std::make_unique<Block>(n, type));
        auto *block = m_blocks.back().get();

        if (!type) {
            continue;
        }

        auto pars = b["parameters"];
        if (pars && pars.IsMap()) {
            for (auto it = pars.begin(); it != pars.end(); ++it) {
                auto key            = it->first.as<std::string>();
                int  parameterIndex = -1;
                for (int i = 0; i < type->parameters.size(); ++i) {
                    if (type->parameters[i].id == key) {
                        parameterIndex = i;
                        break;
                    }
                }
                if (parameterIndex != -1) {
                    auto &p              = type->parameters[parameterIndex];
                    auto &blockParameter = block->m_parameters[parameterIndex];
                    switch (p.impl.index()) {
                    case 0: {
                        auto        option = it->second.as<std::string>();
                        const auto &e      = std::get<BlockType::EnumParameter>(p.impl);
                        for (int i = 0; i < e.options.size(); ++i) {
                            if (e.options[i] == option) {
                                std::get<Block::EnumParameter>(blockParameter).optionIndex = i;
                                break;
                            }
                        }
                        break;
                    }
                    case 1: {
                        std::get<Block::IntParameter>(blockParameter).value = it->second.as<int>();
                        break;
                    }
                    case 2:
                        std::get<Block::RawParameter>(blockParameter).value = it->second.as<std::string>();
                        break;
                    }
                }
            }
        }

        block->update();
    }

    auto connections = tree["connections"];
    for (const auto &c : connections) {
        assert(c.size() == 4);

        auto srcBlockName = c[0].as<std::string>();
        auto srcPortStr   = c[1].as<std::string>();
        int  srcPort;
        std::from_chars(srcPortStr.data(), srcPortStr.data() + srcPortStr.size(), srcPort);

        auto dstBlockName = c[2].as<std::string>();
        auto dstPortStr   = c[3].as<std::string>();
        int  dstPort;
        std::from_chars(dstPortStr.data(), dstPortStr.data() + dstPortStr.size(), dstPort);

        auto srcBlock = findBlock(srcBlockName);
        assert(srcBlock);
        auto dstBlock = findBlock(dstBlockName);
        assert(dstBlock);

        srcBlock->connectTo(srcPort, dstBlock, dstPort);
    }
}

Block *FlowGraph::findBlock(std::string_view name) const {
    for (auto &b : m_blocks) {
        if (b->name == name) {
            return b.get();
        }
    }
    return nullptr;
}

Block *FlowGraph::findBlock(uint32_t id) const {
    for (auto &b : m_blocks) {
        if (b->id == id) {
            return b.get();
        }
    }
    return nullptr;
}

void FlowGraph::addBlock(std::unique_ptr<Block> &&block) {
    block->update();
    m_blocks.push_back(std::move(block));
}

} // namespace ImChart
