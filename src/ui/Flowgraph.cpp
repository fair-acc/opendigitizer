#include "Flowgraph.hpp"

#include <URI.hpp>
#include <cassert>

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include "gnuradio-4.0/Tag.hpp"

#include <gnuradio-4.0/Graph_yaml_importer.hpp>
#include <gnuradio-4.0/YamlPmt.hpp>

#include "App.hpp"

using namespace std::string_literals;

namespace DigitizerUi {

template<typename T>
auto typeToName() {
    std::any value = T{};
    return value.type().name();
}

std::string valueTypeName(auto& port) {
    static const std::map<std::string, std::string> mangledToName{     //
        {typeToName<float>(), "float"s},                               //
        {typeToName<double>(), "double"s},                             //
        {typeToName<std::complex<float>>(), "std::complex<float>"s},   //
        {typeToName<std::complex<double>>(), "std::complex<double>"s}, //
        {typeToName<gr::DataSet<float>>(), "gr::DataSet<float>"s},     //
        {typeToName<gr::DataSet<double>>(), "gr::DataSet<double>"s},   //
        {typeToName<std::int8_t>(), "std::int8_t"s},                   //
        {typeToName<std::int16_t>(), "std::int16_t"s},                 //
        {typeToName<std::int32_t>(), "std::int32_t"s},                 //
        {typeToName<std::int64_t>(), "std::int64_t"s},                 //
        {typeToName<std::uint8_t>(), "std::uint8_t"s},                 //
        {typeToName<std::uint16_t>(), "std::uint16_t"s},               //
        {typeToName<std::uint32_t>(), "std::uint32_t"s},               //
        {typeToName<std::uint64_t>(), "std::uint64_t"s}};              //

    if (auto it = mangledToName.find(port.defaultValue().type().name()); it != mangledToName.end()) {
        return it->second;
    }

    throw gr::exception(fmt::format("valueTypeName(auto& port) - could not identify port data type '{}'", port.defaultValue().type().name()));
    return "unknown_type"s;
}

const std::string& DataType::toString() const {
    const static std::string names[] = {"int32", "float32", "complex float 32"};
    return names[m_id];
}

std::string Block::Parameter::toString() const {
    if (auto* e = std::get_if<Block::EnumParameter>(this)) {
        return e->toString();
    } else if (auto* rawParam = std::get_if<Block::RawParameter>(this)) {
        return rawParam->value;
    } else if (auto* intParam = std::get_if<Block::NumberParameter<int>>(this)) {
        return std::to_string(intParam->value);
    } else if (auto* floatParam = std::get_if<Block::NumberParameter<float>>(this)) {
        return std::to_string(floatParam->value);
    }
    assert(0);
    return {};
}

BlockDefinition::BlockDefinition(std::string_view name_, std::string_view l, std::string_view cat) : name(name_), label(l.empty() ? name_ : l), category(cat) {}

std::unique_ptr<Block> BlockDefinition::createBlock(std::string_view name_) const {
    auto params    = defaultSettings;
    params["name"] = std::string(name_);
    return std::make_unique<Block>(name_, this, std::move(params));
}

BlockRegistry& BlockRegistry::instance() {
    static BlockRegistry r;
    return r;
}

const BlockDefinition* BlockRegistry::get(std::string_view id) const {
    auto it = _types.find(id);
    return it == _types.end() ? nullptr : it->second.get();
}

void BlockRegistry::addBlockDefinitionsFromPluginLoader(gr::PluginLoader& pluginLoader) {
    for (const auto& typeName : pluginLoader.knownBlocks()) {
        auto type  = std::make_unique<BlockDefinition>(typeName, typeName, "TODO category");
        bool first = true;

        std::ranges::transform(pluginLoader.knownBlockParameterizations(typeName), std::back_inserter(type->availableParametrizations), [](std::string_view in) { return std::string(in); });

        for (const auto& parametrization : type->availableParametrizations) {
            BlockInstantiationDefinition instantiationType;

            auto prototype = pluginLoader.instantiate(typeName, parametrization);
            if (!prototype) {
                auto msg = fmt::format("Could not instantiate block of type '{}<{}>'", typeName, parametrization);
                components::Notification::error(msg);
                continue;
            }

            std::ignore          = prototype->settings().applyStagedParameters();
            auto defaultSettings = prototype->settings().get();
            for (const auto& [id, v] : defaultSettings) {
                if (auto intParam = std::get_if<int>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::NumberParameter<int>{*intParam});
                } else if (auto floatParam = std::get_if<float>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::NumberParameter<float>{*floatParam});
                } else if (auto stringParam = std::get_if<std::string>(&v)) {
                    instantiationType.settings.emplace_back(id, id, BlockInstantiationDefinition::StringParameter{*stringParam});
                }
            }

            for (auto index = 0UZ; index < prototype->dynamicInputPortsSize(); index++) {
                const auto& port = prototype->dynamicInputPort(index);
                instantiationType.inputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : valueTypeName(port), std::string(port.name), false);
            }
            for (auto index = 0UZ; index < prototype->dynamicOutputPortsSize(); index++) {
                const auto& port = prototype->dynamicOutputPort(index);
                instantiationType.outputs.emplace_back(port.type() == gr::PortType::MESSAGE ? "message"s : valueTypeName(port), std::string(port.name), false);
            }

