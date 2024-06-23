#ifndef C_RESOURCE_WRAPPER_H
#define C_RESOURCE_WRAPPER_H

#include <version>

#if __cpp_lib_modules >= 202207L
import std;
#else

#include <concepts>
#include <cstring>
#include <type_traits>

#endif

/**
 * @brief RAII wrapper for managing C-style resources in a C++ context, supporting various API schemas.
 *
 * This wrapper is designed to manage resources that are typically handled in C-style APIs, such as dynamically
 * allocated objects, file handles, or other system resources. It provides a modern C++ approach with move-only
 * value semantics, encapsulating the resource management logic and ensuring safe and automatic cleanup.
 *
 * The wrapper is versatile, supporting different resource management schemas and capable of handling both
 * pointer and non-pointer types, including integral types and booleans. It allows for seamless integration
 * of C-style resources into C++ code, promoting code safety, clarity, and reducing the risk of resource leaks.
 *
 * Key Features:
 * - Supports various construction and destruction schemas.
 * - Handles both pointer and non-pointer resource types.
 * - Ensures automatic resource cleanup using RAII principles.
 * - Provides a clear and concise API for resource management.
 *
 * Original implementation inspired by Daniela Engert (published under CC-BY-SA-4.0)
 * (https://github.com/DanielaE/CppInAction/blob/main/Demo-App/c_resource.hpp)
 * and Ivan Sokolov (published under MIT License)
 * (https://github.com/ocornut/imgui/issues/2096#issuecomment-1463837461).
 * Extended to support a wider range of use-cases and resource types.
 */

namespace stdex {

// customisation point to support different 'null' values
template<typename T>
inline constexpr T c_resource_null_value = nullptr;

namespace detail {
template<typename T>
struct function_traits : function_traits<decltype(&T::operator())> {};

template<typename ReturnType, typename... Args>
struct function_traits<ReturnType (*)(Args...)> {
    using return_type = ReturnType;
};

template<typename T>
inline constexpr bool always_false = false;

} // namespace detail

/**
 * The {@code c_resource} class supports multiple API schemas for resource construction and destruction,
 * accommodating a variety of C-style resource management patterns:
 *
 * <p>Schema 1:
 * <ul>
 *   <li>Constructor functions return the constructed entity as an output value (e.g., {@code thing *construct();}).
 *   <li>Destructor functions take the disposable entity as an input value (e.g., {@code void destruct(thing *);}).
 *   <li>Example:
 *   <pre>{@code
 *   struct S { ... };
 *   S *conS1();      // Constructor function
 *   void desS1(S *); // Destructor function
 *   using WrappedType = stdex::c_resource<S *, conS1, desS1>;
 *   WrappedType wrapped; // Manages an S resource using schema 1
 *   }</pre>
 * </ul>
 *
 * <p>Schema 2:
 * <ul>
 *   <li>Both constructor and destructor functions take an input-output reference to the resource pointer
 *       (e.g., {@code void construct(thing **);}, {@code void destruct(thing **);}).
 *   <li>These functions modify the referenced resource pointer accordingly.
 *   <li>Modifiers (e.g., {@code replace(...)}) exist for schema 2, acting like constructors but returning the value of the construct function.
 *   <li>Example:
 *   <pre>{@code
 *   void conS2(S **); // Constructor function
 *   void desS2(S **); // Destructor function
 *   using WrappedType = stdex::c_resource<S *, conS2, desS2>;
 *   WrappedType wrapped; // Manages an S resource using schema 2
 *   }</pre>
 * </ul>
 *
 * <p>Extended Schemas:
 * <ul>
 *   <li>Supports constructors returning non-pointer types, such as booleans or integers.
 *   <li>Example:
 *   <pre>{@code
 *   bool construct(int, double, bool = true); // Constructor function with default argument
 *   void destruct();                          // Destructor function
 *   using WrappedType = stdex::c_resource<bool, construct, destruct>;
 *   WrappedType wrapped(42, 3.141, true); // Manages a boolean resource
 *   }</pre>
 * </ul>
 *
 * <p>ImGui-Style Example:
 * <pre>{@code
 * using WrappedType = stdex::c_resource<bool, construct, destruct>;
 * if (auto _ = WrappedType(42, 3.141, true)) {
 *     // User-code
 *     // 'destruct' is implicitly called on exiting the if statement
 * }
 * }</pre>
 *
 * <p>The design of {@code c_resource} ensures that resources are managed safely according to the specified schema,
 * providing a consistent and type-safe interface for resource management in C++.
 */
template<typename T, auto* constructFunction, auto* destructFunction, bool unconditionallyDestruct = false>
struct c_resource {
    using element_type = T;

private:
    using TConstructor     = decltype(constructFunction);
    using TDestructor      = decltype(destructFunction);
    using TConstructReturn = typename detail::function_traits<TConstructor>::return_type;

