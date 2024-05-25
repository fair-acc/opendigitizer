#include "Flowgraph.hpp"

#include <cassert>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include "gnuradio-4.0/Tag.hpp"
#include "utils/Yaml.hpp"

#include <gnuradio-4.0/Graph_yaml_importer.hpp>

#include "App.hpp"

using namespace std::string_literals;

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

BlockType::BlockType(std::string_view name_, std::string_view l, std::string_view cat)
    : name(name_)
    , label(l.empty() ? name_ : l)
    , category(cat) {
}

std::unique_ptr<Block> BlockType::createBlock(std::string_view name) const {
    auto params    = defaultParameters;
    params["name"] = std::string(name);
    return std::make_unique<Block>(name, this, std::move(params));
}

BlockType::Registry &BlockType::registry() {
    static Registry r;
    return r;
}

const BlockType *BlockType::Registry::get(std::string_view id) const {
    auto it = m_types.find(id);
    return it == m_types.end() ? nullptr : it->second.get();
}

void BlockType::Registry::addBlockTypesFromPluginLoader(gr::PluginLoader &pluginLoader) {
    for (const auto &typeName : pluginLoader.knownBlocks()) {
        // TODO make this also work if the block doesn't allow T=float, or has multiple
        // non-defaulted template parameters.
        // (needs information about possible instantiations from the plugin loader)
        auto prototype = pluginLoader.instantiate(typeName, "float");
        if (!prototype) {
            fmt::println(std::cerr, "Could not instantiate block of type '{}<float>'", typeName);
            continue;
        }
        fmt::println("Registering block type '{}'", typeName);

        auto type               = std::make_unique<BlockType>(typeName, typeName, "TODO category");
        std::ignore             = prototype->settings().applyStagedParameters();
        type->defaultParameters = prototype->settings().get();
        for (const auto &[id, v] : type->defaultParameters) {
            if (auto param = std::get_if<int>(&v)) {
                type->parameters.emplace_back(id, id, BlockType::NumberParameter<int>{ *param });
            } else if (auto param = std::get_if<float>(&v)) {
                type->parameters.emplace_back(id, id, BlockType::NumberParameter<float>{ *param });
            } else if (auto param = std::get_if<std::string>(&v)) {
                type->parameters.emplace_back(id, id, BlockType::StringParameter{ *param });
            }
        }

        // TODO Create input and output ports (needs port information in BlockModel)
        for (auto index = 0UZ; index < prototype->dynamicInputPortsSize(); index++) {
            const auto& port = prototype->dynamicInputPort(index);
            type->inputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : port.valueTypeName(), port.name, false);
        }
        for (auto index = 0UZ; index < prototype->dynamicOutputPortsSize(); index++) {
            const auto& port = prototype->dynamicOutputPort(index);
            type->outputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : port.valueTypeName(), port.name, false);
        }

        addBlockType(std::move(type));
    }
}

void BlockType::Registry::addBlockType(std::unique_ptr<BlockType> &&t) {
    m_types.insert({ t->name, std::move(t) });
}

