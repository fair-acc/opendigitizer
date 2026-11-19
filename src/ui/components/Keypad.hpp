#ifndef OPENDIGITIZER_UI_COMPONENTS_KEYPAD_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_KEYPAD_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace DigitizerUi::components {

enum class TType {
    tt_none,

    tt_plus,
    tt_minus,
    tt_mul,
    tt_div,
    tt_power,

    tt_uminus,
    tt_sin,
    tt_cos,
    tt_tan,
    tt_sinh,
    tt_cosh,
    tt_tanh,

    tt_expr,
    tt_popen,
    tt_pclose,
    tt_const,
    tt_end
};

struct Token {
    TType            type = TType::tt_none;
    std::string_view range; // binary operators include their surrounding spaces, e.g. " + "

    [[nodiscard]] constexpr bool isOperator() const noexcept { return type >= TType::tt_plus && type < TType::tt_expr; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return type != TType::tt_none && type != TType::tt_end; }
    [[nodiscard]] constexpr bool isPOpen() const noexcept { return type == TType::tt_popen || (type >= TType::tt_sin && type <= TType::tt_tanh); }
};

[[nodiscard]] std::vector<Token>   tokenize(std::string_view text);
[[nodiscard]] Token                lastToken(std::string_view text);
[[nodiscard]] bool                 onlyToken(std::string_view text);
[[nodiscard]] std::optional<float> parseNumber(std::string_view number);
[[nodiscard]] std::optional<float> evaluate(std::string_view text);

enum class TrailingZeros { Significant, Ignored };
[[nodiscard]] double incrementOf(std::string_view value, TrailingZeros trailingZeros);
[[nodiscard]] int    decimalPrecision(std::string_view value, TrailingZeros trailingZeros);

struct InputKeypad {
    [[nodiscard]] static bool edit(const std::string& key, const char* label, std::int32_t* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, std::uint32_t* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, std::int64_t* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, std::uint64_t* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, float* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, double* value, std::string_view suffix = {});
    [[nodiscard]] static bool edit(const std::string& key, const char* label, std::string* value, std::string_view suffix = {});

    static void               clearIfNewBlock(std::string_view block);
    [[nodiscard]] static bool isVisible() noexcept;
};

} // namespace DigitizerUi::components

#endif
