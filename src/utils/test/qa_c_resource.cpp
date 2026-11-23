#include <boost/ut.hpp>

#include "../include/stdex/c_resource.hpp"

#include <atomic>
#include <format>
#include <print>

struct S {
    int counter = 0;

    void operator++(int) noexcept { ++counter; }
    void operator--(int) noexcept { --counter; }

    auto operator<=>(const S&) const = default;
};
extern "C" {
inline static S cApiRessource; // NOSONAR
S*              conS1() {
    cApiRessource++;
    return &cApiRessource;
};
void desS1(S* ressource) {
    ressource->counter--;
    ressource = nullptr;
}
void conS2(S** ressource) {
    if (ressource) {
        *ressource = &cApiRessource;
        (*ressource)->counter++;
    }
}
void desS2(S** ressource) {
    (*ressource)->counter--;
    *ressource = nullptr;
}
}

const static boost::ut::suite c_wrapper_API = [] {
    using W1 = stdex::c_resource<S*, conS1, desS1>;
    using W2 = stdex::c_resource<S*, conS2, desS2>;

    static_assert(std::is_nothrow_default_constructible_v<W1>);
    static_assert(!std::is_copy_constructible_v<W1>);
    static_assert(std::is_nothrow_move_constructible_v<W1>);
    static_assert(!std::is_copy_assignable_v<W1>);
    static_assert(std::is_nothrow_move_assignable_v<W1>);
    static_assert(std::is_nothrow_destructible_v<W1>);
    static_assert(std::is_nothrow_swappable_v<W1>);

    static_assert(std::is_nothrow_default_constructible_v<W2>);
    static_assert(!std::is_copy_constructible_v<W2>);
    static_assert(std::is_nothrow_move_constructible_v<W2>);
    static_assert(!std::is_copy_assignable_v<W2>);
    static_assert(std::is_nothrow_move_assignable_v<W2>);
    static_assert(std::is_nothrow_destructible_v<W2>);
    static_assert(std::is_nothrow_swappable_v<W2>);
};

const static boost::ut::suite c_wrapper_functional_c_api_schema12 = [] {
    using namespace boost::ut;

    cApiRessource.counter = 0;
    [] { // API schema 1: pointer-type return value
        using WrappedType = stdex::c_resource<S*, conS1, desS1>;
        expect(eq(0, cApiRessource.counter));
        WrappedType wrapped;
        expect(eq(1, cApiRessource.counter));
        expect(neq(nullptr, wrapped.get()));
        expect(eq(&cApiRessource, wrapped.get()));
    }();
    expect(eq(0, cApiRessource.counter));

    [] { // API schema 2: reference as input-output parameter
        using WrappedType = stdex::c_resource<S*, conS2, desS2>;
        expect(eq(0, cApiRessource.counter));
        WrappedType wrapped;
        expect(eq(1, cApiRessource.counter));
        expect(neq(nullptr, wrapped.get()));
        expect(eq(&cApiRessource, wrapped.get()));
    }();
    expect(eq(0, cApiRessource.counter));
};

const static boost::ut::suite c_wrapper_functional_lambda = [] {
    using namespace boost::ut;
    constexpr auto kConstruct = +[] {
        cApiRessource++;
        return &cApiRessource;
    };
    constexpr auto kDestruct = +[](S const*) { cApiRessource--; };

    [] { // API schema 1: pointer-type return value using lambda
        using WrappedType     = stdex::c_resource<S*, kConstruct, kDestruct>;
        cApiRessource.counter = 0;
        expect(eq(0, cApiRessource.counter));
        WrappedType wrapped;
        expect(eq(1, cApiRessource.counter));
        expect(neq(nullptr, wrapped.get()));
    }();
    expect(eq(0, cApiRessource.counter));
};

const static boost::ut::suite c_wrapper_functional_bool_return = [] {
    using namespace boost::ut;
    static int counter = 0;

    [] { // API schema 3a: boolean return value
        constexpr auto kConstruct = +[] {
            counter++;
            return true;
        };
        constexpr auto kDestruct = +[] { counter--; };
        using WrappedType        = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        WrappedType wrapped;
        expect(eq(1, counter));
    }();
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value
        constexpr auto kConstruct = +[] {
            counter++;
            return false;
        };
        constexpr auto kDestruct = +[] {
            counter--;
            std::println(stderr, "should not be invoked");
        };
        using WrappedType = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        WrappedType wrapped;
        expect(eq(1, counter));
    }();
    expect(eq(1, counter)); // destructor should not be invoked
    counter = 0;

    [] { // API schema 3a: boolean return value
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto kConstruct   = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto kDestruct = +[] { counter--; };
        using WrappedType        = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        WrappedType wrapped(42, 3.141, true);
        expect(eq(1, counter));
        expect(eq(42, argument1));
        expect(eq(3.141, argument2));
        expect(eq(true, optArgument3));
    }();
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value - ImGui use-case
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto kConstruct   = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto kDestruct = +[] { counter--; };
        using WrappedType        = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        bool invoked = false;
        if (auto _ = WrappedType(42, 3.141, true)) { // not assigning to '_' will correctly fail to compile
            expect(eq(1, counter));
            expect(eq(42, argument1));
            expect(eq(3.141, argument2));
            expect(eq(true, optArgument3));
            invoked = true;
        }
        expect(invoked);
        expect(eq(0, counter));
    }();
    expect(eq(0, counter));

#if defined(__GNUC__) && !defined(__clang__) && !defined(__EMSCRIPTEN__)
    // compiles only with GCC: unclear whether GCC deduces too leniently or Clang too strictly
    [] { // API schema 3a: boolean return value
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto kConstruct   = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto kDestruct = +[] { counter--; };
        using WrappedType        = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        WrappedType wrapped(42, 3.141); // last argument defaulted
        expect(eq(1, counter));
        expect(eq(42, argument1));
        expect(eq(3.141, argument2));
        expect(eq(true, optArgument3));
    }();
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value - ImGui use-case
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto kConstruct   = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto kDestruct = +[] { counter--; };
        using WrappedType        = stdex::c_resource<bool, kConstruct, kDestruct>;
        expect(eq(0, counter));
        bool invoked = false;
        if (auto _ = WrappedType(42, 3.141)) { // last argument defaulted
            expect(eq(1, counter));
            expect(eq(42, argument1));
            expect(eq(3.141, argument2));
            expect(eq(true, optArgument3));
            invoked = true;
        }
        expect(invoked);
        expect(eq(0, counter));
    }();
    expect(eq(0, counter));
#endif
};

int main() { /* not needed for ut */ }
