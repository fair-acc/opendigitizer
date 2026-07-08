#ifndef OPENDIGITIZER_UI_COMPONENTS_FILTER_TAG_BAR_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_FILTER_TAG_BAR_HPP_

#include <cstddef>
#include <optional>
#include <utility>

namespace DigitizerUi::components {

/// Draw a series of boxes with (x) buttons, for a filtering + searching UI.
class FilterTagBar {
public:
    explicit FilterTagBar(const char* strId);
    ~FilterTagBar();

    // call beginTag(), then draw the tag's contents (probably a text label), then call endTag()
    void beginTag(std::size_t tagIndex);
    void endTag();

    /// returns the index of the tag which the user pressed (X) on and wants to delete
    [[nodiscard]] std::optional<std::size_t> finish();

private:
    std::optional<std::size_t> _removed;
    std::size_t                _currentTag = 0UZ;
    std::size_t                _tagsDrawn  = 0UZ;
    bool                       _insideTag  = false;
    bool                       _finished   = false;
};

} // namespace DigitizerUi::components

#endif
