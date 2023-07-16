#ifndef OPENDIGITIZER_CALCULATOR_H
#define OPENDIGITIZER_CALCULATOR_H

#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

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
constexpr auto operator+(TType ty) {
    return size_t(ty);
}

struct Token {
    TType                        type = TType::tt_none;
    std::string_view             range;

    [[nodiscard]] constexpr bool is_operator() const noexcept {
        return +type >= +TType::tt_plus && +type < +TType::tt_expr;
    }
    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return type != TType::tt_none && type != TType::tt_end;
    }
    [[nodiscard]] constexpr bool is_popen() const noexcept {
        return type == TType::tt_popen || +type >= +TType::tt_sin && +type <= +TType::tt_tanh;
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
            return {
                TType::tt_uminus, { begin, begin + 1 }
            };
        case '*': return {
            TType::tt_mul, { begin - 1, begin + 2 }
        };
        case '/': return {
            TType::tt_div, { begin - 1, begin + 2 }
        };
        case '^': return {
            TType::tt_power, { begin - 1, begin + 2 }
        };
        case '(': return {
            TType::tt_popen, { begin, begin + 1 }
        };
        case ')': return {
            TType::tt_pclose, { begin, begin + 1 }
        };
        case 's':
            if (stream.starts_with("sinh("))
                return {
                    TType::tt_sinh, { begin, begin + 5 }
                };
            return {
                TType::tt_sin, { begin, begin + 4 }
            };
        case 'c':
            if (stream.starts_with("cosh("))
                return {
                    TType::tt_cosh, { begin, begin + 5 }
                };
            return {
                TType::tt_cos, { begin, begin + 4 }
            };
        case 't':
            if (stream.starts_with("tanh("))
                return {
                    TType::tt_tanh, { begin, begin + 5 }
                };
            return {
                TType::tt_tan, { begin, begin + 4 }
            };
        }

        if (isdigit(*begin) || *begin == '.') {
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

inline bool only_token(std::string_view stream) {
    Token t = get_token(stream);
    stream.remove_prefix(t.range.size());
    if (t.type == TType::tt_uminus) {
        get_token(stream);
        stream.remove_prefix(t.range.size());
    }

    auto t1 = get_token(stream);
    return t1.type == TType::tt_end;
}

inline std::vector<Token> tokenize(std::string_view stream) {
    std::vector<Token> tokens;
    while (true) {
        auto t = get_token(stream);
        tokens.push_back(t);
        if (t.type == TType::tt_end)
            return tokens;
        stream.remove_prefix(t.range.size());
    }
}

struct PTable {
    enum Action : uint8_t {
        S, // shift           (<)
        R, // reduce          (>)
        E, // equal           (=)
        X, // nothing - error ( )
        A, // accept          (A)
    };

    // columns(incoming) {^}{-a}{*/}{+-}{f/(}{)}{id}{$}
    constexpr static Action precedence_table[][8] = {
        /* {^}    */ { S, S, R, R, S, R, S, R },
        /* {-a}   */ { R, X, R, R, S, R, S, R },
        /* {* }   */ { S, S, R, R, S, R, S, R },
        /* {+-}   */ { S, S, S, R, S, R, S, R },
        /* {f/(}  */ { S, S, S, S, S, E, S, X },
        /* {)}    */ { R, X, R, R, X, R, X, R },
        /* {$}    */ { S, S, S, S, S, X, S, A },
    };

public:
    constexpr static Action GetAction(TType stack, TType incoming) {
        auto table_nav = [](TType entry) {switch (entry) {
            default:
                return entry == TType::tt_popen || +entry >= +TType::tt_sin && +entry <= +TType::tt_tanh?4:-1;
        case TType::tt_power: return 0;
        case TType::tt_uminus: return 1;

        case TType::tt_mul:
        case TType::tt_div: return 2;

        case TType::tt_plus:
        case TType::tt_minus: return 3;

        case TType::tt_pclose: return 5;

        case TType::tt_end: return 6;
        } };

        int  row       = table_nav(stack);
        if (row == -1) return X;

        int col = -1;
        switch (incoming) {
        case TType::tt_const:
            col = 6;
            break;
        case TType::tt_end:
            col = 7;
            break;
        default:
            col = table_nav(incoming);
            break;
        }

        if (col == -1) return X;
        return precedence_table[row][col];
    }
};

inline std::optional<float> evaluate(std::string_view stream) {
    std::vector<ASTNode> context;
    context.reserve(64);
    context.emplace_back(); // Add end

    auto last_term = [&]() {
        auto it = context.rbegin();
        while (it->type == TType::tt_expr)
            it++;
        return it;
    };

    auto reduce = [&](auto l_term) {
        using enum TType;
        if (l_term->type == TType::tt_uminus) {
            float a = -context.back().value;
            context.pop_back();
            context.back() = { TType::tt_expr, a };
            return;
        }
        if (l_term->type == tt_pclose) {
            // remove braces
            context.pop_back();
            auto  prev       = last_term();
            if (prev->type == tt_popen) {
                context.erase(context.end() - 2);
                prev = last_term();
                if(prev->type != tt_uminus)
                    return;

                context.back().value = -context.back().value;
                context.erase(context.end() - 2);
                return;
            }

            auto &last_value = context.back().value;

            switch (prev->type) {
            case TType::tt_sin:
                last_value = sinf(last_value);
                break;
            case TType::tt_cos:
                last_value = cosf(last_value);
                break;
            case TType::tt_tan:
                last_value = tanf(last_value);
                break;
            case TType::tt_sinh:
                last_value = sinhf(last_value);
                break;
            case TType::tt_cosh:
                last_value = coshf(last_value);
                break;
            case TType::tt_tanh:
                last_value = tanhf(last_value);
                break;
            default:
                return;
            }
            context.erase(context.end() - 2);
            return;
        }

        float a = context.rbegin()->value;
        float b = (context.rbegin() + 2)->value;
        context.pop_back();
        context.pop_back();

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
        case TType::tt_power:
            context.back().value = powf(b,a);
            break;
        default:
            assert(false);
        }
    };

    auto in_tk = get_token(stream);
    stream.remove_prefix(in_tk.range.size());

    while (true) {
        auto l_term = last_term();
        auto action = PTable::GetAction(l_term->type, in_tk.type);

        switch (action) {
        case PTable::E:
        case PTable::S: // Shift
            if (in_tk.type == TType::tt_const)
                context.emplace_back(ASTNode{TType::tt_expr, stof(std::string{ in_tk.range })});
            else
                context.emplace_back(ASTNode{in_tk.type});

            in_tk = get_token(stream);
            stream.remove_prefix(in_tk.range.size());
            continue;
        case PTable::R: reduce(l_term); break;
        case PTable::X: return std::nullopt;
        case PTable::A: return context.back().value;
        }
    }
}

#endif // OPENDIGITIZER_CALCULATOR_H
