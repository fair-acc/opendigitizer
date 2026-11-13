#ifndef OPENDIGITIZER_CHARTS_RDSVIEW_HPP
#define OPENDIGITIZER_CHARTS_RDSVIEW_HPP

#include "Chart.hpp"
#include "SignalSink.hpp"

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/digital/RDSDecoder.hpp>

#include "../common/ImguiWrap.hpp"
#include "../common/LookAndFeel.hpp"
#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendigitizer::charts {

struct RDSView : gr::Block<RDSView, gr::Drawable<gr::UICategory::Content, "ImGui">>, Chart {
    using Description = gr::Doc<R""(@brief Radio panel for the Radio Data System (RDS): station, now playing, earlier songs, alternative frequencies, traffic messages and reception quality.

Its signal is a TagSink after an RDSDecoder.
With `tuner` set, e.g. to "RTLSDR:frequency", the panel also tunes the radio: the tuner gets the station frequency minus `channel_frequency`.)"">;

    using RDSDecoder  = gr::digital::RDSDecoder;
    using LookAndFeel = DigitizerUi::LookAndFeel;
    using FontScope   = DigitizerUi::IMW::FontWithSize;
    using FontFace    = ImFont* (LookAndFeel::*)() const;

    struct TrafficEntry {
        double                     utcSeconds{};
        RDSDecoder::TrafficMessage message;
    };

    struct EarlierItem {
        double      utcSeconds{};
        std::string title;
        std::string artist;
    };

    struct QualitySample {
        double utcSeconds{};
        float  errorFreePercent{};
    };

    struct Reception {
        static constexpr std::size_t kMaxEarlier         = 50UZ;
        static constexpr std::size_t kMaxTrafficMessages = 100UZ;
        static constexpr std::size_t kMaxQualitySamples  = 60UZ;

        RDSDecoder::Station        station;
        bool                       synchronised         = false;
        double                     lastUpdateUtcSeconds = 0.0;
        double                     radioTextUtcSeconds  = 0.0;
        double                     clockUtcSeconds      = 0.0;
        std::vector<EarlierItem>   earlier;
        std::deque<TrafficEntry>   trafficMessages;
        std::deque<QualitySample>  quality;
        std::vector<std::uint32_t> groupCounts;
        std::vector<std::uint32_t> blockCounts;

        void add(double utcSeconds, const gr::property_map& tag) {
            if (tag.contains(RDSDecoder::kTrafficEventKey)) {
                addTrafficMessage(utcSeconds, tag);
            } else if (tag.contains(RDSDecoder::kProgrammeIdentificationKey)) {
                addStation(utcSeconds, tag);
            } else if (tag.contains(RDSDecoder::kSynchronisedKey)) {
                synchronised = tag.value_or<bool>(RDSDecoder::kSynchronisedKey, false);
            }
        }

        void addStation(double utcSeconds, const gr::property_map& tag) {
            synchronised                    = tag.value_or<bool>(RDSDecoder::kSynchronisedKey, true);
            RDSDecoder::Station next        = stationOf(tag);
            const bool          sameStation = next.programmeIdentification == station.programmeIdentification;
            if (next.radioText != station.radioText && !(sameStation && sameTextFilling(station.radioText, next.radioText))) {
                rememberNowPlaying();
                radioTextUtcSeconds = utcSeconds;
            }
            if (next.clockTime && next.clockTime != station.clockTime) {
                clockUtcSeconds = utcSeconds;
            }
            station     = std::move(next);
            groupCounts = countsOf(tag, RDSDecoder::kGroupCountsKey);
            addQualitySample(utcSeconds, countsOf(tag, RDSDecoder::kBlockCountsKey));
            lastUpdateUtcSeconds = utcSeconds;
        }

        void rememberNowPlaying() {
            EarlierItem previous = earlierItemOf(station, radioTextUtcSeconds);
            if (previous.title.empty()) {
                return;
            }
            std::erase_if(earlier, [&previous](const EarlierItem& item) { return item.title == previous.title && item.artist == previous.artist; });
            earlier.push_back(std::move(previous));
            if (earlier.size() > kMaxEarlier) {
                earlier.erase(earlier.begin());
            }
        }

        void forgetStation() {
            rememberNowPlaying();
            station              = {};
            synchronised         = false;
            lastUpdateUtcSeconds = 0.0;
            radioTextUtcSeconds  = 0.0;
            clockUtcSeconds      = 0.0;
            quality.clear();
            groupCounts.clear();
            blockCounts.clear();
        }

        void addQualitySample(double utcSeconds, std::vector<std::uint32_t> counts) {
            if (counts.size() == 3UZ && blockCounts.size() == 3UZ && std::ranges::all_of(std::views::iota(0UZ, 3UZ), [&](std::size_t i) { return counts[i] >= blockCounts[i]; })) {
                const std::uint32_t errorFree = counts[0] - blockCounts[0];
                const std::uint32_t total     = errorFree + (counts[1] - blockCounts[1]) + (counts[2] - blockCounts[2]);
                if (total > 0U) {
                    quality.push_back({.utcSeconds = utcSeconds, .errorFreePercent = 100.f * static_cast<float>(errorFree) / static_cast<float>(total)});
                    if (quality.size() > kMaxQualitySamples) {
                        quality.pop_front();
                    }
                }
            }
            blockCounts = std::move(counts);
        }

        void addTrafficMessage(double utcSeconds, const gr::property_map& tag) {
            trafficMessages.push_back({
                .utcSeconds = utcSeconds,
                .message =
                    {
                        .event     = tag.value_or<std::uint16_t>(RDSDecoder::kTrafficEventKey, 0U),
                        .location  = tag.value_or<std::uint16_t>(RDSDecoder::kTrafficLocationKey, 0U),
                        .extent    = tag.value_or<std::uint8_t>(RDSDecoder::kTrafficExtentKey, 0U),
                        .direction = tag.value_or<bool>(RDSDecoder::kTrafficDirectionKey, false),
                        .duration  = knownOf<std::uint8_t>(tag, RDSDecoder::kTrafficDurationKey),
                    },
            });
            if (trafficMessages.size() > kMaxTrafficMessages) {
                trafficMessages.pop_front();
            }
        }

        [[nodiscard]] static EarlierItem earlierItemOf(const RDSDecoder::Station& station, double utcSeconds) {
            if (!station.itemTitle.empty()) {
                return {.utcSeconds = utcSeconds, .title = station.itemTitle, .artist = station.itemArtist};
            }
            return {.utcSeconds = utcSeconds, .title = station.radioText, .artist = {}};
        }

