#include "flowgraph.hpp"

#include <assert.h>

#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>

#include <fmt/format.h>

#include "gnuradio-4.0/Tag.hpp"
#include "yamlutils.hpp"
#include <unordered_map>
#include <yaml-cpp/yaml.h>

#include <gnuradio-4.0/Graph_yaml_importer.hpp>

#include "app.hpp"

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

class GRBlock : public Block {
public:
    using Block::Block;
    std::unique_ptr<gr::BlockModel> createGRBlock() final {
        return {};
    }
};

BlockType::BlockType(std::string_view n, std::string_view l, std::string_view cat)
    : name(n)
    , label(l.empty() ? n : l)
    , category(cat)
    , createBlock([this](std::string_view n) { return std::make_unique<GRBlock>(n, this->name, this); }) {
}

BlockType::~BlockType() = default;

BlockType::Registry &BlockType::registry() {
    static Registry r;
    return r;
}

BlockType *BlockType::Registry::get(std::string_view id) const {
    auto it = m_types.find(id);
    return it == m_types.end() ? nullptr : it->second.get();
}

void BlockType::Registry::addBlockType(std::unique_ptr<BlockType> &&t) {
    m_types.insert({ t->name, std::move(t) });
}

Block::Block(std::string_view name, std::string_view id, BlockType *t)
    : type(t)
    , name(name)
    , id(id) {
    if (!type) {
        return;
    }

    m_outputs.reserve(type->outputs.size());
    m_inputs.reserve(type->inputs.size());
    for (auto &o : type->outputs) {
        m_outputs.push_back({ this, o.type, o.dataset, Port::Kind::Output });
    }
    for (auto &o : type->inputs) {
        m_inputs.push_back({ this, o.type, o.dataset, Port::Kind::Input });
    }
}

void Block::setParameter(const std::string &name, const pmtv::pmt &p) {
    m_parameters[name] = p;

    gr::Message msg;
    msg.serviceName = m_uniqueName;
    msg.endpoint    = gr::block::property::kStagedSetting;
    msg.data        = gr::property_map{ { name, p } };
    App::instance().sendMessage(msg);
}

void Block::updateSettings(const gr::property_map &settings) {
    for (const auto &[k, v] : settings) {
        m_parameters[k] = v;
    }
}

void Block::update() {
    auto parseType = [](const std::string &t, bool dataset) -> DataType {
        if (t == "fc64") return DataType::ComplexFloat64;
        if (t == "fc32" || t == "complex") return DataType::ComplexFloat32;
        if (t == "sc64") return DataType::ComplexInt64;
        if (t == "sc32") return DataType::ComplexInt32;
        if (t == "sc16") return DataType::ComplexInt16;
        if (t == "sc8") return DataType::ComplexInt8;
        if (t == "f64") return DataType::Float64;
        if (t == "f32" || t == "float") return dataset ? DataType::DataSetFloat32 : DataType::Float32;
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
    auto getType = [&](const std::string &t, bool dataset) {
        // if (t.starts_with("${") && t.ends_with("}")) {
        //     auto val = getParameterValue(t.substr(2, t.size() - 3));
        //     if (auto type = std::get_if<std::string>(&val)) {
        //         return parseType(*type);
        //     }
        // }
        return parseType(t, dataset);
    };
    for (auto &in : m_inputs) {
        in.type = getType(in.m_rawType, in.dataset);
    }
    for (auto &out : m_outputs) {
        out.type = getType(out.m_rawType, out.dataset);
    }
}

std::string Block::EnumParameter::toString() const {
    return definition.optionsLabels[optionIndex];
}

static std::string_view strview(auto &&s) {
    return { s.data(), s.size() };
}

static void readFile(const std::filesystem::path &file, std::string &str) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        throw std::runtime_error(fmt::format("Cannot open file '{}'", file.native()));
    }

    stream.seekg(0, std::ios::end);
    size_t size = stream.tellg();

    str.resize(size);
    stream.seekg(0);
    stream.read(str.data(), size);
}

