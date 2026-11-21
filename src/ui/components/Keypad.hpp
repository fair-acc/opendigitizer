#ifndef OPENDIGITIZER_UI_COMPONENTS_KEYPAD_HPP_
#define OPENDIGITIZER_UI_COMPONENTS_KEYPAD_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gr::od::components {

enum class TType {
    None,

    Plus,
    Minus,
    Mul,
    Div,
    Power,

    UMinus,
    Sin,
    Cos,
    Tan,
    Sinh,
    Cosh,
    Tanh,

    Expr,
    POpen,
    PClose,
    Const,
    End
};

struct Token {
    TType            type = TType::None;
    std::string_view range; // binary operators include their surrounding spaces, e.g. " + "

    [[nodiscard]] constexpr bool isOperator() const noexcept { return type >= TType::Plus && type < TType::Expr; }
    [[nodiscard]] constexpr bool isValid() const noexcept { return type != TType::None && type != TType::End; }
    [[nodiscard]] constexpr bool isPOpen() const noexcept { return type == TType::POpen || (type >= TType::Sin && type <= TType::Tanh); }
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

} // namespace gr::od::components

#endif
