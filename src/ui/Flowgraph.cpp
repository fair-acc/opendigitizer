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

template<typename T>
auto typeToName() {
    std::any value = T{};
    return value.type().name();
}

std::string valueTypeName(auto& port) {
    static const std::map<std::string, std::string> mangledToName{{typeToName<float>(), "float"s}, {typeToName<double>(), "double"s},

        {typeToName<std::complex<float>>(), "std::complex<float>"s}, {typeToName<std::complex<double>>(), "std::complex<double>"s},

        {typeToName<gr::DataSet<float>>(), "gr::DataSet<float>"s}, {typeToName<gr::DataSet<double>>(), "gr::DataSet<double>"s},

        {typeToName<std::int8_t>(), "std::int8_t"s}, {typeToName<std::int16_t>(), "std::int16_t"s}, {typeToName<std::int32_t>(), "std::int32_t"s}, {typeToName<std::int64_t>(), "std::int64_t"s}};

    if (auto it = mangledToName.find(port.defaultValue().type().name()); it != mangledToName.end()) {
        return it->second;
    } else {
        return "unknown_type"s;
    }
}

const std::string& DataType::toString() const {
    const static std::string names[] = {"int32", "float32", "complex float 32"};
    return names[m_id];
}

std::string Block::Parameter::toString() const {
    if (auto* e = std::get_if<Block::EnumParameter>(this)) {
        return e->toString();
    } else if (auto* r = std::get_if<Block::RawParameter>(this)) {
        return r->value;
    } else if (auto* i = std::get_if<Block::NumberParameter<int>>(this)) {
        return std::to_string(i->value);
    } else if (auto* i = std::get_if<Block::NumberParameter<float>>(this)) {
        return std::to_string(i->value);
    }
    assert(0);
    return {};
}

BlockDefinition::BlockDefinition(std::string_view name_, std::string_view l, std::string_view cat) : name(name_), label(l.empty() ? name_ : l), category(cat) {}

std::unique_ptr<Block> BlockDefinition::createBlock(std::string_view name) const {
    auto params    = defaultSettings;
    params["name"] = std::string(name);
    return std::make_unique<Block>(name, this, std::move(params));
}

BlockDefinition::Registry& BlockDefinition::registry() {
    static Registry r;
    return r;
}

const BlockDefinition* BlockDefinition::Registry::get(std::string_view id) const {
    auto it = m_types.find(id);
    return it == m_types.end() ? nullptr : it->second.get();
}

void BlockDefinition::Registry::addBlockDefinitionsFromPluginLoader(gr::PluginLoader& pluginLoader) {
    for (const auto& typeName : pluginLoader.knownBlocks()) {
        const auto availableParametrizations = pluginLoader.knownBlockParameterizations(typeName);
        auto       type                      = std::make_unique<BlockDefinition>(typeName, typeName, "TODO category");
        bool       first                     = true;

        for (const auto& parametrization : availableParametrizations) {
            BlockInstantiationDefinition instantiationType;

            auto prototype = pluginLoader.instantiate(typeName, parametrization);
            if (!prototype) {
                fmt::println(std::cerr, "Could not instantiate block of type '{}<{}>'", typeName, parametrization);
                continue;
            }

            std::ignore          = prototype->settings().applyStagedParameters();
            auto defaultSettings = prototype->settings().get();
            for (const auto& [id, v] : defaultSettings) {
                if (auto param = std::get_if<int>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::NumberParameter<int>{*param});
                } else if (auto param = std::get_if<float>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::NumberParameter<float>{*param});
                } else if (auto param = std::get_if<std::string>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::StringParameter{*param});
                }
            }

            for (auto index = 0UZ; index < prototype->dynamicInputPortsSize(); index++) {
                const auto& port = prototype->dynamicInputPort(index);
                instantiationType.inputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : valueTypeName(port), port.name, false);
            }
            for (auto index = 0UZ; index < prototype->dynamicOutputPortsSize(); index++) {
                const auto& port = prototype->dynamicOutputPort(index);
                instantiationType.outputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : valueTypeName(port), port.name, false);
            }

            if (first) {
                type->isSource        = instantiationType.inputs.empty() && !instantiationType.outputs.empty();
                type->isSink          = !instantiationType.inputs.empty() && instantiationType.outputs.empty();
                type->defaultSettings = std::move(defaultSettings);
                first                 = false;
            }

            type->instantiations[parametrization] = std::move(instantiationType);
        }
        addBlockDefinition(std::move(type));
    }
}

