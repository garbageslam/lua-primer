//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/***
 * In order to safely use lua, the user must use `lua_checkstack` to check that
 * there is enough stack space remaining to perform the desired operations.
 *
 * Lua does not do these checks internally because it would cause overhead in
 * every operation. They prefer to leave it up to the user.
 *
 * We also would prefer to leave it up to the user. However, it can be complex
 * to estimate how much stack space is needed for some complicated container
 * type or structure.
 *
 * To help with that, we provide a facility to estimate at compile time how much
 * stack space it will cost to read or write a given value.
 *
 * The tricky part is that we don't require the user to annotate their types
 * with this -- it may be variable or impossible to estimate.
 *
 * Therefore, we need a literal type which represents eithter an integer or
 * "unknown". We could use `std::optional`, which is `constexpr` in C++17, but
 * we assume only C++11.
 *
 * Therefore we just throw something suitable together.
 */

#include <type_traits>
#include <utility>

namespace primer {

// Poor man's constexpr optional<int>
class maybe_int {
  int value;
  bool unknown;

public:
  // Ctors
  constexpr maybe_int() noexcept : value(0), unknown(true) {}
  constexpr maybe_int(maybe_int &&) noexcept = default;
  constexpr maybe_int(const maybe_int &) noexcept = default;
  maybe_int & operator=(const maybe_int &) noexcept = default;
  maybe_int & operator=(maybe_int &&) noexcept = default;

  constexpr explicit maybe_int(int v) noexcept : value(v), unknown(false) {}

  // Accessors
  constexpr int operator*() const noexcept { return value; }
  constexpr explicit operator bool() const noexcept { return !unknown; }

  // No implicit constructor from int, but there is an explicit converter
  static constexpr maybe_int to_maybe_int(maybe_int m) { return m; }
  static constexpr maybe_int to_maybe_int(int i) { return maybe_int{i}; }

  // Right associate a binary operation (F) over maybe_int's.
  template <typename F, typename T>
  static constexpr maybe_int right_associate(F &&, T && a) {
    return maybe_int::to_maybe_int(a);
  }

  template <typename F, typename T, typename... Args>
  static constexpr maybe_int right_associate(F && f, T a, Args &&... args) {
    return std::forward<F>(
      f)(maybe_int::to_maybe_int(a),
         right_associate(std::forward<F>(f), std::forward<Args>(args)...));
  }

  // Lift
  template <typename F>
  struct lifted {
    F f;
    constexpr maybe_int operator()(maybe_int a, maybe_int b) const noexcept {
      return (a && b) ? maybe_int{f(*a, *b)} : maybe_int{};
    }
  };

  template <typename F>
  static constexpr auto lift(F && f) -> lifted<F> {
    return lifted<F>{std::forward<F>(f)};
  }

  // Liftees
  static constexpr int add_int(int a, int b) { return a + b; }
  static constexpr int sub_int(int a, int b) { return a - b; }
  static constexpr int mult_int(int a, int b) { return a * b; }
  static constexpr int max_int(int a, int b) { return a > b ? a : b; }
  static constexpr int min_int(int a, int b) { return a < b ? a : b; }

  // Max
  template <typename... Args>
  static constexpr maybe_int max(Args &&... args) noexcept {
    return right_associate(lift(max_int), std::forward<Args>(args)...);
  }

  // Min
  template <typename... Args>
  static constexpr maybe_int min(Args &&... args) noexcept {
    return right_associate(lift(min_int), std::forward<Args>(args)...);
  }

  // For convenience
  constexpr maybe_int operator+(int i) const {
    return lift(add_int)(*this, maybe_int{i});
  }

  constexpr maybe_int operator+(maybe_int i) const {
    return lift(add_int)(*this, i);
  }

  constexpr maybe_int operator*(int i) const {
    return lift(mult_int)(*this, maybe_int{i});
  }

  constexpr maybe_int operator*(maybe_int i) const {
    return lift(mult_int)(*this, i);
  }

  constexpr maybe_int operator-(int i) const {
    return lift(sub_int)(*this, maybe_int{i});
  }

  constexpr maybe_int operator-(maybe_int i) const {
    return lift(sub_int)(*this, i);
  }

  // Unary minus
  constexpr maybe_int operator-() const {
    return this->unknown ? maybe_int{} : maybe_int{-this->value};
  }
};

constexpr inline maybe_int operator+(int a, maybe_int b) { return b + a; }
constexpr inline maybe_int operator-(int a, maybe_int b) { return b - a; }
constexpr inline maybe_int operator*(int a, maybe_int b) { return b * a; }

/***
 * Trait which grabs from a structure the value "stack_space_needed", as a
 * `maybe_int`.
 * If there is no such member, the trait returns "unknown".
 */

template <typename T, typename ENABLE = void>
struct stack_space_needed {
  static constexpr maybe_int value{};
};

template <typename T>
struct stack_space_needed<
  T,
  typename std::enable_if<std::is_same<decltype(T::stack_space_needed),
                                       decltype(T::stack_space_needed)>::value>::type> {
  static constexpr maybe_int value{T::stack_space_needed};
};

} // end namespace primer