Block::Block(std::string_view name, const BlockType *t, gr::property_map params)
    : name(name), m_type(t), m_parameters(std::move(params)) {
    m_outputs.reserve(m_type->outputs.size());
    m_inputs.reserve(m_type->inputs.size());
    for (auto &o : m_type->outputs) {
        m_outputs.push_back({ this, o.type, o.dataset, Port::Kind::Output });
    }
    for (auto &o : m_type->inputs) {
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
        // old names
        if (t == "fc64") return DataType::ComplexFloat64;
        if (t == "fc32" || t == "complex") return DataType::ComplexFloat32;
        if (t == "sc64") return DataType::ComplexInt64;
        if (t == "sc32") return DataType::ComplexInt32;
        if (t == "sc16") return DataType::ComplexInt16;
        if (t == "sc8") return DataType::ComplexInt8;
        if (t == "f64") return DataType::Float64;
        if (t == "f32") return dataset ? DataType::DataSetFloat32 : DataType::Float32;
        if (t == "s64") return DataType::Int64;
        if (t == "s32") return DataType::Int32;
        if (t == "s16") return DataType::Int16;
        if (t == "s8") return DataType::Int8;
        if (t == "byte") return DataType::Int8;
        if (t == "bit" || t == "bits") return DataType::Bits;

        // GR4 names
        if (t == "std::complex<double>") return DataType::ComplexFloat64;
        if (t == "std::complex<float>") return DataType::ComplexFloat32;
        // if (t == "std::complex<std::int64_t>") return DataType::ComplexInt64;
        // if (t == "std::complex<std::int32_t>") return DataType::ComplexInt32;
        // if (t == "std::complex<std::int16_t>") return DataType::ComplexInt16;
        // if (t == "std::complex<std::int8_t>") return DataType::ComplexInt8;
        if (t == "double") return DataType::Float64;
        if (t == "float") return dataset ? DataType::DataSetFloat32 : DataType::Float32;
        if (t == "std::int64_t") return DataType::Int64;
        if (t == "std::int32_t" || t == "int") return DataType::Int32;
        if (t == "std::int16_t" || t == "short") return DataType::Int16;
        if (t == "std::int8_t") return DataType::Int8;

        if (t == "gr::DataSet<float>") return DataType::DataSetFloat32;

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
        YAML::Node config     = YAML::Load(str);

        auto       id         = config["id"].as<std::string>();
        auto       def        = m_types.insert({ id, std::make_unique<BlockType>(id) }).first->second.get();

        auto       parameters = config["parameters"];
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

FlowGraph::FlowGraph() {}

void FlowGraph::parse(const std::filesystem::path &file) {
    std::string str;
    readFile(file, str);
    parse(str);
}

void FlowGraph::parse(const std::string &str) {
    clear();
    auto graph = gr::load_grc(*_pluginLoader, str);

    graph.forEachBlock([&](const auto &grBlock) {
        auto typeName = grBlock.typeName();
        typeName      = std::string_view(typeName.begin(), typeName.find('<'));
        auto type     = BlockType::registry().get(typeName);
        if (!type) {
            throw std::runtime_error(fmt::format("Block type '{}' is unknown.", typeName));
        }

        auto block               = type->createBlock(grBlock.name());
        block->m_uniqueName      = grBlock.uniqueName();
        block->m_metaInformation = grBlock.metaInformation();
        block->updateSettings(grBlock.settings().get());
        addBlock(std::move(block));
    });

    auto findBlock = [&](std::string_view uniqueName) -> Block * {
        const auto it = std::ranges::find_if(m_blocks, [uniqueName](const auto &b) { return b->m_uniqueName == uniqueName; });
        return it == m_blocks.end() ? nullptr : it->get();
    };

    graph.forEachEdge([&](const auto &edge) {
        auto srcBlock = findBlock(edge._sourceBlock->uniqueName());
        assert(srcBlock);
        const auto sourcePort = edge._sourcePortDefinition.topLevel;
        auto       dstBlock   = findBlock(edge._destinationBlock->uniqueName());
        assert(dstBlock);
        const auto destinationPort = edge._destinationPortDefinition.topLevel;
        // TODO support port collections

        if (sourcePort >= srcBlock->m_outputs.size()) {
            fmt::print("ERROR: Cannot connect, no output port with index {} in {}, have only {} ports\n", sourcePort, srcBlock->m_uniqueName, srcBlock->m_outputs.size());
        } else if (destinationPort >= dstBlock->m_inputs.size()) {
            fmt::print("ERROR: Cannot connect, no input port with index {} in {}, have only {} ports\n", destinationPort, dstBlock->m_uniqueName, dstBlock->m_inputs.size());
        } else {
            connect(&srcBlock->m_outputs[sourcePort], &dstBlock->m_inputs[destinationPort]);
        }
    });

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
                map.write("id", b->typeName());

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

Block *FlowGraph::findBlock(std::string_view name) const {
    const auto it = std::find_if(m_blocks.begin(), m_blocks.end(), [&](const auto &b) { return b->name == name; });
    return it == m_blocks.end() ? nullptr : it->get();
}

void FlowGraph::addBlock(std::unique_ptr<Block> &&block) {
    block->m_flowGraph = this;
    block->update();
    if (block->type().isPlotSink() && plotSinkBlockAddedCallback) {
        plotSinkBlockAddedCallback(block.get());
    }
    m_blocks.push_back(std::move(block));
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

    std::erase_if(m_blocks, select);

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
    block->updateSettings({ { "remote_uri", std::string(uri) } });
    addBlock(std::move(block));
}

namespace {

static bool isDrawable(const gr::property_map &meta, std::string_view category) {
    auto it = meta.find("Drawable");
    if (it == meta.end() || !std::holds_alternative<gr::property_map>(it->second)) {
        return false;
    }
    const auto &drawableMap = std::get<gr::property_map>(it->second);
    const auto  catIt       = drawableMap.find("Category");
    return catIt != drawableMap.end() && std::holds_alternative<std::string>(catIt->second) && std::get<std::string>(catIt->second) == category;
}

static std::unique_ptr<gr::BlockModel> createGRBlock(gr::PluginLoader &loader, const Block &block) {
    DataType t          = DataType::Float32;
    auto     inputsView = block.dataInputs();
    if (!std::ranges::empty(inputsView)) {
        if (auto &in = *std::ranges::begin(inputsView); in.connections.size() > 0) {
            auto &src = in.connections[0]->src;
            t         = src.block->outputs()[src.index].type;
        }
    }
    auto params    = block.parameters();
    params["name"] = block.name;
    auto grBlock   = loader.instantiate(block.typeName(), DataType::name(t));

    if (!grBlock) {
        fmt::println(std::cerr, "Could not create GR Block for {} ({}<{}>)\n", block.name, block.typeName(), DataType::name(t));
        return nullptr;
    }

    grBlock->settings().set(params);
    grBlock->settings().applyStagedParameters();
    return grBlock;
}
} // namespace

ExecutionContext FlowGraph::createExecutionContext() {
    ExecutionContext context;
    for (const auto &block : m_blocks) {
        auto grBlock = createGRBlock(*_pluginLoader, *block);
        if (!grBlock) {
            continue;
        }
        block->m_uniqueName      = grBlock->uniqueName();
        block->m_metaInformation = grBlock->metaInformation();

        if (isDrawable(block->metaInformation(), "Toolbar")) {
            context.toolbarBlocks.push_back(grBlock.get());
        }
        if (isDrawable(block->metaInformation(), "ChartPane")) {
            context.plotSinkGrBlocks.insert({ block->name, grBlock.get() });
        }
        context.graph.addBlock(std::move(grBlock));
    }

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
    if (msg.serviceName != App::instance().schedulerUniqueName() && msg.endpoint == gr::block::property::kSetting) {
        const auto it = std::ranges::find_if(m_blocks, [&](const auto &b) { return b->m_uniqueName == msg.serviceName; });
        if (it == m_blocks.end()) {
            fmt::println(std::cerr, "Received settings for unknown block '{}'", msg.serviceName);
            return;
        }
        if (!msg.data) {
            fmt::println(std::cerr, "Received settings error for block '{}': {}", msg.serviceName, msg.data.error());
            return;
        }
        if (it->get()->typeName() == "opendigitizer::RemoteSource") {
            if (const auto remoteUri = std::get_if<std::string>(&msg.data.value().at("remote_uri"))) {
                App::instance().dashboard->registerRemoteService(it->get()->name, *remoteUri);
            }
        }
        (*it)->updateSettings(msg.data.value());
    }
}

void FlowGraph::setPluginLoader(std::shared_ptr<gr::PluginLoader> loader) {
    _pluginLoader = std::move(loader);
}
} // namespace DigitizerUi