        [[nodiscard]] static RDSDecoder::Station stationOf(const gr::property_map& tag) {
            return {
                .programmeIdentification = tag.value_or<std::uint16_t>(RDSDecoder::kProgrammeIdentificationKey, 0U),
                .programmeType           = tag.value_or<std::uint8_t>(RDSDecoder::kProgrammeTypeKey, 0U),
                .programmeServiceName    = textOf(tag, RDSDecoder::kProgrammeServiceNameKey),
                .radioText               = textOf(tag, RDSDecoder::kRadioTextKey),
                .trafficProgramme        = tag.value_or<bool>(RDSDecoder::kTrafficProgrammeKey, false),
                .trafficAnnouncement     = knownOf<bool>(tag, RDSDecoder::kTrafficAnnouncementKey),
                .music                   = knownOf<bool>(tag, RDSDecoder::kMusicKey),
                .clockTime               = knownOf<std::int64_t>(tag, RDSDecoder::kClockTimeKey),
                .localTimeOffset         = tag.value_or<std::int32_t>(RDSDecoder::kLocalTimeOffsetKey, 0),
                .programmeTypeName       = textOf(tag, RDSDecoder::kProgrammeTypeNameKey),
                .stereo                  = knownOf<bool>(tag, RDSDecoder::kStereoKey),
                .dynamicProgrammeType    = knownOf<bool>(tag, RDSDecoder::kDynamicProgrammeTypeKey),
                .extendedCountryCode     = tag.value_or<std::uint8_t>(RDSDecoder::kExtendedCountryCodeKey, 0U),
                .alternativeFrequencies  = frequenciesOf(tag),
                .itemArtist              = textOf(tag, RDSDecoder::kItemArtistKey),
                .itemTitle               = textOf(tag, RDSDecoder::kItemTitleKey),
                .otherNetworks           = otherNetworksOf(tag),
            };
        }

        [[nodiscard]] static std::string textOf(const gr::property_map& map, std::string_view key) { return std::string(map.value_or<std::string_view>(key, "")); }

        template<typename T>
        [[nodiscard]] static std::optional<T> knownOf(const gr::property_map& map, std::string_view key) {
            return map.contains(key) ? std::optional(map.value_or<T>(key, T{})) : std::nullopt;
        }

        [[nodiscard]] static std::vector<float> frequenciesOf(const gr::property_map& map) {
            const auto frequencies = map.value_or<gr::Tensor<float>>(RDSDecoder::kAlternativeFrequenciesKey, gr::Tensor<float>{});
            return {frequencies.begin(), frequencies.end()};
        }

        [[nodiscard]] static std::vector<std::uint32_t> countsOf(const gr::property_map& map, std::string_view key) {
            const auto counts = map.value_or<gr::Tensor<std::uint32_t>>(key, gr::Tensor<std::uint32_t>{});
            return {counts.begin(), counts.end()};
        }

        [[nodiscard]] static std::map<std::uint16_t, RDSDecoder::OtherNetwork> otherNetworksOf(const gr::property_map& tag) {
            std::map<std::uint16_t, RDSDecoder::OtherNetwork> networks;
            const auto                                        entries = tag.value_or<gr::property_map>(RDSDecoder::kOtherNetworksKey, gr::property_map{});
            for (const auto& [key, value] : entries) {
                const std::string_view code{key};
                std::uint16_t          programmeIdentification = 0U;
                if (std::from_chars(code.data(), code.data() + code.size(), programmeIdentification, 16).ec != std::errc{}) {
                    continue;
                }
                const gr::Value entry = value;
                const auto      view  = entry.get_if<gr::property_map>();
                if (!view) {
                    continue;
                }
                const gr::property_map network    = view->owned();
                networks[programmeIdentification] = {
                    .programmeServiceName   = textOf(network, RDSDecoder::kProgrammeServiceNameKey),
                    .alternativeFrequencies = frequenciesOf(network),
                    .trafficProgramme       = network.value_or<bool>(RDSDecoder::kTrafficProgrammeKey, false),
                    .trafficAnnouncement    = knownOf<bool>(network, RDSDecoder::kTrafficAnnouncementKey),
                    .programmeType          = knownOf<std::uint8_t>(network, RDSDecoder::kProgrammeTypeKey),
                };
            }
            return networks;
        }

        [[nodiscard]] static bool sameTextFilling(std::string_view earlier, std::string_view later) {
            constexpr char kMissing = RDSDecoder::kMissingCharacter;
            if (earlier.empty() || later.empty()) {
                return false;
            }
            const std::size_t common = std::min(earlier.size(), later.size());
            return std::ranges::all_of(std::views::iota(0UZ, common), [&](std::size_t i) { return earlier[i] == later[i] || earlier[i] == kMissing || later[i] == kMissing; });
        }
    };

    struct Colours {
        ImU32 text;
        ImU32 secondary;
        ImU32 accent;
        ImU32 success;
        ImU32 warning;
        ImU32 error;
        ImU32 subtle;
        ImU32 border;
    };

    struct Area {
        float left;
        float right;

        [[nodiscard]] float width() const noexcept { return right - left; }
    };

    struct Pill {
        std::string label;
        ImU32       colour;
        bool        on;
    };

    struct TunerTarget {
        std::string block;
        std::string setting;
    };

    struct HeardStation {
        double        frequency{};
        std::uint16_t programmeIdentification{};
        std::string   name;
    };

    static constexpr std::array<std::string_view, 32UZ> kProgrammeTypeNames{"no programme type", "news", "current affairs", "information", "sport", "education", "drama", "culture", "science", "varied", "pop music", "rock music", "easy listening", "light classical", "serious classical", "other music", "weather", "finance", "children's programmes", "social affairs", "religion", "phone-in", "travel", "leisure", "jazz music", "country music", "national music", "oldies music", "folk music", "documentary", "alarm test", "alarm"};
    static constexpr std::array<std::string_view, 6UZ>  kTrafficColumns{"RECEIVED", "EVENT", "LOCATION", "EXTENT", "DIRECTION", "DURATION"};
    static constexpr double                             kBitRateInterval     = 1.0;
    static constexpr double                             kRetuneSettleSeconds = 1.0;
    static constexpr std::string_view                   kSeparator           = " · ";
    static constexpr std::string_view                   kMissingShown        = "·";
    static constexpr std::string_view                   kNoName              = "--------";
    static constexpr float                              kRounding            = 6.f;
    static constexpr ImVec2                             kPillPadding         = {8.f, 2.f};
    static constexpr float                              kTint                = .15f;
    static constexpr float                              kSubtle              = .06f;
    static constexpr float                              kDotRadius           = 4.f;
    static constexpr float                              kLineWidth           = 2.f;
    static constexpr float                              kGraphLines          = 3.f;
    static constexpr float                              kSingleColumnWidth   = 640.f;
    static constexpr double                             kBandLow             = 87.5e6;
    static constexpr double                             kBandHigh            = 108e6;
    static constexpr int                                kMajorTickMHz        = 5;

