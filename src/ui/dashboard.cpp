
#include "dashboard.h"

#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>

#include <fstream>

#include <yaml-cpp/yaml.h>

#include "flowgraph.h"
#include "flowgraph/datasink.h"
#include "yamlutils.h"

namespace DigitizerUi {

namespace {
template<typename T>
inline T randomRange(T min, T max) {
    T scale = rand() / (T) RAND_MAX;
    return min + scale * (max - min);
}

uint32_t randomColor() {
    uint8_t x = randomRange(0.0f, 255.0f);
    uint8_t y = randomRange(0.0f, 255.0f);
    uint8_t z = randomRange(0.0f, 255.0f);
    return x << 24 | y << 16 | z << 8 | 0xff;
}
} // namespace

Dashboard::Plot::Plot() {
    static int n = 1;
    name         = fmt::format("Plot {}", n++);
}

Dashboard::Dashboard(const std::shared_ptr<DashboardDescription> &desc, FlowGraph *fg)
    : m_desc(desc)
    , m_name(desc->name)
    , m_path(desc->source->path)
    , m_flowGraph(fg) {
    m_plots.resize(2);

    fg->sourceBlockAddedCallback = [this](Block *b) {
        for (int i = 0; i < b->type->outputs.size(); ++i) {
            auto name = fmt::format("{}.{}", b->name, b->type->outputs[i].name);
            m_sources.insert({ b, i, name, randomColor() });
        }
    };
    fg->blockDeletedCallback = [this](Block *b) {
        for (auto &p : m_plots) {
            p.sources.erase(std::remove_if(p.sources.begin(), p.sources.end(), [=](auto *s) { return s->block == b; }),
                    p.sources.end());
        }
        m_sources.erase(std::remove_if(m_sources.begin(), m_sources.end(), [=](const auto &s) { return s.block == b; }),
                m_sources.end());
    };

    fg->parse(std::filesystem::path(desc->source->path) / desc->flowgraphFile);
}

Dashboard::~Dashboard() {
}

void Dashboard::save() {
    m_flowGraph->save();
}

std::shared_ptr<DashboardDescription> DashboardSource::load(const std::string &filename) {
#ifndef EMSCRIPTEN
    auto          path = std::filesystem::path(this->path) / filename;
    std::ifstream stream(path, std::ios::in);
    if (!stream.is_open()) {
        return {};
    }
    YAML::Node tree      = YAML::Load(stream);

    auto       flowgraph = tree["flowgraph"];
    if (!flowgraph.IsScalar()) return {};

    auto favorite = tree["favorite"];
    auto lastUsed = tree["lastUsed"];

    auto getDate  = [](const auto &str) -> decltype(DashboardDescription::lastUsed) {
        if (str.size() < 10) {
            return {};
        }
        int                         year  = std::atoi(str.data());
        unsigned                    month = std::atoi(str.c_str() + 5);
        unsigned                    day   = std::atoi(str.c_str() + 8);

        std::chrono::year_month_day date{ std::chrono::year{ year }, std::chrono::month{ month }, std::chrono::day{ day } };
        return std::chrono::sys_days(date);
    };

    return std::make_shared<DashboardDescription>(DashboardDescription{ .name = path.stem(),
            .source                                                           = this,
            .flowgraphFile                                                    = flowgraph.as<std::string>(),
            .isFavorite                                                       = favorite.IsScalar() ? favorite.as<bool>() : false,
            .lastUsed                                                         = lastUsed.IsScalar() ? getDate(lastUsed.as<std::string>()) : std::nullopt });
#endif
    return {};
}

} // namespace DigitizerUi