void BlockDefinition::Registry::addBlockDefinition(std::unique_ptr<BlockDefinition>&& t) { m_types.insert({t->name, std::move(t)}); }

Block::Block(std::string_view name, const BlockDefinition* t, gr::property_map settings) : name(name), m_type(t), m_settings(std::move(settings)) { //
    setCurrentInstantiation(m_type->instantiations.cbegin()->first);
}

void Block::setSetting(const std::string& name, const pmtv::pmt& p) {
    m_settings[name] = p;

    gr::Message msg;
    msg.serviceName = m_uniqueName;
    msg.endpoint    = gr::block::property::kStagedSetting;
    msg.data        = gr::property_map{{name, p}};
    App::instance().sendMessage(msg);
}

void Block::updateSettings(const gr::property_map& settings) {
    for (const auto& [k, v] : settings) {
        m_settings[k] = v;
    }
}

void Block::update() {
    auto parseType = [](const std::string& t, bool dataset) -> DataType {
        // old names
        if (t == "fc64") {
            return DataType::ComplexFloat64;
        }
        if (t == "fc32" || t == "complex") {
            return DataType::ComplexFloat32;
        }
        if (t == "sc64") {
            return DataType::ComplexInt64;
        }
        if (t == "sc32") {
            return DataType::ComplexInt32;
        }
        if (t == "sc16") {
            return DataType::ComplexInt16;
        }
        if (t == "sc8") {
            return DataType::ComplexInt8;
        }
        if (t == "f64") {
            return dataset ? DataType::DataSetFloat64 : DataType::Float64;
        }
        if (t == "f32") {
            return dataset ? DataType::DataSetFloat32 : DataType::Float32;
        }
        if (t == "s64") {
            return DataType::Int64;
        }
        if (t == "s32") {
            return DataType::Int32;
        }
        if (t == "s16") {
            return DataType::Int16;
        }
        if (t == "s8") {
            return DataType::Int8;
        }
        if (t == "byte") {
            return DataType::Int8;
        }
        if (t == "bit" || t == "bits") {
            return DataType::Bits;
        }

        // GR4 names
        if (t == "std::complex<double>") {
            return DataType::ComplexFloat64;
        }
        if (t == "std::complex<float>") {
            return DataType::ComplexFloat32;
        }
        // if (t == "std::complex<std::int64_t>") return DataType::ComplexInt64;
        // if (t == "std::complex<std::int32_t>") return DataType::ComplexInt32;
        // if (t == "std::complex<std::int16_t>") return DataType::ComplexInt16;
        // if (t == "std::complex<std::int8_t>") return DataType::ComplexInt8;
        if (t == "double") {
            return dataset ? DataType::DataSetFloat64 : DataType::Float64;
        }
        if (t == "float") {
            return dataset ? DataType::DataSetFloat32 : DataType::Float32;
        }
        if (t == "std::int64_t") {
            return DataType::Int64;
        }
        if (t == "std::int32_t" || t == "int") {
            return DataType::Int32;
        }
        if (t == "std::int16_t" || t == "short") {
            return DataType::Int16;
        }
        if (t == "std::int8_t") {
            return DataType::Int8;
        }

        if (t == "gr::DataSet<float>") {
            return DataType::DataSetFloat32;
        }
        if (t == "gr::DataSet<double>") {
            return DataType::DataSetFloat64;
        }

        if (t == "message") {
            return DataType::AsyncMessage;
        }
        if (t == "bus") {
            return DataType::BusConnection;
        }
        if (t == "") {
            return DataType::Wildcard;
        }
        if (t == "untyped") {
            return DataType::Untyped;
        }

        fmt::print("unhandled type {}\n", t);
        assert(0);
        return DataType::Untyped;
    };
    auto getType = [&](const std::string& t, bool dataset) {
        // if (t.starts_with("${") && t.ends_with("}")) {
        //     auto val = getParameterValue(t.substr(2, t.size() - 3));
        //     if (auto type = std::get_if<std::string>(&val)) {
        //         return parseType(*type);
        //     }
        // }
        return parseType(t, dataset);
    };
    for (auto& in : m_inputs) {
        in.portDataType = getType(in.rawPortType, in.isDataset);
    }
    for (auto& out : m_outputs) {
        out.portDataType = getType(out.rawPortType, out.isDataset);
    }
}