    template<typename T, gr::meta::fixed_string description = "", typename... Arguments>
    using A = gr::Annotated<T, description, Arguments...>;

    A<std::string, "chart name", gr::Visible>                                                                                                          chart_name;
    A<std::string, "chart title">                                                                                                                      chart_title;
    A<std::vector<std::string>, "data sinks", gr::Visible>                                                                                             data_sinks     = {};
    A<gr::Size_t, "history length", gr::Visible, gr::Doc<"earlier songs or radio texts shown, newest first">>                                          history_length = 20U;
    A<std::string, "tuner", gr::Visible, gr::Doc<"frequency setting to tune as 'block:setting', e.g. 'RTLSDR:frequency'; empty hides the tuner">>      tuner;
    A<double, "station frequency", gr::Unit<"Hz">, gr::Visible, gr::Doc<"station tuned when the panel starts">>                                        station_frequency = 0.0;
    A<double, "channel frequency", gr::Unit<"Hz">, gr::Doc<"shift of the channel from the tuner centre, e.g. a FrequencyXlatingFilter's 'frequency'">> channel_frequency = 0.0;
    A<double, "tuning step", gr::Unit<"Hz">, gr::Doc<"step of the tuning buttons and the band dial">>                                                  tuning_step       = 100'000.0;

    static constexpr SignalKind supportedSignals = SignalKind::Streaming;

    GR_MAKE_REFLECTABLE(RDSView, chart_name, chart_title, data_sinks, history_length, tuner, station_frequency, channel_frequency, tuning_step);

    Reception                            _reception;
    double                               _lastTagUtcSeconds = 0.0;
    std::size_t                          _bitsAtRateStart   = 0UZ;
    double                               _rateStartTime     = 0.0;
    double                               _bitRate           = 0.0;
    double                               _tunedFrequency    = 0.0;
    double                               _tunedUtcSeconds   = 0.0;
    std::optional<double>                _tuneRequest;
    std::map<std::int64_t, HeardStation> _heardStationsByKilohertz;
    Colours                              _colours{};

    static constexpr std::string_view kChartTypeName = "RDSView";

    [[nodiscard]] static constexpr std::string_view chartTypeName() noexcept { return kChartTypeName; }
    [[nodiscard]] std::string_view                  uniqueId() const noexcept { return this->unique_name; }

    gr::work::Result work(std::size_t = std::numeric_limits<std::size_t>::max(), gr::device::DeviceContext& = gr::device::hostBackend()) noexcept { return {0UZ, 0UZ, gr::work::Status::OK}; }

    void settingsChanged(const gr::property_map& oldSettings, const gr::property_map& newSettings) {
        handleSettingsChanged(newSettings);
        const auto changeOf = [&oldSettings](std::string_view key, double value) { return value - oldSettings.value_or<double>(key, value); };
        if (changeOf("station_frequency", station_frequency.value) != 0.0) {
            _tunedFrequency = station_frequency.value;
        } else if (const double channelMove = changeOf("channel_frequency", channel_frequency.value); channelMove != 0.0 && tunerActive()) {
            changeStation(_tunedFrequency + channelMove);
        }
    }

    gr::work::Status draw(const gr::property_map& config = {}) {
        [[maybe_unused]] auto [plotFlags, plotSize, showLegend, chartMode, showGrid, chartStyle] = prepareDrawPrologue(config);

        std::size_t bitCount = 0UZ;
        if (!_signalSinks.empty() && _signalSinks.front()) {
            const auto& sink   = _signalSinks.front();
            auto        guard  = sink->dataGuard();
            double      newest = _lastTagUtcSeconds;
            sink->forEachTag([this, &newest](double utcSeconds, const gr::property_map& tag) {
                if (utcSeconds > _lastTagUtcSeconds) {
                    if (utcSeconds >= _tunedUtcSeconds + kRetuneSettleSeconds) {
                        _reception.add(utcSeconds, tag);
                    }
                    newest = std::max(newest, utcSeconds);
                }
            });
            _lastTagUtcSeconds = newest;
            bitCount           = sink->totalSampleCount();
        }
        updateBitRate(bitCount);
        rememberStation();

        _colours                    = coloursOf();
        const double            now = utcNow();
        DigitizerUi::IMW::Child panel("##rdsPanel", plotSize, false, ImGuiWindowFlags_NoScrollbar);
        float                   statusHeight = 0.f;
        {
            FontScope fontScope = font(&LookAndFeel::fontSmall);
            statusHeight        = ImGui::GetTextLineHeight() + ImGui::GetStyle().ItemSpacing.y;
        }
        if (tunerActive()) {
            card(ImGui::GetContentRegionAvail().x, [this](const Area& area) { drawTuner(area); });
        }
        if (_reception.lastUpdateUtcSeconds == 0.0) {
            drawWaiting(std::max(0.f, ImGui::GetContentRegionAvail().y - statusHeight));
        } else {
            drawBody(statusHeight, now);
        }
        drawStatusLine(now);
        if (_tuneRequest.has_value()) {
            tune(*_tuneRequest);
            _tuneRequest.reset();
        }
        return gr::work::Status::OK;
    }

    [[nodiscard]] static std::optional<TunerTarget> tunerTargetOf(std::string_view target) {
        const std::size_t colon = target.rfind(':');
        if (colon == std::string_view::npos || colon == 0UZ || colon + 1UZ == target.size()) {
            return std::nullopt;
        }
        return TunerTarget{.block = std::string(target.substr(0UZ, colon)), .setting = std::string(target.substr(colon + 1UZ))};
    }

    [[nodiscard]] static double snappedFrequency(double frequency, double step, double lowest, double highest) {
        const double snapped = step > 0.0 ? std::round(frequency / step) * step : frequency;
        return std::clamp(snapped, lowest, highest);
    }

    [[nodiscard]] bool tunerActive() const { return _tunedFrequency > 0.0 && tunerTargetOf(tuner.value).has_value(); }

    [[nodiscard]] bool isTuned(double frequency) const { return std::abs(frequency - _tunedFrequency) < .5 * std::max(tuning_step.value, 1.0); }

    [[nodiscard]] double gridFrequency(double frequency) const { return snappedFrequency(frequency, tuning_step.value, kBandLow, kBandHigh); }

    void tune(double frequency) {
        const auto target = tunerTargetOf(tuner.value);
        if (!target) {
            return;
        }
        changeStation(gridFrequency(frequency));
        gr::emit<gr::message::Command::Set>(this->msgOut, gr::property_map{{std::pmr::string(target->setting), _tunedFrequency - channel_frequency.value}}, {.endpoint = gr::block::property::kSetting, .serviceName = target->block});
    }

    void changeStation(double frequency) {
        const bool otherStation = gridFrequency(frequency) != gridFrequency(_tunedFrequency);
        _tunedFrequency         = frequency;
        if (otherStation) {
            _tunedUtcSeconds = utcNow();
            _reception.forgetStation();
        }
    }