    static_assert(std::is_function_v<std::remove_pointer_t<TConstructor>>, "needs a free-standing C-style constructor function");
    static_assert(constructFunction != nullptr, "constructFunction must not be null");
    static_assert(std::is_function_v<std::remove_pointer_t<TDestructor>>, "needs a free-standing C-style destructor function");
    static_assert(destructFunction != nullptr, "destructFunction must not be null");
    static_assert(!std::is_pointer_v<T> || (std::is_invocable_v<TDestructor, T> || std::is_invocable_v<TDestructor, T*>), "require either destructFunction(T) or destructFunction(T*)");

    constexpr static auto initElement() {
        if constexpr (std::is_pointer_v<T>) {
            return c_resource_null_value<T>; // pointer-type value
        } else {
            return element_type{}; // integral value
        }
    }
    static constexpr element_type initValue = initElement();
    element_type                  _element  = initValue;

    constexpr static void destruct(element_type& p) noexcept
    requires std::is_invocable_v<TDestructor, T>
    {
        if (p != initValue) {
            destructFunction(p);
        }
    }
    constexpr static void destruct(element_type& p) noexcept
    requires std::is_invocable_v<TDestructor, T*>
    {
        if (p != initValue) {
            destructFunction(&p);
        }
    }

public:
    // default constructor for API schema 1
    [[nodiscard]] constexpr c_resource() noexcept
    requires requires { constructFunction(); } && std::is_invocable_r_v<T, TConstructor>
    {
        _element = constructFunction();
    }

    // default constructor for API schema 2
    [[nodiscard]] constexpr explicit c_resource() noexcept
    requires requires { construct(&_element); }
    {
        construct(&_element);
    }

    template<typename... Ts>
    requires(std::is_invocable_r_v<TConstructReturn, TConstructor, Ts...> || std::is_invocable_r_v<TConstructReturn, TConstructor, Ts..., bool>)
    [[nodiscard]] constexpr explicit(sizeof...(Ts) == 1) c_resource(Ts&&... Args) noexcept {
        _element = [](auto&&... args) -> decltype(auto) { // construct wrapper to allow for default constructor arguments
            return constructFunction(std::forward<decltype(args)>(args)...);
        }(std::forward<Ts>(Args)...);
    }

    template<typename... Ts>
    requires(std::is_invocable_v<TConstructor, T*, Ts...>)
    [[nodiscard]] constexpr explicit(sizeof...(Ts) == 1) c_resource(Ts&&... Args) noexcept {
        if constexpr (std::is_pointer_v<T>) {
            _element = initValue;
            constructFunction(&_element, std::forward<Ts>(Args)...);
        } else {
            _element = [](auto&&... args) -> decltype(auto) { // construct wrapper to allow for default constructor arguments
                return constructFunction(std::forward<decltype(args)>(args)...);
            }(std::forward<Ts>(Args)...);
        }
    }

    template<typename... Ts>
    requires(std::is_invocable_v<TConstructor, T*, Ts...>)
    [[nodiscard]] constexpr auto emplace(Ts&&... Args) noexcept
    requires std::is_pointer_v<T>
    {
        destruct(_element);
        _element = initValue;
        return construct(&_element, static_cast<Ts&&>(Args)...);
    }

    [[nodiscard]] constexpr c_resource(c_resource&& other) noexcept
    requires std::is_pointer_v<T>
    {
        if (this != &other) {
            _element = std::exchange(other._element, initValue);
        }
    }
    constexpr c_resource& operator=(c_resource&& rhs) noexcept
    requires std::is_pointer_v<T>
    {
        if (this != &rhs) {
            destruct(_element);
            _element = std::exchange(rhs._element, initValue);
        }
        return *this;
    }

    constexpr ~c_resource() noexcept {
        if constexpr (std::is_pointer_v<T>) {
            destruct(_element);
        } else {
            if constexpr (unconditionallyDestruct) {
                destructFunction();
            } else {
                if (_element) {
                    destructFunction();
                }
            }
        }
    }

