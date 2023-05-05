#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef EMSCRIPTEN
#include "emscripten_compat.h"
#endif
#include <plf_colony.h>

namespace DigitizerUi {

class Block;
class FlowGraph;
struct DashboardDescription;

struct DashboardSource {
    ~DashboardSource() noexcept;

    std::string                             path;
    bool                                    enabled;
    const bool                              isValid = true;

    static std::shared_ptr<DashboardSource> get(std::string_view path);
};

struct DashboardDescription {
    static constexpr const char                                      *fileExtension = ".ddd"; // ddd for "Digitizer Dashboard Description"

    std::string                                                       name;
    std::shared_ptr<DashboardSource>                                  source;
    std::string                                                       filename;
    bool                                                              isFavorite;
    std::optional<std::chrono::time_point<std::chrono::system_clock>> lastUsed;

    void                                                              save();

    static void                                                       load(const std::shared_ptr<DashboardSource> &source, const std::string &filename,
                                                                  const std::function<void(std::shared_ptr<DashboardDescription> &&)> &cb);
    static std::shared_ptr<DashboardDescription>                      createEmpty(const std::string &name);
};

class Dashboard {
public:
    struct Source {
        Block      *block;
        int         port;
        std::string name;
        uint32_t    color;

        inline bool operator==(const Source &s) const { return s.block == block && s.port == port; };
    };
    struct Plot {
        enum class Axis {
            X,
            Y
        };

        Plot();

        std::string           name;
        std::vector<Source *> sources;
        struct AxisData {
            Axis   axis;
            double min;
            double max;
        };
        std::vector<AxisData> axes;

        struct GridRect {
            int x = 0, y = 0, w = 5, h = 5;
        };
        GridRect rect;
    };

    explicit Dashboard(const std::shared_ptr<DashboardDescription> &desc);
    ~Dashboard();

    void                         save();

    void                         newPlot(int x, int y, int w, int h);
    void                         deletePlot(Plot *plot);

    inline const auto           &sources() const { return m_sources; }
    inline auto                 &sources() { return m_sources; }

    inline auto                 &plots() { return m_plots; }

    void                         setNewDescription(const std::shared_ptr<DashboardDescription> &desc);
    inline DashboardDescription *description() const { return m_desc.get(); }

private:
    void                                  doLoad(const std::string &desc);

    std::shared_ptr<DashboardDescription> m_desc;
    std::vector<Plot>                     m_plots;
    plf::colony<Source>                   m_sources;
};

const std::string EXAMPLE_HEADER = R"""(favorite: false
lastUsed: 07/04/2023)""";
const std::string EXAMPLE_DASHBOARD = R"""(sources:
  - name: sine source 1.out
    block: sine source 1
    port: 0
    color: 4292240583
  - name: source for sink 1.out
    block: source for sink 1
    port: 0
    color: 4283810630
  - name: source for sink 2.out
    block: source for sink 2
    port: 0
    color: 4284318450
plots:
  - name: Plot 1
    axes:
    - axis: X
      min: -12.661030686446349
      max: 580.68847757027561
    - axis: Y
      min: -1.1052913129824811
      max: 1.2526563780175193
    sources:
      - sine source 1.out
    rect:
      - 0
      - 0
      - 8
      - 6
  - name: Plot 2
    axes:
      - axis: X
        min: -5.0791591167480172
        max: 440.71265782820461
      - axis: Y
        min: -1.7225485895235
        max: 1.7593722791967299
    sources:
      - source for sink 2.out
      - source for sink 1.out
    rect:
      - 0
      - 6
      - 12
      - 9
flowgraphLayout: "{\"nodes\":{\"node:93878263824832\":{\"location\":{\"x\":22,\"y\":-35},\"name\":\"FFT\"},\"node:93878264299904\":{\"location\":{\"x\":314,\"y\":0},\"name\":\"sink 1\"},\"node:93878264332256\":{\"location\":{\"x\":314,\"y\":91},\"name\":\"sink 2\"},\"node:93878265192784\":{\"location\":{\"x\":-362,\"y\":0},\"name\":\"sine source 1\"},\"node:93878265200720\":{\"location\":{\"x\":-9,\"y\":201},\"name\":\"sum sigs\"},\"node:93878265206112\":{\"location\":{\"x\":-362,\"y\":91},\"name\":\"source for sink 1\"},\"node:93878265207280\":{\"location\":{\"x\":-362,\"y\":182},\"name\":\"source for sink 2\"}},\"selection\":null,\"view\":{\"scroll\":{\"x\":-735.00006103515625,\"y\":-117.999969482421875},\"visible_rect\":{\"max\":{\"x\":415.499969482421875,\"y\":313.5},\"min\":{\"x\":-367.500030517578125,\"y\":-58.9999847412109375}},\"zoom\":2}}" "";)""";
const std::string EXAMPLE_FLOWGRAPH = R"""(blocks:
- name: FFT
  id: FFT
- name: sum sigs
  id: sum sigs
- name: sine source 1
  id: sine_source
  parameters:
    frequency: 0.100000
- name: source for sink 1
  id: sink_source
- name: source for sink 2
  id: sink_source
- name: sink 1
  id: sink
- name: sink 2
  id: sink
connections:
  - [sine source 1, 0, FFT, 0]
  - [FFT, 0, sink 1, 0]
  - [sine source 1, 0, sum sigs, 0]
  - [source for sink 1, 0, sum sigs, 1]
  - [sum sigs, 0, sink 2, 0])""";

} // namespace DigitizerUi

#endif