    void rememberStation() {
        if (!tunerActive() || _reception.lastUpdateUtcSeconds == 0.0) {
            return;
        }
        const std::string_view stationName = trimmed(_reception.station.programmeServiceName);
        if (stationName.empty() || stationName.find(RDSDecoder::kMissingCharacter) != std::string_view::npos) {
            return;
        }
        const double frequency                                   = gridFrequency(_tunedFrequency);
        _heardStationsByKilohertz[std::llround(frequency / 1e3)] = {.frequency = frequency, .programmeIdentification = _reception.station.programmeIdentification, .name = std::string(stationName)};
    }

    [[nodiscard]] static std::string_view programmeTypeName(std::uint8_t programmeType) noexcept { return programmeType < kProgrammeTypeNames.size() ? kProgrammeTypeNames[programmeType] : std::string_view{}; }

    [[nodiscard]] static std::string frequencyList(const std::vector<float>& frequencies) {
        if (frequencies.empty()) {
            return {};
        }
        std::string list;
        for (const float frequency : frequencies) {
            list += std::format("{}{:.1f}", list.empty() ? "" : kSeparator, frequency);
        }
        return list + " MHz";
    }

    [[nodiscard]] static std::string groupName(std::size_t groupCode) { return std::format("{}{}", groupCode >> 1U, (groupCode & 1U) != 0U ? 'B' : 'A'); }

    [[nodiscard]] static std::string durationText(double seconds) {
        seconds = std::max(0.0, seconds);
        if (seconds < 60.0) {
            return std::format("{:.0f} s", seconds);
        }
        if (seconds < 3600.0) {
            return std::format("{:.0f} min", seconds / 60.0);
        }
        return std::format("{:.0f} h", seconds / 3600.0);
    }

    [[nodiscard]] static std::string ageText(double seconds) { return seconds < 5.0 ? std::string("now") : durationText(seconds) + " ago"; }

    [[nodiscard]] static std::chrono::sys_seconds localClock(std::int64_t clockTime, std::int32_t localTimeOffset, double secondsSinceReceived) {
        using namespace std::chrono;
        return sys_seconds{seconds{clockTime}} + minutes{localTimeOffset} + seconds{static_cast<std::int64_t>(std::max(0.0, secondsSinceReceived))};
    }

    [[nodiscard]] static std::string clockTimeText(std::int64_t clockTime, std::int32_t localTimeOffset, double secondsSinceReceived) { return std::format("{:%H:%M:%S}", localClock(clockTime, localTimeOffset, secondsSinceReceived)); }

    [[nodiscard]] static std::string clockDateText(std::int64_t clockTime, std::int32_t localTimeOffset, double secondsSinceReceived) {
        const std::int32_t offset     = std::abs(localTimeOffset);
        const std::string  zoneMinute = offset % 60 == 0 ? std::string{} : std::format(":{:02}", offset % 60);
        return std::format("{:%Y-%m-%d}{}UTC{}{}{}", localClock(clockTime, localTimeOffset, secondsSinceReceived), kSeparator, localTimeOffset < 0 ? '-' : '+', offset / 60, zoneMinute);
    }

    [[nodiscard]] static std::string percentText(std::uint32_t part, std::uint32_t total) { return total == 0U ? std::string("-") : std::format("{:.0f} %", 100.0 * static_cast<double>(part) / static_cast<double>(total)); }

    [[nodiscard]] static std::string shownText(std::string_view value) {
        std::string shown;
        shown.reserve(value.size());
        for (const char character : value) {
            if (character == RDSDecoder::kMissingCharacter) {
                shown += kMissingShown;
            } else {
                shown += character;
            }
        }
        return shown;
    }

    [[nodiscard]] static std::string_view trimmed(std::string_view value) {
        const std::size_t first = value.find_first_not_of(' ');
        return first == std::string_view::npos ? std::string_view{} : value.substr(first, value.find_last_not_of(' ') - first + 1UZ);
    }

    void updateBitRate(std::size_t bitCount) {
        const double now = ImGui::GetTime();
        if (_rateStartTime == 0.0 || bitCount < _bitsAtRateStart) {
            _bitsAtRateStart = bitCount;
            _rateStartTime   = now;
            return;
        }
        if (now - _rateStartTime < kBitRateInterval) {
            return;
        }
        _bitRate         = static_cast<double>(bitCount - _bitsAtRateStart) / (now - _rateStartTime);
        _bitsAtRateStart = bitCount;
        _rateStartTime   = now;
    }

    [[nodiscard]] static ImU32 withAlpha(ImU32 colour, float alpha) {
        ImVec4 value = ImGui::ColorConvertU32ToFloat4(colour);
        value.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    [[nodiscard]] static Colours coloursOf() {
        const auto& palette    = LookAndFeel::instance().palette();
        const ImU32 textColour = ImGui::GetColorU32(ImGuiCol_Text);
        return {
            .text      = textColour,
            .secondary = ImGui::GetColorU32(ImGuiCol_TextDisabled),
            .accent    = ImGui::GetColorU32(ImGuiCol_CheckMark),
            .success   = ImGui::GetColorU32(palette.successColor),
            .warning   = ImGui::GetColorU32(palette.warningColor),
            .error     = ImGui::GetColorU32(palette.errorColor),
            .subtle    = withAlpha(textColour, kSubtle),
            .border    = ImGui::GetColorU32(ImGuiCol_Border),
        };
    }

    [[nodiscard]] static FontScope font(FontFace face) {
        const LookAndFeel& lnf    = LookAndFeel::instance();
        ImFont*            chosen = (lnf.*face)();
        return FontScope(chosen, lnf.relativeFontSize(chosen));
    }

    [[nodiscard]] static ImVec2 textSize(std::string_view value) { return ImGui::CalcTextSize(value.data(), value.data() + value.size()); }

    [[nodiscard]] static float localX(float screenX) { return screenX - ImGui::GetWindowPos().x + ImGui::GetScrollX(); }

    [[nodiscard]] static std::string countTitle(std::string_view title, std::size_t count) { return std::format("{}{}{}", title, kSeparator, count); }

    [[nodiscard]] static double hertzOf(float megahertz) { return 1e6 * static_cast<double>(megahertz); }

    static void text(std::string_view value, ImU32 colour) {
        if (value.empty()) {
            return;
        }
        DigitizerUi::IMW::StyleNamedColor scoped(ImGuiCol_Text, colour);
        ImGui::TextUnformatted(value.data(), value.data() + value.size());
    }

    static void textAt(ImVec2 position, ImU32 colour, std::string_view value) {
        if (!value.empty()) {
            ImGui::GetWindowDrawList()->AddText(position, colour, value.data(), value.data() + value.size());
        }
    }

    static void flowNext(float right, float width) {
        ImGui::SameLine();
        if (ImGui::GetCursorScreenPos().x + width > right) {
            ImGui::NewLine();
        }
    }

    static void alignRight(float right, float width) {
        flowNext(right, width);
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(std::max(cursor.x, right - width), cursor.y));
    }

