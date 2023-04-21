#include "flowgraph.h"

#include <assert.h>

#include <charconv>
#include <fstream>
#include <iostream>
#include <string_view>

#include <IoSerialiserJson.hpp>
#include <MdpMessage.hpp>
#include <opencmw.hpp>
#include <RestClient.hpp>

#include "flowgraph/remotedatasource.h"
#include "yamlutils.h"
#include <yaml-cpp/yaml.h>

struct Reply {
    std::string flowgraph;
};
ENABLE_REFLECTION_FOR(Reply, flowgraph)

namespace DigitizerUi {

const std::string &DataType::toString() const {
    const static std::string names[] = {
        "int32", "float32", "complex float 32"
    };
    return names[m_id];
}

std::string Block::Parameter::toString() const {
    if (auto *e = std::get_if<Block::EnumParameter>(this)) {
        return e->toString();
    } else if (auto *r = std::get_if<Block::RawParameter>(this)) {
        return r->value;
    } else if (auto *i = std::get_if<Block::NumberParameter<int>>(this)) {
        return std::to_string(i->value);
    } else if (auto *i = std::get_if<Block::NumberParameter<float>>(this)) {
        return std::to_string(i->value);
    }
    assert(0);
    return {};
}

BlockType::BlockType(std::string_view n, std::string_view label, std::string_view cat, bool source)
    : name(n)
    , label(label.empty() ? n : label)
    , category(cat)
    , isSource(source)
    , createBlock([this](std::string_view name) { return std::make_unique<Block>(name, this->name, this); }) {
}

Block::Block(std::string_view name, std::string_view id, BlockType *type)
    : type(type)
    , name(name)
    , id(id) {
    if (!type) {
        return;
    }

    m_parameters.reserve(type->parameters.size());
    for (auto &p : type->parameters) {
        if (auto *e = std::get_if<BlockType::EnumParameter>(&p.impl)) {
            m_parameters.push_back(Block::EnumParameter{ *e, 0 });
        } else if (auto *i = std::get_if<BlockType::NumberParameter<int>>(&p.impl)) {
            m_parameters.push_back(Block::NumberParameter<int>{ i->defaultValue });
        } else if (auto *i = std::get_if<BlockType::NumberParameter<float>>(&p.impl)) {
            m_parameters.push_back(Block::NumberParameter<float>{ i->defaultValue });
        } else if (auto *r = std::get_if<BlockType::RawParameter>(&p.impl)) {
            m_parameters.push_back(Block::RawParameter{ r->defaultValue });
        }
    }

    m_outputs.reserve(type->outputs.size());
    m_inputs.reserve(type->inputs.size());
    for (auto &o : type->outputs) {
        m_outputs.push_back({ this, o.type, Port::Kind::Output });
    }
    for (auto &o : type->inputs) {
        m_inputs.push_back({ this, o.type, Port::Kind::Input });
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
                case 1: return std::get<Block::NumberParameter<int>>(bp).value;
                case 2: return std::get<Block::NumberParameter<float>>(bp).value;
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
    auto parseType = [](const std::string &t) -> DataType {
        if (t == "fc64") return DataType::ComplexFloat64;
        if (t == "fc32" || t == "complex") return DataType::ComplexFloat32;
        if (t == "sc64") return DataType::ComplexInt64;
        if (t == "sc32") return DataType::ComplexInt32;
        if (t == "sc16") return DataType::ComplexInt16;
        if (t == "sc8") return DataType::ComplexInt8;
        if (t == "f64") return DataType::Float64;
        if (t == "f32" || t == "float") return DataType::Float32;
        if (t == "s64") return DataType::Int64;
        if (t == "s32" || t == "int") return DataType::Int32;
        if (t == "s16" || t == "short") return DataType::Int16;
        if (t == "s8" || t == "byte") return DataType::Int8;
        if (t == "bit" || t == "bits") return DataType::Bits;
        if (t == "message") return DataType::AsyncMessage;
        if (t == "bus") return DataType::BusConnection;
        if (t == "") return DataType::Wildcard;
        if (t == "untyped") return DataType::Untyped;

        fmt::print("unhandled type {}\n", t);
        assert(0);
        return DataType::Untyped;
    };
    auto getType = [&](const std::string &t) {
        if (t.starts_with("${") && t.ends_with("}")) {
            auto val = getParameterValue(t.substr(2, t.size() - 3));
            if (auto type = std::get_if<std::string>(&val)) {
                return parseType(*type);
            }
        }
        return parseType(t);
    };
    for (auto &in : m_inputs) {
        in.type = getType(in.m_rawType);
    }
    for (auto &out : m_outputs) {
        out.type = getType(out.m_rawType);
    }
}

void Block::updateInputs() {
    for (auto &i : m_inputs) {
        for (auto *c : i.connections) {
            auto *b = c->ports[0]->block;
            if (!b->m_updated) {
                b->updateInputs();
                b->processData();
                b->m_updated = true;
            }
        }
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

        auto       def    = m_types.insert({ id, std::make_unique<BlockType>(id) }).first->second.get();
        def->createBlock  = [def](std::string_view name) { return std::make_unique<Block>(name, def->name, def); };

        auto parameters   = config["parameters"];
        for (const auto &p : parameters) {
            const auto &idNode = p["id"];
            if (!idNode || !idNode.IsScalar()) {
                continue;
            }

            auto dtypeNode = p["dtype"];
            if (!dtypeNode || !dtypeNode.IsScalar()) {
                continue;
            }

            auto dtype       = dtypeNode.as<std::string>();
            auto id          = idNode.as<std::string>();
            auto labelNode   = p["label"];
            auto defaultNode = p["default"];
            auto label       = labelNode && labelNode.IsScalar() ? labelNode.as<std::string>(id) : id;

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
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::NumberParameter<int>{ defaultValue } });
            } else if (dtype == "float") {
                auto defaultValue = defaultNode ? defaultNode.as<float>(0) : 0;
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::NumberParameter<float>{ defaultValue } });
            } else if (dtype == "raw") {
                auto defaultValue = defaultNode ? defaultNode.as<std::string>(std::string{}) : "";
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::RawParameter{ defaultValue } });
            }
        }

        auto outputs = config["outputs"];
        for (const auto &o : outputs) {
            auto        n    = o["dtype"];
            std::string type = "";
            if (n) {
                type = n.as<std::string>();
            }
            std::string name = "out";
            if (auto id = o["id"]) {
                name = id.as<std::string>(name);
            }
            def->outputs.push_back({ type, name });
        }

        auto inputs = config["inputs"];
        for (const auto &o : inputs) {
            auto        n    = o["dtype"];
            std::string type = "";
            if (n) {
                type = n.as<std::string>();
            }
            std::string name = "in";
            if (auto id = o["id"]) {
                name = id.as<std::string>(name);
            }
            def->inputs.push_back({ type, name });
        }
    }
}