void BlockType::Registry::loadBlockDefinitions(const std::filesystem::path &dir) {
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
        def->createBlock  = [def](std::string_view name) { return std::make_unique<GRBlock>(name, def->name, def); };

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
                def->parameters.push_back(BlockType::Parameter{ id, label, BlockType::StringParameter{ defaultValue } });
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

FlowGraph::FlowGraph()
    : _pluginLoader(gr::globalBlockRegistry(), {}) {}

void FlowGraph::parse(const std::filesystem::path &file) {
    std::string str;
    readFile(file, str);
    parse(str);
}

void FlowGraph::parse(const std::string &str) {
    clear();

    auto tree   = YAML::Load(str);
    auto blocks = tree["blocks"];

    for (const auto &b : blocks) {
        const auto name = b["name"].as<std::string>();
        const auto id   = b["id"].as<std::string>();

        auto       type = BlockType::registry().get(id);
        if (!type) {
            throw std::runtime_error(fmt::format("Block type '{}' is unknown.", id));
        }

        auto block = type->createBlock(name);

        auto pars  = b["parameters"];
        if (pars && pars.IsMap()) {
            for (auto it = pars.begin(); it != pars.end(); ++it) {
                auto key = it->first.as<std::string>();

                auto p   = block->parameters().find(key);
                if (p == block->parameters().end()) {
                    continue;
                }

                auto unpack = [&]<typename T>() {
                    return std::visit([&](auto &&val) {
                        if constexpr (std::is_same_v<T, std::decay_t<decltype(val)>>) {
                            auto value = it->second.template as<T>();
                            block->setParameter(key, value);
                            return true;
                        }
                        return false;
                    },
                            p->second);
                };
                unpack.operator()<std::int8_t>() || unpack.operator()<std::int16_t>() || unpack.operator()<std::int32_t>() || unpack.operator()<std::int64_t>() || unpack.operator()<std::uint8_t>() || unpack.operator()<std::uint16_t>() || unpack.operator()<std::uint32_t>() || unpack.operator()<std::uint64_t>() || unpack.operator()<bool>() || unpack.operator()<float>() || unpack.operator()<double>() || unpack.operator()<std::string>();
            }
        }

        if (type->isSource()) {
            addSourceBlock(std::move(block));
        } else if (type->isSink()) {
            addSinkBlock(std::move(block));
        } else {
            addBlock(std::move(block));
        }
    }

    const auto connections = tree["connections"];
    for (const auto &c : connections) {
        if (c.size() != 4) {
            throw std::runtime_error("Malformed connection");
        }

        auto        srcBlockName = c[0].as<std::string>();
        auto        srcPortStr   = c[1].as<std::string>();
        std::size_t srcPort;
        std::from_chars(srcPortStr.data(), srcPortStr.data() + srcPortStr.size(), srcPort);

        auto        dstBlockName = c[2].as<std::string>();
        auto        dstPortStr   = c[3].as<std::string>();
        std::size_t dstPort;
        std::from_chars(dstPortStr.data(), dstPortStr.data() + dstPortStr.size(), dstPort);

        auto srcBlock = findBlock(srcBlockName);
        assert(srcBlock);
        auto dstBlock = findBlock(dstBlockName);
        assert(dstBlock);

        connect(&srcBlock->m_outputs[srcPort], &dstBlock->m_inputs[dstPort]);
    }

    m_graphChanged = true;
}

void FlowGraph::clear() {
    auto del = [&](auto &vec) {
        if (blockDeletedCallback) {
            for (auto &b : vec) {
                blockDeletedCallback(b.get());
            }
        }
        vec.clear();
    };
    del(m_blocks);
    del(m_sourceBlocks);
    del(m_sinkBlocks);
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
                        for (const auto &[settingsKey, settingsValue] : parameters) {
                            std::visit([&]<typename T>(const T &value) { pars.write(settingsKey, value); }, settingsValue);
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

        if (!m_connections.empty()) {
            root.write("connections", [&]() {
                YamlSeq connections(out);
                for (const auto &c : m_connections) {
                    out << YAML::Flow;
                    YamlSeq seq(out);
                    out << c.src.block->name << c.src.index;
                    out << c.dst.block->name << c.dst.index;
                }
            });
        }
    }

    stream << out.c_str();
    return int(out.size());
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

void FlowGraph::addBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    m_blocks.push_back(std::move(block));
    m_graphChanged = true;
}

void FlowGraph::addSourceBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    if (sourceBlockAddedCallback) {
        sourceBlockAddedCallback(block.get());
    }
    m_sourceBlocks.push_back(std::move(block));
    m_graphChanged = true;
}

void FlowGraph::addSinkBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    if (sinkBlockAddedCallback) {
        sinkBlockAddedCallback(block.get());
    }
    m_sinkBlocks.push_back(std::move(block));
    m_graphChanged = true;
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

    m_graphChanged = true;
}