    template<typename Content>
    void card(float width, Content&& content) const {
        const ImVec2 padding = ImGui::GetStyle().WindowPadding;
        const ImVec2 min     = ImGui::GetCursorScreenPos();
        const Area   area{.left = min.x + padding.x, .right = min.x + width - padding.x};
        ImGui::SetCursorScreenPos(min + padding);
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(localX(area.right));
        content(area);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        const ImVec2 max(min.x + width, ImGui::GetItemRectMax().y + padding.y);
        ImGui::GetWindowDrawList()->AddRect(min, max, _colours.border, kRounding);
        ImGui::SetCursorScreenPos(min);
        ImGui::Dummy(max - min);
    }

    void cardHeader(const Area& area, std::string_view title, std::string_view aside = {}) const {
        FontScope fontScope = font(&LookAndFeel::fontTiny);
        text(title, _colours.secondary);
        if (!aside.empty()) {
            alignRight(area.right, textSize(aside).x);
            text(aside, _colours.secondary);
        }
    }

    void note(std::string_view value) const {
        FontScope fontScope = font(&LookAndFeel::fontSmall);
        text(value, _colours.secondary);
    }

    [[nodiscard]] static ImVec2 pillSize(std::string_view label) { return textSize(label) + kPillPadding * 2.f; }

    bool pill(std::string_view label, ImU32 colour, bool on, bool clickable = false) const {
        const ImVec2  min = ImGui::GetCursorScreenPos();
        const ImRect  box(min, min + pillSize(label));
        const ImGuiID id = clickable ? ImGui::GetID(label.data(), label.data() + label.size()) : 0U;
        ImGui::ItemSize(box.GetSize(), kPillPadding.y);
        if (!ImGui::ItemAdd(box, id)) {
            return false;
        }
        bool       hovered = false;
        bool       held    = false;
        const bool pressed = clickable && ImGui::ButtonBehavior(box, id, &hovered, &held);
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float rounding = .5f * box.GetHeight();
        drawList->AddRectFilled(box.Min, box.Max, on ? withAlpha(colour, kTint) : _colours.subtle, rounding);
        drawList->AddRect(box.Min, box.Max, (hovered || on) ? colour : _colours.border, rounding);
        textAt(min + kPillPadding, on ? colour : _colours.secondary, label);
        return pressed;
    }

    std::optional<std::size_t> pillFlow(const Area& area, const std::vector<Pill>& pills, bool clickable, const char* id) const {
        FontScope                     fontScope = font(&LookAndFeel::fontSmall);
        DigitizerUi::IMW::ChangeStrId flowScope(id);
        std::optional<std::size_t>    clicked;
        for (std::size_t i = 0UZ; i < pills.size(); ++i) {
            if (i > 0UZ) {
                flowNext(area.right, pillSize(pills[i].label).x);
            }
            DigitizerUi::IMW::ChangeId pillScope(static_cast<int>(i));
            if (pill(pills[i].label, pills[i].colour, pills[i].on, clickable)) {
                clicked = i;
            }
        }
        return clicked;
    }

    static void alignTextToPills() {
        ImGuiWindow* window               = ImGui::GetCurrentWindow();
        window->DC.CurrLineSize.y         = std::max(window->DC.CurrLineSize.y, ImGui::GetTextLineHeight() + 2.f * kPillPadding.y);
        window->DC.CurrLineTextBaseOffset = std::max(window->DC.CurrLineTextBaseOffset, kPillPadding.y);
    }

