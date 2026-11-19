#include "components/Keypad.hpp"

#include <boost/ut.hpp>

#include <cstring>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using namespace boost::ut;
using namespace DigitizerUi::components;

struct HeapText { // exact-size heap copy, so that a read outside the input trips the sanitizers
    std::unique_ptr<char[]> buffer;
    std::size_t             size;

    explicit HeapText(std::string_view text) : buffer(std::make_unique<char[]>(text.size() + 1UZ)), size(text.size()) { std::memcpy(buffer.get(), text.data(), text.size()); }
    [[nodiscard]] std::string_view view() const { return {buffer.get(), size}; }
};

[[nodiscard]] std::optional<float> evaluated(std::string_view text) { return evaluate(HeapText(text).view()); }

const suite<"keypad expression parser"> _parser = [] {
    "keypad input evaluates as before"_test = [] {
        // results of the previous parser, bit-exact
        constexpr std::pair<std::string_view, float> kExpected[] = {
            {"5", 0x1.4p+2f},
            {"-5", -0x1.4p+2f},
            {"-55", -0x1.b8p+5f},
            {"1 + 2", 0x1.8p+1f},
            {"1 - 2", -0x1p+0f},
            {"2 * 3", 0x1.8p+2f},
            {"7 / 2", 0x1.cp+1f},
            {"2 ^ 3", 0x1p+3f},
            {"2 ^ 3 ^ 2", 0x1p+9f},
            {"1 + 2 * 3", 0x1.cp+2f},
            {"(1 + 2) * 3", 0x1.2p+3f},
            {"((1 + 2) * (3 - 4)) / 5", -0x1.333334p-1f},
            {"-(1 + 2)", -0x1.8p+1f},
            {"2 * -3", -0x1.8p+2f},
            {"1 - -2", 0x1.8p+1f},
            {"-2 ^ 2", 0x1p+2f},
            {"2 ^ -1", 0x1p-1f},
            {"sin(0.5)", 0x1.eaee88p-2f},
            {"cos(0.5)", 0x1.c1528p-1f},
            {"tan(0.5)", 0x1.17b4f6p-1f},
            {"sinh(0.5)", 0x1.0acdp-1f},
            {"cosh(0.5)", 0x1.20ac18p+0f},
            {"tanh(0.5)", 0x1.d9353ep-2f},
            {"-sin(0.5)", -0x1.eaee88p-2f},
            {"sin(cos(0.5))", 0x1.89d41ap-1f},
            {"2 * sin(1 + 2)", 0x1.210386p-2f},
            {"1e3", 0x1.f4p+9f},
            {"1.5e-3 * 2", 0x1.89374cp-9f},
            {"2.5e+2 + 1", 0x1.f6p+7f},
            {"1e", 0x1p+0f},
            {"10 / 4 - 3 * 2 + 1", -0x1.4p+1f},
            {"8 - 3 - 2", 0x1.8p+1f},
            {"64 / 4 / 2", 0x1p+3f},
            {"0.1 + 0.2", 0x1.333334p-2f},
            {"-(2 * 3) + 1", -0x1.4p+2f},
            {"2 ^ (1 + 1)", 0x1p+2f},
            {"-0.5 * -4", 0x1p+1f},
        };
        for (const auto& [text, value] : kExpected) {
            const auto result = evaluated(text);
            expect(result.has_value() && *result == value) << text;
        }
    };

    "leading and trailing operators, garbage and empty input are rejected"_test = [] {
        for (std::string_view text : {"", " ", "+", "-", "*", "1 +", "1 -", "1 - ", "+ 2", "* 3", "^ 2", ")", "(", "()", "(1 + 2", "1 + 2)", "abc", "1 + + 2", "asin(0.5)", "--2", "s", "sin", "sin(", "sinx(1)", ".", "1 + .", "1e99", "1 2", "5E3", "1 & 2"}) {
            expect(!evaluated(text).has_value()) << text;
        }
    };

    "unary minus binds tighter than any binary operator"_test = [] {
        expect(evaluated("-2 ^ 2") == std::optional(4.f));
        expect(evaluated("2 ^ -2") == std::optional(.25f));
        expect(evaluated("-(-(2))") == std::optional(2.f));
        expect(evaluated("-cos(0)") == std::optional(-1.f));
    };

    "nested parentheses and functions"_test = [] {
        expect(evaluated("((((1))))") == std::optional(1.f));
        expect(evaluated("(2 * (3 + (4 - 1))) / 3") == std::optional(4.f));
        expect(evaluated("tanh(sinh(0) + cosh(0) - 1)") == std::optional(0.f));
        expect(evaluated("tan(sin(0)) * 2") == std::optional(0.f));
    };

    "exponent notation"_test = [] {
        expect(evaluated("1e3") == std::optional(1000.f));
        expect(evaluated("2.5e-1 * 4") == std::optional(1.f));
        expect(evaluated("1e+2 / 1e2") == std::optional(1.f));
        expect(evaluated("1e") == std::optional(1.f)) << "a dangling exponent is ignored, as by strtof";
    };

    "tokens"_test = [] {
        expect(lastToken("").type == TType::tt_none);
        expect(lastToken("1 + ").type == TType::tt_plus);
        expect(eq(lastToken("1 + ").range, std::string_view(" + "))) << "binary operators span their spaces, which Backspace removes";
        expect(lastToken("1 - ").type == TType::tt_minus);
        expect(lastToken("1 -").type == TType::tt_minus);
        expect(lastToken("2 * -").type == TType::tt_minus);
        expect(lastToken("-").type == TType::tt_minus);
        expect(lastToken("-5").type == TType::tt_const);
        expect(lastToken("sinh(").type == TType::tt_sinh);
        expect(eq(lastToken("2 * sin(").range, std::string_view("sin(")));
        expect(lastToken("(1)").type == TType::tt_pclose);
        expect(lastToken("+").type == TType::tt_plus) << "a leading operator stays inside the input";
        expect(eq(lastToken("+").range, std::string_view("+")));

        const HeapText text("  12.5e-1 ^ 2");
        const auto     tokens = tokenize(text.view());
        expect(eq(tokens.size(), 4UZ));
        expect(eq(tokens[0].range, std::string_view("12.5e-1"))) << "leading spaces are skipped once";
        expect(tokens[1].type == TType::tt_power);
        expect(tokens.back().type == TType::tt_end);
    };

    "only a single, optionally negated, constant is accepted as is"_test = [] {
        expect(onlyToken("5"));
        expect(onlyToken("-5"));
        expect(onlyToken("-55")) << "was false before, so Enter re-evaluated it forever";
        expect(!onlyToken(""));
        expect(!onlyToken("(5)"));
        expect(!onlyToken("1 + 2"));
        expect(!onlyToken("--5"));
    };

    "numbers"_test = [] {
        expect(parseNumber("2.5") == std::optional(2.5f));
        expect(parseNumber("1-2") == std::optional(1.f)) << "trailing characters are ignored, as by strtof";
        expect(!parseNumber(".").has_value());
        expect(!parseNumber("").has_value());
        expect(!parseNumber("1e99").has_value());
    };

    "increment and precision of the last digit"_test = [] {
        expect(eq(incrementOf("", TrailingZeros::Significant), 1.0));
        expect(eq(incrementOf("0", TrailingZeros::Significant), 1.0));
        expect(eq(incrementOf("250", TrailingZeros::Significant), 100.0));
        expect(eq(incrementOf("2.", TrailingZeros::Significant), 1.0));
        expect(eq(incrementOf("2.500", TrailingZeros::Significant), 0.001));
        expect(eq(incrementOf("2.500", TrailingZeros::Ignored), 0.1));
        expect(eq(incrementOf("2.000", TrailingZeros::Ignored), 1.0));
        expect(eq(decimalPrecision("12", TrailingZeros::Significant), 0));
        expect(eq(decimalPrecision("2.500", TrailingZeros::Significant), 3));
        expect(eq(decimalPrecision("2.500", TrailingZeros::Ignored), 1));
        expect(eq(decimalPrecision("2.000", TrailingZeros::Ignored), 0));
    };
};

} // namespace

int main() { return boost::ut::cfg<boost::ut::override>.run(); }
