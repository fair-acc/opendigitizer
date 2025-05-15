#include <boost/ut.hpp>

#include "../include/c_resource.hpp"

#include <atomic>
#include <format>
#include <print>

struct S {
    int counter = 0; // instance counter

    void operator++(int) noexcept { ++counter; }
    void operator--(int) noexcept { --counter; }

    auto operator<=>(const S&) const = default;
};
extern "C" {
inline static S cApiRessource; // NOSONAR acts as a global resource that needs to be managed
// test functions
S* conS1() {
    cApiRessource++;
    return &cApiRessource;
}; // API schema 1: pointer-type return value
void desS1(S* ressource) {
    ressource->counter--;
    ressource = nullptr;
} // API schema 1: value as input parameter
void conS2(S** ressource) {
    if (ressource) {
        *ressource = &cApiRessource; // Point to the global instance
        (*ressource)->counter++;
    }
} // API schema 2: reference as input-output parameter
void desS2(S** ressource) {
    (*ressource)->counter--;
    *ressource = nullptr;
} // API schema 2: reference as input-output parameter
}

const static boost::ut::suite c_wrapper_API = [] {
    using w1 = stdex::c_resource<S*, conS1, desS1>;
    using w2 = stdex::c_resource<S*, conS2, desS2>;

    static_assert(std::is_nothrow_default_constructible_v<w1>);
    static_assert(!std::is_copy_constructible_v<w1>);
    static_assert(std::is_nothrow_move_constructible_v<w1>);
    static_assert(!std::is_copy_assignable_v<w1>);
    static_assert(std::is_nothrow_move_assignable_v<w1>);
    static_assert(std::is_nothrow_destructible_v<w1>);
    static_assert(std::is_nothrow_swappable_v<w1>);

    static_assert(std::is_nothrow_default_constructible_v<w2>);
    static_assert(!std::is_copy_constructible_v<w2>);
    static_assert(std::is_nothrow_move_constructible_v<w2>);
    static_assert(!std::is_copy_assignable_v<w2>);
    static_assert(std::is_nothrow_move_assignable_v<w2>);
    static_assert(std::is_nothrow_destructible_v<w2>);
    static_assert(std::is_nothrow_swappable_v<w2>);
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
    }(); // scope - WrappedType being destroyed
    expect(eq(0, cApiRessource.counter));

    [] { // API schema 2: reference as input-output parameter
        using WrappedType = stdex::c_resource<S*, conS2, desS2>;
        expect(eq(0, cApiRessource.counter));
        WrappedType wrapped;
        expect(eq(1, cApiRessource.counter));
        expect(neq(nullptr, wrapped.get()));
        expect(eq(&cApiRessource, wrapped.get()));
    }(); // scope - WrappedType being destroyed
    expect(eq(0, cApiRessource.counter));
};

const static boost::ut::suite c_wrapper_functional_lambda = [] {
    using namespace boost::ut;
    constexpr auto construct = +[] {
        cApiRessource++;
        return &cApiRessource;
    };
    constexpr auto destruct = +[](S const*) { cApiRessource--; };

    [] { // API schema 1: pointer-type return value using lambda
        using WrappedType     = stdex::c_resource<S*, construct, destruct>;
        cApiRessource.counter = 0;
        expect(eq(0, cApiRessource.counter));
        WrappedType wrapped;
        expect(eq(1, cApiRessource.counter));
        expect(neq(nullptr, wrapped.get()));
    }(); // scope - WrappedType being destroyed
    expect(eq(0, cApiRessource.counter));
};

const static boost::ut::suite c_wrapper_functional_bool_return = [] {
    using namespace boost::ut;
    static int counter = 0;

    [] { // API schema 3a: boolean return value
        constexpr auto construct = +[] {
            counter++;
            return true;
        };
        constexpr auto destruct = +[] { counter--; };
        using WrappedType       = stdex::c_resource<bool, construct, destruct>;
        expect(eq(0, counter));
        WrappedType wrapped;
        expect(eq(1, counter));
    }(); // scope - WrappedType being destroyed
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value
        constexpr auto construct = +[] {
            counter++;
            return false;
        };
        constexpr auto destruct = +[] {
            counter--;
            std::println(stderr, "should not be invoked");
        };
        using WrappedType = stdex::c_resource<bool, construct, destruct>;
        expect(eq(0, counter));
        WrappedType wrapped;
        expect(eq(1, counter));
    }(); // scope - WrappedType being destroyed
    expect(eq(1, counter)); // destructor should not be invoked
    counter = 0;

    [] { // API schema 3a: boolean return value
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto construct    = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto destruct = +[] { counter--; };
        using WrappedType       = stdex::c_resource<bool, construct, destruct>;
        expect(eq(0, counter));
        WrappedType wrapped(42, 3.141, true); // all arguments provided explicitly
        expect(eq(1, counter));
        expect(eq(42, argument1));
        expect(eq(3.141, argument2));
        expect(eq(true, optArgument3));
    }(); // scope - WrappedType being destroyed
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value - ImGui use-case
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto construct    = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto destruct = +[] { counter--; };
        using WrappedType       = stdex::c_resource<bool, construct, destruct>;
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
        expect(eq(0, counter)); // scope - WrappedType being destroyed
    }(); // scope - WrappedType being destroyed
    expect(eq(0, counter));

#if defined(__GNUC__) && !defined(__clang__) && !defined(__EMSCRIPTEN__)
    // This code will only compile with GCC, not with Clang or Emscripten
    // -> to be further investigated whether this is due to gcc to relaxed in the template argument deduction or clang to strict
    [] { // API schema 3a: boolean return value
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto construct    = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto destruct = +[] { counter--; };
        using WrappedType       = stdex::c_resource<bool, construct, destruct>;
        expect(eq(0, counter));
        WrappedType wrapped(42, 3.141); // last argument is defaulted (seems to work only for gcc)
        expect(eq(1, counter));
        expect(eq(42, argument1));
        expect(eq(3.141, argument2));
        expect(eq(true, optArgument3));
    }(); // scope - WrappedType being destroyed
    expect(eq(0, counter));

    [] { // API schema 3a: boolean return value - ImGui use-case
        static int     argument1    = 0;
        static double  argument2    = 0;
        static bool    optArgument3 = false;
        constexpr auto construct    = +[](int arg1, double arg2, bool optArg3 = true) {
            counter++;
            argument1    = arg1;
            argument2    = arg2;
            optArgument3 = optArg3;
            return true;
        };
        constexpr auto destruct = +[] { counter--; };
        using WrappedType       = stdex::c_resource<bool, construct, destruct>;
        expect(eq(0, counter));
        bool invoked = false;
        if (auto _ = WrappedType(42, 3.141)) { // last argument is defaulted (seems to work only for gcc)
            expect(eq(1, counter));
            expect(eq(42, argument1));
            expect(eq(3.141, argument2));
            expect(eq(true, optArgument3));
            invoked = true;
        }
        expect(invoked);
        expect(eq(0, counter)); // scope - WrappedType being destroyed
    }(); // scope - WrappedType being destroyed
    expect(eq(0, counter));
#endif
};

int main() { /* not needed for ut */ }