    void drawStation(const Area& area, double now) const {
        const auto&                 station     = _reception.station;
        const std::string_view      stationName = trimmed(station.programmeServiceName);
        const std::string_view      typeName    = programmeTypeName(station.programmeType);
        const std::array<Pill, 4UZ> flags{{
            {.label = "TP", .colour = _colours.success, .on = station.trafficProgramme},
            {.label = "TA", .colour = _colours.warning, .on = station.trafficAnnouncement.value_or(false)},
            {.label = "stereo", .colour = _colours.accent, .on = station.stereo.value_or(false)},
            {.label = station.music.value_or(true) ? "music" : "speech", .colour = _colours.accent, .on = station.music.has_value()},
        }};
        std::string                 tooltip;
        {
            FontScope fontScope = font(&LookAndFeel::fontBig);
            alignTextToPills();
            if (station.clockTime) {
                const double age = now - _reception.clockUtcSeconds;
                text(clockTimeText(*station.clockTime, station.localTimeOffset, age), _colours.secondary);
                if (ImGui::IsItemHovered()) {
                    tooltip = clockDateText(*station.clockTime, station.localTimeOffset, age);
                }
                ImGui::SameLine();
            }
            text(stationName.empty() ? std::string(kNoName) : shownText(stationName), _colours.text);
            if (ImGui::IsItemHovered()) {
                tooltip = std::format("PI {:04X}", station.programmeIdentification);
                if (station.extendedCountryCode != 0U) {
                    tooltip += std::format("{}ECC {:02X}", kSeparator, station.extendedCountryCode);
                }
            }
            flowNext(area.right, pillSize(typeName).x);
            pill(typeName, _colours.accent, true);
            if (ImGui::IsItemHovered() && !station.programmeTypeName.empty()) {
                tooltip = station.programmeTypeName;
            }
            for (const Pill& flag : flags) {
                flowNext(area.right, pillSize(flag.label).x);
                pill(flag.label, flag.colour, flag.on);
            }
        }
        if (!tooltip.empty()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    void drawNowPlaying(const Area& area, double now) const {
        const auto&       station = _reception.station;
        const std::string since   = _reception.radioTextUtcSeconds > 0.0 ? std::format("since {}", durationText(now - _reception.radioTextUtcSeconds)) : std::string{};
        cardHeader(area, "NOW PLAYING", since);
        {
            FontScope fontScope = font(&LookAndFeel::fontBigger);
            text(station.itemTitle, _colours.text);
        }
        {
            FontScope fontScope = font(&LookAndFeel::fontBig);
            text(station.itemArtist, _colours.accent);
        }
        if (station.radioText.empty()) {
            note("no radio text yet");
            return;
        }
        const bool hasItem   = !station.itemTitle.empty() || !station.itemArtist.empty();
        FontScope  fontScope = font(hasItem ? &LookAndFeel::fontSmall : &LookAndFeel::fontBig);
        text(shownText(station.radioText), hasItem ? _colours.secondary : _colours.text);
    }

    void drawEarlier(const Area& area, double now) const {
        cardHeader(area, countTitle("EARLIER", _reception.earlier.size()));
        if (_reception.earlier.empty()) {
            note("nothing yet");
            return;
        }
        for (const EarlierItem& item : _reception.earlier | std::views::reverse | std::views::take(history_length.value)) {
            const bool  plain    = item.artist.empty();
            const float top      = ImGui::GetCursorScreenPos().y;
            float       ageWidth = 0.f;
            {
                FontScope         fontScope = font(&LookAndFeel::fontSmall);
                const std::string age       = durationText(now - item.utcSeconds);
                ageWidth                    = textSize(age).x;
                textAt(ImVec2(area.right - ageWidth, top), _colours.secondary, age);
            }
            ImGui::PushTextWrapPos(localX(area.right - ageWidth - ImGui::GetStyle().ItemSpacing.x));
            {
                FontScope fontScope = font(plain ? &LookAndFeel::fontSmall : &LookAndFeel::fontNormal);
                text(item.title, plain ? _colours.secondary : _colours.text);
            }
            if (!plain) {
                FontScope fontScope = font(&LookAndFeel::fontSmall);
                text(item.artist, _colours.secondary);
            }
            ImGui::PopTextWrapPos();
        }
    }

    void drawTuner(const Area& area) {
        {
            FontScope fontScope = font(&LookAndFeel::fontBigger);
            if (ImGui::ArrowButton("##rdsTuneDown", ImGuiDir_Left)) {
                _tuneRequest = _tunedFrequency - tuning_step.value;
            }
            ImGui::SameLine();
            text(std::format("{:.1f} MHz", _tunedFrequency / 1e6), _colours.text);
            ImGui::SameLine();
            if (ImGui::ArrowButton("##rdsTuneUp", ImGuiDir_Right)) {
                _tuneRequest = _tunedFrequency + tuning_step.value;
            }
        }
        {
            FontScope      fontScope = font(&LookAndFeel::fontTiny);
            constexpr auto hint      = std::string_view("click, drag or scroll the band to tune");
            alignRight(area.right, textSize(hint).x);
            text(hint, _colours.secondary);
        }
        drawDial(area);
        drawHeardStations(area);
    }

    void drawDial(const Area& area) {
        FontScope    fontScope = font(&LookAndFeel::fontTiny);
        ImDrawList*  drawList  = ImGui::GetWindowDrawList();
        const float  line      = ImGui::GetTextLineHeight();
        const ImVec2 top       = ImGui::GetCursorScreenPos();
        const float  axisY     = top.y + kGraphLines * line;
        const auto   xOf       = [&area](double frequency) { return area.left + static_cast<float>((frequency - kBandLow) / (kBandHigh - kBandLow)) * area.width(); };

        ImGui::InvisibleButton("##rdsDial", ImVec2(std::max(1.f, area.width()), axisY + line - top.y));
        const bool   hovered = ImGui::IsItemHovered();
        const bool   active  = ImGui::IsItemActive();
        const float  wheel   = ImGui::GetIO().MouseWheel;
        const double pointed = snappedFrequency(kBandLow + static_cast<double>((ImGui::GetIO().MousePos.x - area.left) / area.width()) * (kBandHigh - kBandLow), tuning_step.value, kBandLow, kBandHigh);
        if (ImGui::IsItemDeactivated()) {
            _tuneRequest = pointed;
        } else if (hovered && wheel != 0.f) {
            _tuneRequest = _tunedFrequency + (wheel > 0.f ? tuning_step.value : -tuning_step.value);
        }

        drawList->AddLine(ImVec2(area.left, axisY), ImVec2(area.right, axisY), _colours.border, kLineWidth);
        const auto label = [&](double frequency, float anchor) {
            const std::string value = std::format("{:g}", frequency / 1e6);
            textAt(ImVec2(xOf(frequency) - anchor * textSize(value).x, axisY), _colours.secondary, value);
        };
        for (auto megahertz = static_cast<int>(std::ceil(kBandLow / 1e6)); megahertz <= static_cast<int>(kBandHigh / 1e6); ++megahertz) {
            const bool  major = megahertz % kMajorTickMHz == 0;
            const float x     = xOf(1e6 * megahertz);
            drawList->AddLine(ImVec2(x, axisY), ImVec2(x, axisY - (major ? .5f : .25f) * line), _colours.secondary);
            if (major) {
                label(1e6 * megahertz, .5f);
            }
        }
        label(kBandLow, 0.f);
        label(kBandHigh, 1.f);

        for (const HeardStation& heard : _heardStationsByKilohertz | std::views::values) {
            drawList->AddCircleFilled(ImVec2(xOf(heard.frequency), axisY), kDotRadius, _colours.secondary);
        }
        if (hovered && !active) {
            const float x = xOf(pointed);
            drawList->AddLine(ImVec2(x, top.y), ImVec2(x, axisY), _colours.secondary, kLineWidth);
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        const ImVec2 needleHead(xOf(active ? pointed : _tunedFrequency), top.y + kDotRadius);
        drawList->AddLine(needleHead, ImVec2(needleHead.x, axisY), _colours.accent, kLineWidth);
        drawList->AddCircleFilled(needleHead, kDotRadius, _colours.accent);
        if (hovered || active) {
            const auto        known   = _heardStationsByKilohertz.find(std::llround(pointed / 1e3));
            const std::string tooltip = known == _heardStationsByKilohertz.end() ? std::format("{:.1f} MHz", pointed / 1e6) : std::format("{:.1f} MHz{}{}", pointed / 1e6, kSeparator, known->second.name);
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    void drawHeardStations(const Area& area) {
        std::vector<Pill>   pills;
        std::vector<double> frequencies;
        for (const HeardStation& heard : _heardStationsByKilohertz | std::views::values) {
            pills.push_back({.label = std::format("{} {:.1f}", heard.name, heard.frequency / 1e6), .colour = _colours.accent, .on = isTuned(heard.frequency)});
            frequencies.push_back(heard.frequency);
        }
        if (const auto clicked = pillFlow(area, pills, true, "##rdsHeard"); clicked.has_value()) {
            _tuneRequest = frequencies[*clicked];
        }
    }

    void drawFrequencies(const Area& area) {
        const auto& frequencies = _reception.station.alternativeFrequencies;
        const bool  tunable     = tunerActive();
        cardHeader(area, countTitle("ALTERNATIVE FREQUENCIES (MHz)", frequencies.size()), tunable ? "click to tune" : "");
        if (frequencies.empty()) {
            note("none received yet");
            return;
        }
        std::vector<Pill> pills;
        pills.reserve(frequencies.size());
        for (const float frequency : frequencies) {
            pills.push_back({.label = std::format("{:.1f}", frequency), .colour = _colours.accent, .on = tunable && isTuned(hertzOf(frequency))});
        }
        if (const auto clicked = pillFlow(area, pills, tunable, "##rdsAlternative"); clicked.has_value()) {
            _tuneRequest = hertzOf(frequencies[*clicked]);
        }
    }

    void drawNetwork(const Area& area) {
        const auto& networks = _reception.station.otherNetworks;
        const bool  tunable  = tunerActive();
        cardHeader(area, countTitle("NETWORK", networks.size()), tunable ? "other stations of this network, click to tune" : "other stations of this network");
        if (networks.empty()) {
            note("no other network stations received yet");
            return;
        }
        FontScope fontScope = font(&LookAndFeel::fontSmall);
        if (!ImGui::BeginTable("##rdsNetwork", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit, ImVec2(area.width(), 0.f))) {
            return;
        }
        ImGui::TableSetupColumn("name");
        ImGui::TableSetupColumn("PI");
        ImGui::TableSetupColumn("frequencies", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("traffic");
        for (const auto& [programmeIdentification, network] : networks) {
            const bool        announcing  = network.trafficAnnouncement.value_or(false);
            const std::string networkName = network.programmeServiceName.empty() ? std::string(kNoName) : network.programmeServiceName;
            ImGui::TableNextRow();
            if (announcing) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, withAlpha(_colours.warning, kTint));
            }
            ImGui::TableNextColumn();
            if (tunable && !network.alternativeFrequencies.empty()) {
                DigitizerUi::IMW::ChangeId rowScope(programmeIdentification);
                if (ImGui::Selectable(networkName.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
                    _tuneRequest = hertzOf(network.alternativeFrequencies.front());
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("tune to %.1f MHz", static_cast<double>(network.alternativeFrequencies.front()));
                }
            } else {
                text(networkName, _colours.text);
            }
            ImGui::TableNextColumn();
            text(std::format("{:04X}", programmeIdentification), _colours.secondary);
            ImGui::TableNextColumn();
            text(frequencyList(network.alternativeFrequencies), _colours.secondary);
            ImGui::TableNextColumn();
            text("TP", network.trafficProgramme ? _colours.success : _colours.border);
            ImGui::SameLine();
            text("TA", announcing ? _colours.warning : _colours.border);
        }
        ImGui::EndTable();
    }

    void drawTraffic(const Area& area, double now) const {
        cardHeader(area, countTitle("TRAFFIC MESSAGES", _reception.trafficMessages.size()), "TMC");
        if (_reception.trafficMessages.empty()) {
            note("no traffic messages (TMC) received yet");
            return;
        }
        {
            FontScope fontScope = font(&LookAndFeel::fontSmall);
            if (ImGui::BeginTable("##rdsTraffic", static_cast<int>(kTrafficColumns.size()), ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp, ImVec2(area.width(), 0.f))) {
                for (const std::string_view column : kTrafficColumns) {
                    ImGui::TableSetupColumn(column.data());
                }
                ImGui::TableHeadersRow();
                for (const auto& [utcSeconds, message] : _reception.trafficMessages | std::views::reverse) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    text(durationText(now - utcSeconds), _colours.secondary);
                    for (const std::string& value : {std::format("{}", message.event), std::format("{}", message.location), std::format("{}", message.extent), std::string(message.direction ? "negative" : "positive"), message.duration ? std::format("{}", *message.duration) : std::string("-")}) {
                        ImGui::TableNextColumn();
                        text(value, _colours.text);
                    }
                }
                ImGui::EndTable();
            }
        }
        FontScope fontScope = font(&LookAndFeel::fontTiny);
        text("Raw TMC numbers. Translating them needs the licensed location tables.", _colours.secondary);
    }

    void drawReception(const Area& area, double now) const {
        const std::uint32_t groupTotal = std::accumulate(_reception.groupCounts.begin(), _reception.groupCounts.end(), 0U);
        cardHeader(area, "RECEPTION", std::format("{} groups", groupTotal));
        if (_reception.blockCounts.size() == 3UZ) {
            drawBlockQuality(area, now);
        }
        drawGroupHistogram(area, groupTotal);
    }

    void drawBlockQuality(const Area& area, double now) const {
        const auto&         counts = _reception.blockCounts;
        const std::uint32_t blocks = counts[0] + counts[1] + counts[2];
        {
            FontScope                                                 fontScope = font(&LookAndFeel::fontSmall);
            const std::array<std::pair<std::string_view, ImU32>, 3UZ> parts{{{"error-free", _colours.success}, {"corrected", _colours.warning}, {"lost", _colours.error}}};
            for (std::size_t i = 0UZ; i < parts.size(); ++i) {
                const std::string label = std::format("{} {}", parts[i].first, percentText(counts[i], blocks));
                if (i > 0UZ) {
                    flowNext(area.right, textSize(label).x);
                }
                text(label, parts[i].second);
            }
        }
        drawTrend(area, now);
    }

    void drawTrend(const Area& area, double now) const {
        const auto& quality   = _reception.quality;
        FontScope   fontScope = font(&LookAndFeel::fontTiny);
        if (quality.size() < 2UZ) {
            text("error-free trend follows", _colours.secondary);
            return;
        }
        const ImVec2        min = ImGui::GetCursorScreenPos();
        const ImVec2        max(area.right, min.y + kGraphLines * ImGui::GetTextLineHeight());
        const float         lowest = std::min(90.f, std::ranges::min(quality, {}, &QualitySample::errorFreePercent).errorFreePercent);
        std::vector<ImVec2> points;
        points.reserve(quality.size());
        for (std::size_t i = 0UZ; i < quality.size(); ++i) {
            const float x = min.x + (max.x - min.x) * static_cast<float>(i) / static_cast<float>(quality.size() - 1UZ);
            points.emplace_back(x, max.y - (quality[i].errorFreePercent - lowest) / (100.f - lowest) * (max.y - min.y));
        }
        ImGui::GetWindowDrawList()->AddPolyline(points.data(), static_cast<int>(points.size()), _colours.success, ImDrawFlags_None, kLineWidth);
        ImGui::Dummy(max - min);
        text(std::format("error-free blocks, last {}", durationText(now - quality.front().utcSeconds)), _colours.secondary);
    }

    void drawGroupHistogram(const Area& area, std::uint32_t groupTotal) const {
        std::string tooltip;
        {
            FontScope fontScope = font(&LookAndFeel::fontTiny);
            text("GROUP TYPES", _colours.secondary);
            std::vector<std::pair<std::size_t, std::uint32_t>> groups;
            for (std::size_t code = 0UZ; code < _reception.groupCounts.size(); ++code) {
                if (_reception.groupCounts[code] > 0U) {
                    groups.emplace_back(code, _reception.groupCounts[code]);
                }
            }
            if (groups.empty()) {
                text("no groups yet", _colours.secondary);
                return;
            }
            const std::uint32_t largest  = std::ranges::max(groups | std::views::values);
            const float         line     = ImGui::GetTextLineHeight();
            const ImVec2        top      = ImGui::GetCursorScreenPos();
            const float         bottom   = top.y + 2.f * kGraphLines * line;
            const float         slot     = area.width() / static_cast<float>(groups.size());
            const float         barWidth = std::min(slot - ImGui::GetStyle().ItemSpacing.x, 2.f * line);
            ImDrawList*         drawList = ImGui::GetWindowDrawList();
            for (std::size_t i = 0UZ; i < groups.size(); ++i) {
                const auto& [code, groupCount] = groups[i];
                const float       left         = area.left + static_cast<float>(i) * slot;
                const float       barLeft      = left + .5f * (slot - barWidth);
                const float       barHeight    = std::max(kLineWidth, (bottom - top.y) * static_cast<float>(groupCount) / static_cast<float>(largest));
                const std::string groupLabel   = groupName(code);
                const float       labelWidth   = textSize(groupLabel).x;
                drawList->AddRectFilled(ImVec2(barLeft, bottom - barHeight), ImVec2(barLeft + barWidth, bottom), _colours.accent);
                if (labelWidth <= slot) {
                    textAt(ImVec2(left + .5f * (slot - labelWidth), bottom), _colours.secondary, groupLabel);
                }
                if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(ImVec2(left, top.y), ImVec2(left + slot, bottom + line))) {
                    tooltip = std::format("{}{}{}{}{} groups", groupLabel, kSeparator, percentText(groupCount, groupTotal), kSeparator, groupCount);
                }
            }
            ImGui::Dummy(ImVec2(area.width(), bottom + line - top.y));
        }
        if (!tooltip.empty()) {
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
    }

    void drawBody(float statusHeight, double now) {
        const float width = ImGui::GetContentRegionAvail().x;
        const bool  wide  = width >= kSingleColumnWidth;
        card(width, [this, now](const Area& area) { drawStation(area, now); });
        card(width, [this, now](const Area& area) { drawNowPlaying(area, now); });
        DigitizerUi::IMW::Child sections("##rdsSections", ImVec2(0.f, std::max(0.f, ImGui::GetContentRegionAvail().y - statusHeight)), ImGuiChildFlags_None, ImGuiWindowFlags_None);
        drawSections(wide, now);
    }

    void drawSections(bool wide, double now) {
        const float width       = ImGui::GetContentRegionAvail().x;
        const auto  earlier     = [this, now](const Area& area) { drawEarlier(area, now); };
        const auto  frequencies = [this](const Area& area) { drawFrequencies(area); };
        const auto  network     = [this](const Area& area) { drawNetwork(area); };
        const auto  traffic     = [this, now](const Area& area) { drawTraffic(area, now); };
        const auto  reception   = [this, now](const Area& area) { drawReception(area, now); };
        if (!wide) {
            card(width, earlier);
            card(width, frequencies);
            card(width, network);
            card(width, traffic);
            card(width, reception);
            return;
        }
        const float columnWidth = .5f * (width - ImGui::GetStyle().ItemSpacing.x);
        ImGui::BeginGroup();
        card(columnWidth, earlier);
        card(columnWidth, traffic);
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        card(columnWidth, frequencies);
        card(columnWidth, network);
        card(columnWidth, reception);
        ImGui::EndGroup();
    }

    void drawWaiting(float height) const {
        const ImVec2      top      = ImGui::GetCursorScreenPos();
        const float       width    = ImGui::GetContentRegionAvail().x;
        constexpr auto    title    = std::string_view("Waiting for RDS station data");
        const std::string subtitle = _bitRate > 0.0 ? std::format("RDS bits arrive at {:.0f} bit/s", _bitRate) : std::string("no RDS bits yet");
        ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + .4f * height));
        {
            FontScope fontScope = font(&LookAndFeel::fontBig);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + .5f * (width - textSize(title).x));
            text(title, _colours.text);
        }
        {
            FontScope fontScope = font(&LookAndFeel::fontSmall);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + .5f * (width - textSize(subtitle).x));
            text(subtitle, _colours.secondary);
        }
        ImGui::SetCursorScreenPos(top);
        ImGui::Dummy(ImVec2(width, height));
    }