            if (first) {
                type->isSource        = instantiationType.inputs.empty() && !instantiationType.outputs.empty();
                type->isSink          = !instantiationType.inputs.empty() && instantiationType.outputs.empty();
                type->defaultSettings = std::move(defaultSettings);
                first                 = false;
            }

            type->instantiations[std::string(parametrization)] = std::move(instantiationType);
        }
        addBlockDefinition(std::move(type));
    }
}

void BlockRegistry::addBlockDefinition(std::unique_ptr<BlockDefinition>&& t) { _types.insert({t->name, std::move(t)}); }

Block::Block(std::string_view name, const BlockDefinition* t, gr::property_map settings) : name(name), m_settings(std::move(settings)), m_type(t) {
    //
    setCurrentInstantiation(m_type->instantiations.cbegin()->first);
}

void Block::setSetting(const std::string& settingName, const pmtv::pmt& p) {
    m_settings[settingName] = p;

    gr::Message msg;
    msg.cmd         = gr::message::Command::Set;
    msg.serviceName = m_uniqueName;
    msg.endpoint    = gr::block::property::kStagedSetting;
    msg.data        = gr::property_map{{settingName, p}};
    App::instance().sendMessage(msg);
}

void Block::updateSettings(const gr::property_map& settings, const std::map<pmtv::pmt, std::vector<std::pair<gr::SettingsCtx, gr::property_map>>, gr::settings::PMTCompare>& stagedSettings) {
    for (const auto& [k, v] : settings) {
        m_settings[k] = v;
    }
    _storedSettings = stagedSettings;
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
        if (t == "double") {
            return dataset ? DataType::DataSetFloat64 : DataType::Float64;
        }
        if (t == "float") {
            return dataset ? DataType::DataSetFloat32 : DataType::Float32;
        }
        if (t == "std::uint64_t") {
            return DataType::UInt64;
        }
        if (t == "std::uint32_t" || t == "unsigned int") {
            return DataType::UInt32;
        }
        if (t == "std::uint16_t" || t == "unsigned short") {
            return DataType::UInt16;
        }
        if (t == "std::uint8_t") {
            return DataType::UInt8;
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
        if (t.empty()) {
            return DataType::Wildcard;
        }
        if (t == "untyped") {
            return DataType::Untyped;
        }

        throw gr::exception(fmt::format("unhandled data type: '{}'\n", t));
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
        auto msg = fmt::format("Cannot open file '{}'", file.native());
        components::Notification::error(msg);
        throw std::runtime_error(msg);
    }

    stream.seekg(0, std::ios::end);
    auto size = static_cast<std::size_t>(stream.tellg());

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

    gr::Graph grGraph = [this, &str]() -> gr::Graph {
        try {
            return gr::loadGrc(*_pluginLoader, str);
        } catch (const gr::exception&) {
            throw;
        } catch (const std::string& e) {
            throw gr::exception(e);
        } catch (...) {
            throw std::current_exception();
        }
    }();

    grGraph.forEachBlock([&](const auto& grBlock) {
        auto typeName               = grBlock.typeName();
        typeName                    = std::string_view(typeName.begin(), typeName.find('<'));
        const BlockDefinition* type = BlockRegistry::instance().get(typeName);
        if (!type) {
            auto msg = fmt::format("Block type '{}' is unknown.", typeName);
            components::Notification::error(msg);
            throw gr::exception(msg);
        }

        auto block               = type->createBlock(grBlock.name());
        block->m_uniqueName      = grBlock.uniqueName();
        block->m_metaInformation = grBlock.metaInformation();
        block->updateSettings(grBlock.settings().get(), grBlock.settings().getStoredAll());
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
                                      auto msg = fmt::format("Cannot connect, index {} is not valid (only {} ports available)", definition.topLevel, ports.size());
                                      components::Notification::error(msg);
#ifndef NDEBUG
                                      throw std::runtime_error(msg);
#endif
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
                                          auto msg = fmt::format("Invalid subindex in '{}'", definition.name);
                                          components::Notification::error(msg);
                                          throw std::runtime_error(msg);
                                      }
                                      return index;
                                  }();
                                  const auto it = std::ranges::find_if(ports, [&name](const auto& port) { return port.name == name; });
                                  if (it == ports.end()) {
                                      auto msg = fmt::format("Cannot connect, no port with name '{}'", name);
                                      components::Notification::error(msg);
                                  }

                                  // TODO check subIndex once we support port collections

                                  return std::pair{it, subIndex};
                              }),
            portDefinition.definition);
    };

    grGraph.forEachEdge([&](const auto& edge) {
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
    using namespace gr;
    property_map           yaml;
    std::vector<pmtv::pmt> blocks;

    for (auto& b : m_blocks) {
        property_map blockMap;
        blockMap["name"] = b->name;
        blockMap["id"]   = std::string(b->typeName());

        const auto& settings = b->settings();
        if (!settings.empty()) {
            blockMap["parameters"] = settings;
        }
        blocks.emplace_back(std::move(blockMap));
    }

    yaml["blocks"] = blocks;

    std::vector<pmtv::pmt> connections;
    if (!m_connections.empty()) {
        for (const auto& connection : m_connections) {
            std::vector<pmtv::pmt> pmtConnection;
            pmtConnection.emplace_back(connection.src.uiBlock->name);
            pmtConnection.emplace_back(std::int64_t(connection.src.index));
            pmtConnection.emplace_back(connection.dst.uiBlock->name);
            pmtConnection.emplace_back(std::int64_t(connection.dst.index));
            connections.emplace_back(std::move(pmtConnection));
        }
    }

    yaml["connections"] = connections;

    std::string outYaml = pmtv::yaml::serialize(yaml);
    stream << outYaml;
    return int(outYaml.size());
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

    const auto ain = std::size_t(a - a->owningUiBlock->outputs().data());
    const auto bin = std::size_t(b - b->owningUiBlock->inputs().data());

    if (a->rawPortType != b->rawPortType) {
        fmt::println("incompatible block connection: {}.{}({}) to {}.{}({})", //
            a->owningUiBlock->name, ain, a->rawPortType, b->owningUiBlock->name, bin, b->rawPortType);
    } else {
        fmt::println("connect {}.{}({}) to {}.{}({})", a->owningUiBlock->name, ain, a->rawPortType, b->owningUiBlock->name, bin, b->rawPortType);
    }

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
        auto msg = fmt::format("Could not create GR Block for {} ({}<{}>)", block.name, block.typeName(), instantiationName);
        components::Notification::error(msg);
        return nullptr;
    }

    grBlock->settings().set(params);
    // using StoredSettingsType = std::map<pmtv::pmt, std::vector<std::pair<gr::SettingsCtx, gr::property_map>>, gr::settings::PMTCompare>;
    for (const auto& [_, vec] : block.storedSettings()) {
        for (const auto& [ctx, map] : vec) {
            std::ignore = grBlock->settings().set(map, ctx);
        }
    }

    grBlock->settings().applyStagedParameters();
    return grBlock;
}