void FlowGraph::parse(const opencmw::URI<opencmw::STRICT> &uri) {
#ifndef EMSCRIPTEN
    opencmw::client::RestClient client;

    std::atomic<bool>           done(false);
    opencmw::client::Command    command;
    command.command  = opencmw::mdp::Command::Get;
    command.endpoint = uri;

    std::string result;

    // command.data     = std::move(data);
    command.callback = [&](const opencmw::mdp::Message &rep) {
        auto  buf = rep.data;

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
#endif
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
    clear();

    YAML::Node tree     = YAML::Load(str);

    auto       rsources = tree["remote_sources"];
    if (rsources && rsources.IsSequence()) {
        for (const auto &s : rsources) {
            auto uri        = s["uri"].as<std::string>();
            auto signalName = s["signal_name"].as<std::string>();

            RemoteDataSource::registerBlockType(this, uri, signalName);
        }
    }

    auto blocks = tree["blocks"];
    for (const auto &b : blocks) {
        auto n  = b["name"].as<std::string>();
        auto id = b["id"].as<std::string>();

        std::cout << "b" << n << id << "\n";

        auto type = m_types[id].get();
        if (!type) {
            std::cerr << "Block type '" << id << "' is unkown.\n";

            auto block = std::make_unique<Block>(n, id, type);
            m_blocks.push_back(std::move(block));
            continue;
        }

        auto block = type->createBlock(n);

        auto pars  = b["parameters"];
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
                        std::get<Block::NumberParameter<int>>(blockParameter).value = it->second.as<int>();
                        break;
                    }
                    case 2: {
                        std::get<Block::NumberParameter<float>>(blockParameter).value = it->second.as<float>();
                        break;
                    }
                    case 3:
                        std::get<Block::RawParameter>(blockParameter).value = it->second.as<std::string>();
                        break;
                    }
                }
            }
        }

        if (type->isSource) {
            addSourceBlock(std::move(block));
        } else if (type->outputs.size() == 0 && type->inputs.size() > 0) {
            addSinkBlock(std::move(block));
        } else {
            addBlock(std::move(block));
        }
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

        connect(&srcBlock->m_outputs[srcPort], &dstBlock->m_inputs[dstPort]);
    }
}

void FlowGraph::clear() {
    m_blocks.clear();
    m_sourceBlocks.clear();
    m_sinkBlocks.clear();
    m_connections.clear();
}

