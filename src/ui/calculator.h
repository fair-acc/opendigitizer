#ifndef OPENDIGITIZER_CALCULATOR_H
#define OPENDIGITIZER_CALCULATOR_H

#include <array>
#include <cctype>
#include <charconv>
#include <string_view>
#include <vector>

enum class TType {
    tt_none,
    tt_plus,
    tt_minus,
    tt_mul,
    tt_div,
    tt_expr,
    tt_const,
    tt_end
};

constexpr inline std::array typestrings = {
    "ERROR",
    "+",
    "-",
    "*",
    "/",
    "Expression",
    "Const",
    "End"
};
constexpr auto operator+(TType ty) {
    return size_t(ty);
}
constexpr std::string_view to_string(TType ty) {
    return typestrings[+ty];
}

struct Token {
    TType                        type = TType::tt_none;
    std::string_view             range;

    [[nodiscard]] constexpr bool is_operator() const noexcept {
        return +type >= +TType::tt_plus && +type <= +TType::tt_div;
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return type != TType::tt_none && type != TType::tt_end;
    }
};

// non-owning
struct ASTNode {
    TType type  = TType::tt_end;
    float value = 0.0f;
};

constexpr inline std::string_view parse_float(std::string_view stream) {
    auto begin = stream.begin();
    while (begin != stream.end()) {
        if (!isdigit(*begin) && *begin != '.' && *begin != '-' && *begin != '+' && *begin != 'e')
            break;
        begin++;
    }
    return { stream.begin(), begin };
}

constexpr inline Token get_token(std::string_view stream) {
    Token token;
    auto  begin = stream.begin();
    while (begin != stream.end()) {
        switch (*begin) {
        case '+': return {
            TType::tt_plus, { begin - 1, begin + 2 }
        };
        case '-':
            if (auto it = begin + 1; *it == ' ')
                return {
                    TType::tt_minus, { begin - 1, begin + 2 }
                };
            break;
        case '*': return {
            TType::tt_mul, { begin - 1, begin + 2 }
        };
        case '/': return {
            TType::tt_div, { begin - 1, begin + 2 }
        };
        }

        if (isdigit(*begin) || *begin == '.' || *begin == '-') {
            return { TType::tt_const, parse_float({ begin, stream.end() }) };
        }
        begin++;
    }
    return { TType::tt_end, "" };
}

inline Token last_token(std::string_view stream) {
    Token t;
    while (true) {
        auto t1 = get_token(stream);
        if (t1.type == TType::tt_end)
            return t;
        t = t1;
        stream.remove_prefix(t.range.size());
    }
}

bool only_token(std::string_view stream) {
    Token t = get_token(stream);
    stream.remove_prefix(t.range.size());
    auto t1 = get_token(stream);
    return t1.type == TType::tt_end;
}

inline std::vector<Token> _Debug_tokenize(std::string_view stream) {
    std::vector<Token> tokens;
    while (true) {
        auto t = get_token(stream);
        tokens.push_back(t);
        if (t.type == TType::tt_end)
            return tokens;
        stream.remove_prefix(t.range.size());
    }
}

constexpr int op_priority(TType op) {
    switch (op) {
    case TType::tt_plus:
    case TType::tt_minus: return 1;
    case TType::tt_div:
    case TType::tt_mul: return 2;
    case TType::tt_const: return 3;
    default: return 0;
    }
}

float evaluate(std::string_view stream) {
    std::vector<ASTNode> context;
    context.reserve(64);
    context.emplace_back(); // Add end

    auto last_term = [&]() {
        auto it = context.rbegin();
        while (it->type == TType::tt_expr)
            it++;
        return it;
    };

    auto in_tk = get_token(stream);
    stream.remove_prefix(in_tk.range.size());

    while (true) {
        auto l_term = last_term();
        if (op_priority(in_tk.type) > op_priority(l_term->type)) {
            if (in_tk.type == TType::tt_const)
                context.emplace_back(TType::tt_expr, stof(std::string{ in_tk.range }));
            else
                context.emplace_back(in_tk.type);

            in_tk = get_token(stream);
            stream.remove_prefix(in_tk.range.size());
            continue;
        }

        if (in_tk.type == TType::tt_end && l_term->type == TType::tt_end)
            return context.back().value;

        float a = context.rbegin()->value;
        float b = (context.rbegin() + 2)->value;
        context.pop_back();
        context.pop_back();
        // reduce
        switch (l_term->type) {
        case TType::tt_plus:
            context.back().value = a + b;
            break;
        case TType::tt_minus:
            context.back().value = b - a;
            break;
        case TType::tt_mul:
            context.back().value = b * a;
            break;
        case TType::tt_div:
            context.back().value = b / a;
            break;
        default:
            assert(false);
        }
    }
}

#endif // OPENDIGITIZER_CALCULATOR_H
