#ifndef OPENDIGITIZER_UI_STATUSBARVIEW_HPP
#define OPENDIGITIZER_UI_STATUSBARVIEW_HPP

#include <memory>
#include <vector>

#include <gnuradio-4.0/BlockModel.hpp>

#include "GraphSession.hpp"
#include "PaneBlocks.hpp"

namespace DigitizerUi {

class StatusBarView {
public:
    void draw(GraphSession* session); // nullptr: log only

    [[nodiscard]] static float height() noexcept;

private:
    PaneBlocks _blocks{gr::UICategory::StatusBar};

    void drawLogLine();
    void drawLogPopup();
};

} // namespace DigitizerUi

#endif