int FlowGraph::save(std::ostream &stream) {
    YAML::Emitter out;
    {
        YamlMap root(out);
        root.write("blocks", [&]() {
            YamlSeq blocks(out);

            auto    emitBlock = [&](auto &&b) {
                YamlMap map(out);
                map.write("name", b->name);
                map.write("id", b->id);

                const auto &parameters = b->parameters();
                if (!parameters.empty()) {
                    map.write("parameters", [&]() {
                        YamlMap pars(out);
                        for (int i = 0; i < parameters.size(); ++i) {
                            pars.write(b->type->parameters[i].id, parameters[i].toString());
                        }
                       });
                }
            };

            for (auto &b : m_blocks) {
                emitBlock(b);
            }
            for (auto &b : m_sourceBlocks) {
                emitBlock(b);
            }
            for (auto &b : m_sinkBlocks) {
                emitBlock(b);
            }
        });

        root.write("connections", [&]() {
            YamlSeq connections(out);
            for (const auto &c : m_connections) {
                out << YAML::Flow;
                YamlSeq seq(out);
                auto   *src          = c.ports[0];
                auto   *dst          = c.ports[1];

                auto    getPortIndex = [](Block::Port *port, const auto &ports) {
                    return port - static_cast<const Block::Port *>(ports.data());
                };

                out << src->block->name << getPortIndex(src, src->block->outputs());
                out << dst->block->name << getPortIndex(dst, dst->block->inputs());
            }
        });

        root.write("remote_sources", [&]() {
            YamlSeq sources(out);
            for (const auto &s : m_remoteSources) {
                YamlMap map(out);
                map.write("uri", s.uri);
                // we need to save down the name of the signal (and in the future probably other stuff) because when we load
                // having to wait for information from the servers about all the remote signals used by the flowgraph would
                // not be ideal. Moreover, this way a flowgraph can be loaded even when some signal isn't available at the
                // moment. There will be no data but the flowgraph will load correctly anyway.
                map.write("signal_name", s.type->outputs[0].name);
            }
        });
    }

    stream << out.c_str();
    return out.size();
}

static Block *findBlockImpl(std::string_view name, auto &list) {
    for (auto &b : list) {
        if (b->name == name) {
            return b.get();
        }
    }
    return nullptr;
}

Block *FlowGraph::findBlock(std::string_view name) const {
    if (auto *b = findSourceBlock(name)) {
        return b;
    }
    if (auto *b = findSinkBlock(name)) {
        return b;
    }
    return findBlockImpl(name, m_blocks);
}

Block *FlowGraph::findSinkBlock(std::string_view name) const {
    return findBlockImpl(name, m_sinkBlocks);
}

Block *FlowGraph::findSourceBlock(std::string_view name) const {
    return findBlockImpl(name, m_sourceBlocks);
}

void FlowGraph::addBlockType(std::unique_ptr<BlockType> &&t) {
    m_types.insert({ t->name, std::move(t) });
}

void FlowGraph::addBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    m_blocks.push_back(std::move(block));
}

void FlowGraph::addSourceBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    if (sourceBlockAddedCallback) {
        sourceBlockAddedCallback(block.get());
    }
    m_sourceBlocks.push_back(std::move(block));
}

void FlowGraph::addSinkBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    m_sinkBlocks.push_back(std::move(block));
}

void FlowGraph::deleteBlock(Block *block) {
    for (auto &p : block->inputs()) {
        for (auto *c : p.connections) {
            disconnect(c);
        }
    }
    for (auto &p : block->outputs()) {
        for (auto *c : p.connections) {
            disconnect(c);
        }
    }

    if (blockDeletedCallback) {
        blockDeletedCallback(block);
    }

    auto select = [&](const auto &b) {
        return block == b.get();
    };

    if (auto it = std::find_if(m_sourceBlocks.begin(), m_sourceBlocks.end(), select); it != m_sourceBlocks.end()) {
        m_sourceBlocks.erase(it);
    }
    if (auto it = std::find_if(m_sinkBlocks.begin(), m_sinkBlocks.end(), select); it != m_sinkBlocks.end()) {
        m_sinkBlocks.erase(it);
    }
    if (auto it = std::find_if(m_blocks.begin(), m_blocks.end(), select); it != m_blocks.end()) {
        m_blocks.erase(it);
    }
}

void FlowGraph::connect(Block::Port *a, Block::Port *b) {
    assert(a->kind != b->kind);
    // make sure a is the output and b the input
    if (a->kind == Block::Port::Kind::Input) {
        std::swap(a, b);
    }
    auto it = m_connections.insert(Connection(a, b));
    a->connections.push_back(&(*it));
    b->connections.push_back(&(*it));
}

void FlowGraph::disconnect(Connection *c) {
    auto it = m_connections.get_iterator(c);
    assert(it != m_connections.end());

    for (auto *p : c->ports) {
        p->connections.erase(std::remove(p->connections.begin(), p->connections.end(), c));
    }

    m_connections.erase(it);
}

void FlowGraph::update() {
    for (auto &b : m_blocks) {
        b->m_updated = false;
    }
    for (auto &b : m_sourceBlocks) {
        b->processData();
    }

    for (auto &b : m_sinkBlocks) {
        b->updateInputs();
        b->processData();
    }
}

void FlowGraph::addRemoteSource(std::string_view uri) {
    RemoteDataSource::registerBlockType(this, uri);
}

void FlowGraph::registerRemoteSource(std::unique_ptr<BlockType> &&type, std::string_view uri) {
    m_remoteSources.push_back({ type.get(), std::string(uri) });
    addBlockType(std::move(type));
}

} // namespace DigitizerUi
