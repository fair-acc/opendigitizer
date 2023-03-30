
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

DashboardSource *unsavedSource() {
    static DashboardSource source = {
        .path    = "Unsaved",
        .isValid = false,
    };
    return &source;
}

} // namespace

Dashboard::Plot::Plot() {
    static int n = 1;
    name         = fmt::format("Plot {}", n++);
}

Dashboard::Dashboard(const std::shared_ptr<DashboardDescription> &desc, FlowGraph *fg)
    : m_desc(desc)
    , m_flowGraph(fg) {
    m_plots.resize(2);

    m_desc->lastUsed             = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());

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

    if (desc->source == unsavedSource()) {
        fg->clear();
    } else {
        fg->parse(std::filesystem::path(desc->source->path) / (desc->name + ".grc"));
    }
}

Dashboard::~Dashboard() {
}

void Dashboard::setNewDescription(const std::shared_ptr<DashboardDescription> &desc) {
    m_desc = desc;
}

void Dashboard::save() {
    if (!m_desc->source->isValid) {
        return;
    }

    auto path = std::filesystem::path(m_desc->source->path);
    m_flowGraph->save(path / (m_desc->name + ".grc"));

    YAML::Emitter out;
    {
        YamlMap root(out);

        root.write("favorite", m_desc->isFavorite);
        std::chrono::year_month_day ymd(std::chrono::floor<std::chrono::days>(m_desc->lastUsed.value()));
        char                        lastUsed[11];
        fmt::format_to(lastUsed, "{:02}/{:02}/{:04}", static_cast<unsigned>(ymd.day()), static_cast<unsigned>(ymd.month()), static_cast<int>(ymd.year()));
        root.write("lastUsed", lastUsed);
    }

    std::ofstream stream(path / (m_desc->name + DashboardDescription::fileExtension), std::ios::out | std::ios::trunc);
    if (!stream.is_open()) {
        return;
    }
    stream << out.c_str();
}

std::shared_ptr<DashboardDescription> DashboardSource::load(const std::string &filename) {
#ifndef EMSCRIPTEN
    auto          path = std::filesystem::path(this->path) / filename;
    std::ifstream stream(path, std::ios::in);
    if (!stream.is_open()) {
        return {};
    }
    YAML::Node tree      = YAML::Load(stream);

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

    return std::make_shared<DashboardDescription>(DashboardDescription{
            .name       = path.stem(),
            .source     = this,
            .isFavorite = favorite.IsScalar() ? favorite.as<bool>() : false,
            .lastUsed   = lastUsed.IsScalar() ? getDate(lastUsed.as<std::string>()) : std::nullopt });
#endif
    return {};
}

std::shared_ptr<DashboardDescription> DashboardDescription::createEmpty(const std::string &name) {
    return std::make_shared<DashboardDescription>(DashboardDescription{
            .name       = name,
            .source     = unsavedSource(),
            .isFavorite = false,
            .lastUsed   = std::nullopt });
}

} // namespace DigitizerUi
