//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/***
 * A bound_function is a reference to an object of function type in a lua VM.
 *
 * This is a safe and convenience wrapper over lua_ref. The constructor provides
 * a check against `lua_isfunction`, and we provide several "call" methods which
 * are backed up by `primer/support/function.hpp`. These functions do not
 * require a lua_State * -- they attempt to lock the state that holds the
 * function, and the stack should be left unchanged, even if an error occurs.
 *
 * Calling this object will not raise a lua error or throw an exception.
 */

#include <primer/base.hpp>

PRIMER_ASSERT_FILESCOPE;

#include <primer/error.hpp>
#include <primer/expected.hpp>
#include <primer/lua.hpp>
#include <primer/lua_ref.hpp>
#include <primer/push.hpp>
#include <primer/support/function.hpp>
#include <primer/support/function_check_stack.hpp>
#include <primer/support/cpp_pcall.hpp>

#include <utility>

namespace primer {

//[ primer_bound_function
class bound_function {
  lua_ref ref_;

  //<-

  // Actually execute a call. This is permitted to raise lua errors but not
  // not to throw exceptions.
  template <typename return_type, typename... Args>
  static void call_impl(return_type & ret,
                        lua_State * L,
                        const lua_ref & fcn,
                        Args &&... args) {
    fcn.push(L);
    primer::push_each(L, std::forward<Args>(args)...);
    detail::fcn_call(ret, L, sizeof...(Args));
  }

  // Calls the call_impl in a protected context. This is no fail.
  template <typename return_type, typename... Args>
  expected<return_type> protected_call(Args &&... args) const noexcept {
    expected<return_type> result{primer::error{"Can't lock VM"}};
    if (lua_State * L = ref_.lock()) {
      if (auto stack_check = detail::check_stack_push_each<int, Args...>(L)) {
        auto ok =
          primer::mem_pcall(L, &call_impl<expected<return_type>, Args...>,
                            result, L, ref_, std::forward<Args>(args)...);
        if (!ok) { result = std::move(ok.err()); }
      } else {
        result = std::move(stack_check.err());
      }
    }
    return result;
  }

  // Another version, using `lua_ref_seq` as input instead of a parameter pack.
  template <typename return_type>
  static void call_impl2(return_type & ret,
                         lua_State * L,
                         const lua_ref & fcn,
                         const lua_ref_seq & inputs) {
    fcn.push(L);
    inputs.push_each(L);
    detail::fcn_call(ret, L, inputs.size());
  }

  // Calls the call_impl2 in a protected context. This is no fail.
  template <typename return_type>
  expected<return_type> protected_call2(const lua_ref_seq & inputs) const
    noexcept {
    expected<return_type> result{primer::error{"Can't lock VM"}};
    if (lua_State * L = ref_.lock()) {
      if (auto stack_check = detail::check_stack_push_n(L, 1 + inputs.size())) {
        auto ok = primer::mem_pcall(L, &call_impl2<expected<return_type>>,
                                    result, L, ref_, inputs);
        if (!ok) { result = std::move(ok.err()); }
      } else {
        result = std::move(stack_check.err());
      }
    }
    return result;
  }


  //->
public:
  // Special member functions
  bound_function() noexcept = default;
  bound_function(const bound_function &) = default;
  bound_function(bound_function &&) noexcept = default;
  bound_function & operator=(const bound_function &) = default;
  bound_function & operator=(bound_function &&) noexcept = default;
  ~bound_function() noexcept = default;

  // Primary constructor: Bind to a function on top of the stack.
  // Only capture the top item if it is actually a function.
  // Pop the item *whether or not* it is a function.
  /*<< Note: Can cause lua memory allocation failure from `ref_` ctor. >>*/
  explicit bound_function(lua_State * L);

  // Forwarded methods from lua_ref
  explicit operator bool() const noexcept { return static_cast<bool>(ref_); }

  lua_State * push() const noexcept { return ref_.push(); }
  bool push(lua_State * L) const noexcept { return ref_.push(L); }
  void reset() noexcept { ref_.reset(); }
  void swap(bound_function & other) noexcept { ref_.swap(other.ref_); }

  // Call methods
  // These methods attempt to lock the state which holds the lua function,
  // and perform the call there. They clean up after themselves and leave the
  // stack as they found it afterwards.
  //
  // If passed a lua_ref_seq, its members are the call arguments. If passed
  // any other sequence of C++ types, those objects are pushed onto the stack
  // and are the call arguments.

  template <typename... Args>
  expected<void> call_no_ret(Args &&... args) const noexcept;
  expected<void> call_no_ret(lua_ref_seq &) const noexcept;
  expected<void> call_no_ret(lua_ref_seq const &) const noexcept;
  expected<void> call_no_ret(lua_ref_seq &&) const noexcept;

  template <typename... Args>
  expected<lua_ref> call_one_ret(Args &&... args) const noexcept;
  expected<lua_ref> call_one_ret(lua_ref_seq &) const noexcept;
  expected<lua_ref> call_one_ret(lua_ref_seq const &) const noexcept;
  expected<lua_ref> call_one_ret(lua_ref_seq &&) const noexcept;

  template <typename... Args>
  expected<lua_ref_seq> call(Args &&... args) const noexcept;
  expected<lua_ref_seq> call(lua_ref_seq &) const noexcept;
  expected<lua_ref_seq> call(lua_ref_seq const &) const noexcept;
  expected<lua_ref_seq> call(lua_ref_seq &&) const noexcept;
};
//]

inline bound_function::bound_function(lua_State * L) //
    : ref_(lua_gettop(L) ? (lua_isfunction(L, -1) ? L : (lua_pop(L, 1), nullptr))
                         : nullptr) //
  {}


/// Same thing now but with a lua_ref_seq
// Use a macro so that we can get const &, &&, and & qualifiers defined.
#define CALL_ARGS_HELPER(N, T)                                                 \
  template <typename... Args>                                                  \
  inline expected<T> bound_function::N(Args &&... args) const noexcept {       \
    return this->protected_call<T>(std::forward<Args>(args)...);               \
  }


#define CALL_REF_SEQ_HELPER(N, T, Q)                                           \
  inline expected<T> bound_function::N(lua_ref_seq Q inputs) const noexcept {  \
    return this->protected_call2<T>(inputs);                                   \
  }

#define CALL_DEFINITIONS(N, T)                                                 \
  CALL_ARGS_HELPER(N, T)                                                       \
  CALL_REF_SEQ_HELPER(N, T, &)                                                 \
  CALL_REF_SEQ_HELPER(N, T, const &)                                           \
  CALL_REF_SEQ_HELPER(N, T, &&)

  // Actual declarations

  CALL_DEFINITIONS(call_no_ret, void)
  CALL_DEFINITIONS(call_one_ret, lua_ref)
  CALL_DEFINITIONS(call, lua_ref_seq)

#undef CALL_ARGS_HELPER
#undef CALL_REF_SEQ_HELPER
#undef CALL_DEFINITIONS


inline void swap(bound_function & one, bound_function & other) noexcept {
  one.swap(other);
}

} // end namespace primer