void Block::setCurrentInstantiation(const std::string& newInstantiation) {
    m_inputs.clear();
    m_outputs.clear();

    m_currentInstantiation = newInstantiation;

    const auto& instantiation = currentInstantiation();
    m_outputs.reserve(instantiation.outputs.size());
    m_inputs.reserve(instantiation.inputs.size());
    for (auto& o : instantiation.outputs) {
        m_outputs.push_back({this, o.name, o.type, o.dataset, Port::Direction::Output});
    }
    for (auto& o : instantiation.inputs) {
        m_inputs.push_back({this, o.name, o.type, o.dataset, Port::Direction::Input});
    }
}

std::string Block::EnumParameter::toString() const { return definition.optionsLabels[optionIndex]; }

static std::string_view strview(auto&& s) { return {s.data(), s.size()}; }

static void readFile(const std::filesystem::path& file, std::string& str) {
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

FlowGraph::FlowGraph() {}

void FlowGraph::parse(const std::filesystem::path& file) {
    std::string str;
    readFile(file, str);
    parse(str);
}

void FlowGraph::parse(const std::string& str) {
    clear();
    auto graph = gr::loadGrc(*_pluginLoader, str);

    graph.forEachBlock([&](const auto& grBlock) {
        auto typeName = grBlock.typeName();
        typeName      = std::string_view(typeName.begin(), typeName.find('<'));
        auto type     = BlockDefinition::registry().get(typeName);
        if (!type) {
            throw std::runtime_error(fmt::format("Block type '{}' is unknown.", typeName));
        }

        auto block               = type->createBlock(grBlock.name());
        block->m_uniqueName      = grBlock.uniqueName();
        block->m_metaInformation = grBlock.metaInformation();
        block->updateSettings(grBlock.settings().get());
        addBlock(std::move(block));
    });

    auto findBlock = [&](std::string_view uniqueName) -> Block* {
        const auto it = std::ranges::find_if(m_blocks, [uniqueName](const auto& b) { return b->m_uniqueName == uniqueName; });
        return it == m_blocks.end() ? nullptr : it->get();
    };

    auto findPort = [&](const auto& portDefinition, auto& ports) {
        return std::visit(gr::meta::overloaded(
                              [&](const gr::PortDefinition::IndexBased& definition) {
                                  if (definition.topLevel >= ports.size()) {
                                      fmt::println(std::cerr, "Cannot connect, index {} is not valid (only {} ports available)", definition.topLevel, ports.size());
                                  }
                                  // TODO check subIndex once we support port collections
                                  return std::pair{ports.begin() + definition.topLevel, definition.subIndex};
                              },
                              [&](const gr::PortDefinition::StringBased& definition) {
                                  auto       split    = std::string_view(definition.name) | std::ranges::views::split('#');
                                  const auto segs     = std::vector(split.begin(), split.end());
                                  const auto name     = std::string_view(segs[0].data(), segs[0].size());
                                  const auto subIndex = [&] {
                                      if (segs.size() < 2) {
                                          return 0UZ;
                                      }
                                      auto index          = 0UZ;
                                      const auto& [_, ec] = std::from_chars(segs[1].begin(), segs[1].end(), index);
                                      if (ec != std::errc{}) {
                                          throw std::runtime_error(fmt::format("Invalid subindex in '{}'", definition.name));
                                      }
                                      return index;
                                  }();
                                  const auto it = std::ranges::find_if(ports, [&name](const auto& port) { return port.name == name; });
                                  if (it == ports.end()) {
                                      fmt::println(std::cerr, "Cannot connect, no port with name '{}'", name);
                                  }

                                  // TODO check subIndex once we support port collections

                                  return std::pair{it, subIndex};
                              }),
            portDefinition.definition);
    };

    graph.forEachEdge([&](const auto& edge) {
        auto srcBlock = findBlock(edge._sourceBlock->uniqueName());
        assert(srcBlock);
        // TODO do not ignore subindexes
        const auto& [srcPort, srcIndex] = findPort(edge.sourcePortDefinition(), srcBlock->m_outputs);
        auto dstBlock                   = findBlock(edge._destinationBlock->uniqueName());
        assert(dstBlock);
        const auto& [dstPort, dstIndex] = findPort(edge.destinationPortDefinition(), dstBlock->m_inputs);

        if (srcPort == srcBlock->m_outputs.end() || dstPort == dstBlock->m_inputs.end()) {
            return;
        }
        connect(&*srcPort, &*dstPort);
    });

    m_graphChanged = true;
}

void FlowGraph::clear() {
    auto del = [&](auto& vec) {
        if (blockDeletedCallback) {
            for (auto& b : vec) {
                blockDeletedCallback(b.get());
            }
        }
        vec.clear();
    };
    del(m_blocks);
    m_connections.clear();
}

int FlowGraph::save(std::ostream& stream) {
    YAML::Emitter out;
    {
        YamlMap root(out);
        root.write("blocks", [&]() {
            YamlSeq blocks(out);

            auto emitBlock = [&](auto&& b) {
                YamlMap map(out);
                map.write("name", b->name);
                map.write("id", b->typeName());

                const auto& settings = b->settings();
                if (!settings.empty()) {
                    map.write("parameters", [&]() {
                        YamlMap pars(out);
                        for (const auto& [settingsKey, settingsValue] : settings) {
                            std::visit([&]<typename T>(const T& value) { pars.write(settingsKey, value); }, settingsValue);
                        }
                    });
                }
            };

            for (auto& b : m_blocks) {
                emitBlock(b);
            }
        });

        if (!m_connections.empty()) {
            root.write("connections", [&]() {
                YamlSeq connections(out);
                for (const auto& c : m_connections) {
                    out << YAML::Flow;
                    YamlSeq seq(out);
                    out << c.src.uiBlock->name << c.src.index;
                    out << c.dst.uiBlock->name << c.dst.index;
                }
            });
        }
    }

    stream << out.c_str();
    return int(out.size());
}

Block* FlowGraph::findBlock(std::string_view name) const {
    const auto it = std::find_if(m_blocks.begin(), m_blocks.end(), [&](const auto& b) { return b->name == name; });
    return it == m_blocks.end() ? nullptr : it->get();
}

void FlowGraph::addBlock(std::unique_ptr<Block>&& block) {
    block->m_flowGraph = this;
    block->update();
    if (block->type().isPlotSink() && plotSinkBlockAddedCallback) {
        plotSinkBlockAddedCallback(block.get());
    }
    m_blocks.push_back(std::move(block));
    m_graphChanged = true;
}

void FlowGraph::deleteBlock(Block* block) {
    for (auto& p : block->inputs()) {
        for (auto* c : p.portConnections) {
            disconnect(c);
        }
    }
    for (auto& p : block->outputs()) {
        for (auto* c : p.portConnections) {
            disconnect(c);
        }
    }

    if (blockDeletedCallback) {
        blockDeletedCallback(block);
    }

    auto select = [&](const auto& b) { return block == b.get(); };

    std::erase_if(m_blocks, select);

    m_graphChanged = true;
}

Connection* FlowGraph::connect(Block::Port* a, Block::Port* b) {
    assert(a->portDirection != b->portDirection);
    // make sure a is the output and b the input
    if (a->portDirection == Block::Port::Direction::Input) {
        std::swap(a, b);
    }

    auto ain = std::size_t(a - a->owningUiBlock->outputs().data());
    auto bin = std::size_t(b - b->owningUiBlock->inputs().data());

    fmt::print("connect {}.{} to {}.{}\n", a->owningUiBlock->name, ain, b->owningUiBlock->name, bin);

    auto it = m_connections.insert(Connection(a->owningUiBlock, ain, b->owningUiBlock, bin));
    a->portConnections.push_back(&(*it));
    b->portConnections.push_back(&(*it));
    m_graphChanged = true;
    return &(*it);
}

void FlowGraph::disconnect(Connection* c) {
    auto it = m_connections.get_iterator(c);
    assert(it != m_connections.end());

    auto ports = {&c->src.uiBlock->outputs()[c->src.index], &c->dst.uiBlock->inputs()[c->dst.index]};
    for (auto* p : ports) {
        p->portConnections.erase(std::remove(p->portConnections.begin(), p->portConnections.end(), c));
    }

    m_connections.erase(it);
    m_graphChanged = true;
}

Block* FlowGraph::addRemoteSource(std::string_view uri) {
    auto block = BlockDefinition::registry().get("opendigitizer::RemoteSource")->createBlock("Remote Source");
    block->updateSettings({{"remote_uri", std::string(uri)}});
    auto res = block.get();
    addBlock(std::move(block));
    return res;
}

namespace {

static bool isDrawable(const gr::property_map& meta, std::string_view category) {
    auto it = meta.find("Drawable");
    if (it == meta.end() || !std::holds_alternative<gr::property_map>(it->second)) {
        return false;
    }
    const auto& drawableMap = std::get<gr::property_map>(it->second);
    const auto  catIt       = drawableMap.find("Category");
    return catIt != drawableMap.end() && std::holds_alternative<std::string>(catIt->second) && std::get<std::string>(catIt->second) == category;
}

static std::unique_ptr<gr::BlockModel> createGRBlock(gr::PluginLoader& loader, const Block& block) {
    auto        params            = block.settings();
    const auto& instantiationName = block.currentInstantiationName();
    params["name"]                = block.name;
    auto grBlock                  = loader.instantiate(block.typeName(), instantiationName);

    if (!grBlock) {
        fmt::println(std::cerr, "Could not create GR Block for {} ({}<{}>)\n", block.name, block.typeName(), instantiationName);
        return nullptr;
    }

    grBlock->settings().set(params);
    grBlock->settings().applyStagedParameters();
    return grBlock;
}
} // namespace

ExecutionContext FlowGraph::createExecutionContext() {
    ExecutionContext context;
    for (const auto& block : m_blocks) {
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
            context.plotSinkGrBlocks.insert({block->name, grBlock.get()});
        }
        context.graph.addBlock(std::move(grBlock));
    }

    auto findBlock = [&](std::string_view name) -> gr::BlockModel* {
        const auto it = std::ranges::find_if(context.graph.blocks(), [&](auto& b) { return b->uniqueName() == name; });
        return it == context.graph.blocks().end() ? nullptr : it->get();
    };

    for (auto& c : m_connections) {
        const auto src = findBlock(c.src.uiBlock->m_uniqueName);
        const auto dst = findBlock(c.dst.uiBlock->m_uniqueName);
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

void FlowGraph::handleMessage(const gr::Message& msg) {
    if (msg.serviceName != App::instance().schedulerUniqueName() && msg.endpoint == gr::block::property::kSetting) {
        const auto it = std::ranges::find_if(m_blocks, [&](const auto& b) { return b->m_uniqueName == msg.serviceName; });
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

void FlowGraph::setPluginLoader(std::shared_ptr<gr::PluginLoader> loader) { _pluginLoader = std::move(loader); }

void FlowGraph::changeBlockDefinition(Block* block, const std::string& type) {
    auto select = [&](const auto& b) { return block == b.get(); };
    auto it     = std::ranges::find_if(m_blocks, select);
    assert(it != m_blocks.end());
    std::unique_ptr<Block> b{std::move((*it))};
    m_blocks.erase(it);

    // TODO: refactor -- this can be takeBlock to be a bit cleaner.
    // Now it looks like we are using a deleted thing
    deleteBlock(block);
    block->setCurrentInstantiation(type);
    addBlock(std::move(b));

    m_graphChanged = true;
}

} // namespace DigitizerUi