    constexpr void clear() noexcept {
        if constexpr (std::is_pointer_v<T>) {
            destruct(_element);
            _element = initValue;
        } else {
            destructFunction();
        }
    }
    constexpr c_resource& operator=(std::nullptr_t) noexcept {
        clear();
        return *this;
    }

    [[nodiscard]] constexpr explicit operator bool() const& noexcept { return _element != initValue; }
    [[nodiscard]] constexpr explicit operator bool() && { // N.B. suppresses user-code error
        static_assert(detail::always_false<T>, "\n**** you probably did not intend this. The resource must be assigned to an lvalue! ****\n"
                                               "**** e.g. potential error: if (ImScoped::Window(...)) // as the temporary would be destroyed immediately ****");
        return false;
    }

    [[nodiscard]] constexpr bool        empty() const noexcept { return _element == initValue; }
    [[nodiscard]] constexpr friend bool have(const c_resource& r) noexcept { return r._element != initValue; }

    auto               operator<=>(const c_resource&) = delete;
    [[nodiscard]] bool operator==(const c_resource& rhs) const noexcept { return 0 == std::memcmp(_element, rhs._element, sizeof(*_element)); }

#if defined(__cpp_explicit_this_parameter)
    template<typename U, typename V>
    static constexpr bool less_const = std::is_const_v<U> < std::is_const_v<V>;
    template<typename U, typename V>
    static constexpr bool similar = std::is_same_v<std::remove_const_t<U>, T>;

    template<typename U, typename Self>
    requires(similar<U, T> && !less_const<U, Self>)
    [[nodiscard]] constexpr operator U*(this Self&& self) noexcept {
        return std::forward_like<Self>(self._element);
    }
    [[nodiscard]] constexpr auto operator->(this auto&& self) noexcept { return std::forward_like<decltype(self)>(self._element); }
    [[nodiscard]] constexpr auto get(this auto&& self) noexcept { return std::forward_like<decltype(self)>(self._element); }
#else
    [[nodiscard]] constexpr explicit operator element_type() noexcept
    requires std::is_pointer_v<element_type>
    {
        return like(*this);
    }
    [[nodiscard]] constexpr explicit operator element_type() const noexcept
    requires std::is_pointer_v<element_type>
    {
        return like(*this);
    }
    [[nodiscard]] constexpr element_type operator->() noexcept
    requires std::is_pointer_v<element_type>
    {
        return like(*this);
    }
    [[nodiscard]] constexpr element_type operator->() const noexcept
    requires std::is_pointer_v<element_type>
    {
        return like(*this);
    }
    [[nodiscard]] constexpr element_type       get() noexcept { return like(*this); }
    [[nodiscard]] constexpr const element_type get() const noexcept { return like(*this); }

private:
    static constexpr auto like(c_resource& self) noexcept { return self._element; }
    static constexpr auto like(const c_resource& self) noexcept { return static_cast<const element_type>(self._element); }

public:
#endif

    constexpr void reset(element_type ptr = initValue) noexcept
    requires std::is_pointer_v<T>
    {
        destruct(_element);
        _element = ptr;
    }

    constexpr element_type release() noexcept
    requires std::is_pointer_v<T>
    {
        auto ptr = _element;
        _element = initValue;
        return ptr;
    }

    template<auto* CleanupFunction>
    requires std::is_pointer_v<T>
    struct guard {
        using cleaner = decltype(CleanupFunction);

        static_assert(std::is_function_v<std::remove_pointer_t<cleaner>>, "I need a C function");
        static_assert(std::is_invocable_v<cleaner, element_type>, "Please check the function");

        constexpr explicit guard(c_resource& Obj) noexcept : ptr_{Obj._element} {}
        constexpr ~guard() noexcept {
            if (ptr_ != initValue) {
                CleanupFunction(ptr_);
            }
        }

    private:
        element_type ptr_;
    };
};

template<auto* constructFunction, auto* destructFunction>
struct c_resource<void, constructFunction, destructFunction, true> {
    template<typename... Ts>
    c_resource(Ts&&... args) {
        constructFunction(std::forward<Ts>(args)...);
    }

    ~c_resource() { destructFunction(); }
};

} // namespace stdex

#endif // C_RESOURCE_WRAPPER_H
