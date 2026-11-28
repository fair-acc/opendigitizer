#ifndef OPENDIGITIZER_UI_COMMON_PROCESSORGLYPHS_HPP
#define OPENDIGITIZER_UI_COMMON_PROCESSORGLYPHS_HPP

/// OpenDigitizer's own glyphs, in LookAndFeel::fontGlyphs (assets/odglyphs/od-glyphs.ttf, made by generate_od_glyphs.py):
/// UTF-8 strings of their Private Use Area codepoints, drawn like any text in that font
namespace gr::od::glyphs {

inline constexpr const char* kCpu  = "";
inline constexpr const char* kGpu  = "";
inline constexpr const char* kFpga = "";
inline constexpr const char* kTpu  = "";

// the same inside a chip package
inline constexpr const char* kCpuOutline  = "";
inline constexpr const char* kGpuOutline  = "";
inline constexpr const char* kFpgaOutline = "";
inline constexpr const char* kTpuOutline  = "";

inline constexpr char32_t kFirstCodepoint = 0xE000;
inline constexpr char32_t kLastCodepoint  = 0xE007;

} // namespace gr::od::glyphs

#endif // OPENDIGITIZER_UI_COMMON_PROCESSORGLYPHS_HPP