Connection *FlowGraph::connect(Block::Port *a, Block::Port *b) {
    assert(a->kind != b->kind);
    // make sure a is the output and b the input
    if (a->kind == Block::Port::Kind::Input) {
        std::swap(a, b);
    }

    auto ain = std::size_t(a - a->block->outputs().data());
    auto bin = std::size_t(b - b->block->inputs().data());

    fmt::print("connect {}.{} to {}.{}\n", a->block->name, ain, b->block->name, bin);

    auto it = m_connections.insert(Connection(a->block, ain, b->block, bin));
    a->connections.push_back(&(*it));
    b->connections.push_back(&(*it));
    m_graphChanged = true;
    return &(*it);
}

void FlowGraph::disconnect(Connection *c) {
    auto it = m_connections.get_iterator(c);
    assert(it != m_connections.end());

    auto ports = { &c->src.block->outputs()[c->src.index], &c->dst.block->inputs()[c->dst.index] };
    for (auto *p : ports) {
        p->connections.erase(std::remove(p->connections.begin(), p->connections.end(), c));
    }

    m_connections.erase(it);
    m_graphChanged = true;
}

void FlowGraph::addRemoteSource(std::string_view uri) {
    auto block = BlockType::registry().get("opendigitizer::RemoteSource")->createBlock("Remote Source");
    block->setParameter("remote_uri", std::string(uri));
    addBlock(std::move(block));
}

namespace {

bool isDrawable(const gr::property_map &meta, std::string_view category) {
    auto it = meta.find("Drawable");
    if (it == meta.end() || !std::holds_alternative<gr::property_map>(it->second)) {
        return false;
    }
    const auto &drawableMap = std::get<gr::property_map>(it->second);
    const auto  catIt       = drawableMap.find("Category");
    return catIt != drawableMap.end() && std::holds_alternative<std::string>(catIt->second) && std::get<std::string>(catIt->second) == category;
}

} // namespace

ExecutionContext FlowGraph::createExecutionContext() {
    ExecutionContext context;
    for (const auto *list : std::initializer_list<const decltype(m_blocks) *>{ &m_sourceBlocks, &m_blocks, &m_sinkBlocks }) {
        for (const auto &block : *list) {
            auto node = block->createGRBlock();
            if (!node) {
                fmt::print("no node {}\n", block->name);
                continue;
            }
            block->m_uniqueName      = node->uniqueName();
            block->m_metaInformation = node->metaInformation();

#ifdef __EMSCRIPTEN__
            try {
#endif
                std::ignore = node->settings().set(block->parameters());
#ifdef __EMSCRIPTEN__
            } catch (...) {
            }
#endif
            if (isDrawable(block->metaInformation(), "Toolbar")) {
                context.toolbarBlocks.push_back(node.get());
            }
            if (isDrawable(block->metaInformation(), "Plot")) {
                context.plotBlocks.push_back(node.get());
            }
            context.graph.addBlock(std::move(node));
        }
    }

#if 0 // needed?
    for (const auto *list : std::initializer_list<const decltype(m_blocks) *>{ &m_sourceBlocks, &m_blocks, &m_sinkBlocks }) {
        for (const auto &block : *list) {
            block->connect(graph);
        }
    }
#endif

    auto findBlock = [&](std::string_view name) -> gr::BlockModel * {
        const auto it = std::ranges::find_if(context.graph.blocks(), [&](auto &b) { return b->uniqueName() == name; });
        return it == context.graph.blocks().end() ? nullptr : it->get();
    };

    for (auto &c : m_connections) {
        const auto src = findBlock(c.src.block->m_uniqueName);
        const auto dst = findBlock(c.dst.block->m_uniqueName);
        if (!src || !dst) {
            continue;
        }

        context.graph.connect(*src, c.src.index, *dst, c.dst.index);
    }

    m_graphChanged = false;

    std::stringstream str;
    save(str);
    m_grc = str.str();

    return context;
}

void FlowGraph::handleMessage(const gr::Message &msg) {
    if (msg.endpoint == gr::block::property::kSetting) {
        forEachBlock([&, name = msg.serviceName](auto &block) -> bool {
            if (block->m_uniqueName == name) {
                if (msg.data.has_value()) {
                    block->updateSettings(msg.data.value());
                }
                return false;
            }
            return true;
        });
    }
}

} // namespace DigitizerUi