    void drawStatusLine(double now) const {
        FontScope   fontScope = font(&LookAndFeel::fontSmall);
        const float height    = ImGui::GetTextLineHeight();
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - height - ImGui::GetStyle().WindowPadding.y));
        const ImVec2 top       = ImGui::GetCursorScreenPos();
        const float  right     = top.x + ImGui::GetContentRegionAvail().x;
        const float  gap       = ImGui::GetStyle().ItemSpacing.x;
        const bool   receiving = _bitRate > 0.0;
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(top.x + kDotRadius, top.y + .5f * height), kDotRadius, receiving ? _colours.success : _colours.secondary);
        std::string status = receiving ? std::format("{:.0f} bit/s", _bitRate) : std::string("no RDS bits");
        if (_reception.blockCounts.size() == 3UZ) {
            const std::uint32_t blocks = _reception.blockCounts[0] + _reception.blockCounts[1] + _reception.blockCounts[2];
            status += std::format("{}{} error-free", kSeparator, percentText(_reception.blockCounts[0], blocks));
        }
        status += kSeparator;
        if (_reception.lastUpdateUtcSeconds == 0.0) {
            status += "no station data yet";
        } else if (_reception.synchronised) {
            status += std::format("data {}", ageText(now - _reception.lastUpdateUtcSeconds));
        } else {
            status += std::format("RDS sync lost{}data {}", kSeparator, ageText(now - _reception.lastUpdateUtcSeconds));
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x + 2.f * kDotRadius + gap, top.y));
        text(status, _colours.secondary);
        if (_reception.lastUpdateUtcSeconds == 0.0) {
            return;
        }
        const std::string_view stationName = trimmed(_reception.station.programmeServiceName);
        std::string            identity    = std::format("{}{}PI {:04X}", stationName.empty() ? kNoName : stationName, kSeparator, _reception.station.programmeIdentification);
        if (tunerActive()) {
            identity += std::format("{}{:.1f} MHz", kSeparator, _tunedFrequency / 1e6);
        }
        const float identityWidth = textSize(identity).x;
        if (ImGui::GetItemRectMax().x + gap + identityWidth <= right) {
            ImGui::SetCursorScreenPos(ImVec2(right - identityWidth, top.y));
            text(identity, _colours.secondary);
        }
    }

    [[nodiscard]] static double utcNow() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }
};

} // namespace opendigitizer::charts

inline auto registerRDSViewCompatibilities = opendigitizer::charts::registerChartSignalCompatibility<opendigitizer::charts::RDSView>();

#endif // OPENDIGITIZER_CHARTS_RDSVIEW_HPP