std::string_view sourceTypeForURI(std::string_view uriStr) {
    opencmw::URI<opencmw::RELAXED> uri{std::string(uriStr)};
    const auto                     params  = uri.queryParamMap();
    const auto                     acqMode = params.find("acquisitionModeFilter");
    if (acqMode != params.end() && acqMode->second && acqMode->second != "streaming") {
        return "opendigitizer::RemoteDataSetSource";
    }
    return "opendigitizer::RemoteStreamSource";
}

} // namespace

Block* FlowGraph::addRemoteSource(std::string_view uriStr) {
    auto block = BlockRegistry::instance().get(sourceTypeForURI(uriStr))->createBlock("Remote Source");
    block->updateSettings({{"remote_uri", std::string(uriStr)}});
    auto res = block.get();
    addBlock(std::move(block));
    return res;
}

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
        context.grGraph.addBlock(std::move(grBlock));
    }

    auto findBlock = [&](std::string_view name) -> gr::BlockModel* {
        const auto it = std::ranges::find_if(context.grGraph.blocks(), [&](auto& b) { return b->uniqueName() == name; });
        return it == context.grGraph.blocks().end() ? nullptr : it->get();
    };

    for (auto& c : m_connections) {
        const auto src = findBlock(c.src.uiBlock->m_uniqueName);
        const auto dst = findBlock(c.dst.uiBlock->m_uniqueName);
        if (!src || !dst) {
            continue;
        }

        context.grGraph.connect(*src, c.src.index, *dst, c.dst.index);
    }

    m_graphChanged = false;

    std::stringstream str;
    save(str);
    m_grc = str.str();

    return context;
}

void FlowGraph::handleMessage(const gr::Message& msg) {
    const bool consumed = graphModel.processMessage(msg);

    if (msg.serviceName == App::instance().schedulerUniqueName()) {
        return;
    }

    const auto it = std::ranges::find_if(m_blocks, [&](const auto& b) { return b->m_uniqueName == msg.serviceName; });
    if (it == m_blocks.end()) {
        if (consumed) {
            return;
        }

        auto error = fmt::format("Received settings for unknown block '{}'", msg.serviceName);
        components::Notification::error(error);
        return;
    }

    if (msg.endpoint == gr::block::property::kSetting) {
        if (!msg.data) {
            auto error = fmt::format("Received settings error for block '{}': {}", msg.serviceName, msg.data.error());
            components::Notification::error(error);
            return;
        }
        if (it->get()->typeName() == "opendigitizer::RemoteStreamSource" || it->get()->typeName() == "opendigitizer::RemoteDataSetSource") {
            Digitizer::Settings& settings = Digitizer::Settings::instance();
            // add remote flowgraph for remote data sources
            if (const auto remoteUri = std::get_if<std::string>(&msg.data.value().at("remote_uri"))) {
                std::optional<opencmw::URI<>> uri{};
                try {
                    uri = opencmw::URI<>(std::string(*remoteUri));
                    if (uri && (!uri->hostName().has_value() || uri->hostName()->empty())) {
                        if (!settings.hostname.empty() && settings.port != 0) {
                            uri = uri->factory().hostName(settings.hostname).port(settings.port).scheme(settings.disableHttps ? "http" : "https").build();
                        } else {
                            uri = {};
                        }
                    }
                } catch (const std::exception& e) {
                    auto msg = fmt::format("remote_source of '{}' is not a valid URI '{}': {}", it->get()->name, *remoteUri, e.what());
                    components::Notification::error(msg);
                    uri = {};
                }
                App::instance().dashboard->registerRemoteService(it->get()->name, uri);
            }
            // add settings for local host
            const std::string* host = std::get_if<std::string>(&msg.data.value().at("host"));
            if (!settings.hostname.empty() && settings.port != 0 && (!host || host->empty())) {
                std::string newHost = fmt::format("{}://{}{}", settings.disableHttps ? "http" : "https", settings.hostname, settings.port == 0 ? "" : fmt::format(":{}", settings.port));
                fmt::print("setting local service settings for remote source({}): {{host: {} }}\n", it->get()->name, newHost);
                (*it)->setSetting("host", newHost);
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
