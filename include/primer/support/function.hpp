//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/***
 * Support code related to calling functions, capturing their return values,
 * reporting errors. Mostly this means "safe" or "friendly" wrappers over
 * lua_pcall and lua_resume.
 */

#include <primer/base.hpp>

PRIMER_ASSERT_FILESCOPE;

#include <primer/error.hpp>
#include <primer/expected.hpp>
#include <primer/lua.hpp>
#include <primer/lua_ref.hpp>
#include <primer/lua_ref_seq.hpp>
#include <primer/support/error_capture.hpp>
#include <primer/support/push_cached.hpp>
#include <tuple>
#include <utility>

namespace primer {

namespace detail {

// TODO: Would be nice to push our own C traceback function which can be
// customized and technically cannot cause lua error when we luaopen_debug.
inline void fetch_traceback_function(lua_State * L) noexcept {
  luaopen_debug(L);
  int result = lua_getfield(L, -1, "traceback");
  PRIMER_ASSERT(result == LUA_TFUNCTION,
                "could not find debug traceback function");
  static_cast<void>(result);
  lua_remove(L, -2);
}

// Expects: Function, followed by narg arguments, on top of the stack.
// Calls pcall with traceback error handler, removes error handler.
// Error is left on top of the stack.
// Returns the error code, and the stack index at which the return values start.
inline std::tuple<int, int> pcall_helper(lua_State * L,
                                         int narg,
                                         int nret) noexcept {
  PRIMER_ASSERT(lua_gettop(L) >= (1 + narg),
                "Not enough arguments on stack for pcall!");
  PRIMER_ASSERT(lua_isfunction(L, -1 - narg), "Missing function for pcall!");
  detail::push_cached<fetch_traceback_function>(L);
  lua_insert(L, -2 - narg);
  const int error_handler_index = lua_absindex(L, -2 - narg);
  const int result_code = lua_pcall(L, narg, nret, error_handler_index);
  lua_remove(L, error_handler_index);

  return std::tuple<int, int>{result_code, error_handler_index};
}

// Expects: Function, followed by narg arguments, on top of the stack.
// Calls lua_resume. If an error occurs, calls the traceback error handler,
// removes error handler. Error is left on top of the stack.
// Returns the error code, and the stack index at which the return values start.
inline std::tuple<int, int> resume_helper(lua_State * L, int narg) noexcept {
  PRIMER_ASSERT(lua_gettop(L) >= (narg),
                "Not enough arguments on stack for resume!");

  const int result_index = lua_absindex(L, -1 - narg);

  const int result_code = lua_resume(L, nullptr, narg);
  if ((result_code != LUA_OK) && (result_code != LUA_YIELD)) {
    detail::push_cached<fetch_traceback_function>(L);
    lua_insert(L, -2);
    lua_call(L, 1, 1);
  }

  return std::tuple<int, int>{result_code, result_index};
}

// Helper structures to write a generic call function which works regardless of
// number of returns

template <typename T>
struct return_helper;

template <>
struct return_helper<void> {
  using return_type = expected<void>;

  static void pop(lua_State *, int, return_type & result) noexcept {
    result = {};
  }
  static constexpr int nrets = 0;
};

template <>
struct return_helper<lua_ref> {
  using return_type = expected<lua_ref>;

  static void pop(lua_State * L, int, return_type & result) {
    result = lua_ref{L};
  }
  static constexpr int nrets = 1;
};

template <>
struct return_helper<lua_ref_seq> {
  using return_type = expected<lua_ref_seq>;

  static void pop(lua_State * L, int start_idx, return_type & result) {
    PRIMER_TRY_BAD_ALLOC {
      result = return_type{default_construct_in_place_tag{}};
      primer::pop_n(L, lua_gettop(L) - start_idx + 1, *result);
    }
    PRIMER_CATCH_BAD_ALLOC { result = primer::error{bad_alloc_tag{}}; }
  }

  static constexpr int nrets = LUA_MULTRET;
};


/***
 * Generic scheme for calling a function
 * Note: NOT noexcept, it can `primer::pop_n` can cause lua memory allocation
 * failure.
 * It is `noexcept` in the no return values case.
 *
 * Note: This function should not have nontrivial objects on the stack.
 */
template <typename T>
void fcn_call(expected<T> & result, lua_State * L, int narg) {
  int err_code;
  int results_idx;

  std::tie(err_code, results_idx) =
    detail::pcall_helper(L, narg, return_helper<T>::nrets);
  if (err_code != LUA_OK) {
    result = detail::pop_error(L, err_code);
  } else {
    return_helper<T>::pop(L, results_idx, result);
  }

  PRIMER_ASSERT(lua_gettop(L) == (results_idx - 1),
                "hmm stack discipline error");
}

/***
 * Generic scheme for resuming a coroutine
 */
template <typename T>
void resume_call(expected<T> & result, lua_State * L, int narg) {
  int err_code;
  int results_idx;

  std::tie(err_code, results_idx) = detail::resume_helper(L, narg);
  if (err_code == LUA_OK || err_code == LUA_YIELD) {
    return_helper<T>::pop(L, results_idx, result);
  } else {
    result = detail::pop_error(L, err_code);
  }
  lua_settop(L, results_idx - 1);
}

} // end namespace detail

// Expects: Function, followed by narg arguments, on top of the stack.
// Returns: Either a reference to the value, or an error message. In either case
// the results are cleared from the stack.
inline expected<lua_ref> fcn_call_one_ret(lua_State * L, int narg) {
  expected<lua_ref> result;
  detail::fcn_call(result, L, narg);
  return result;
}

// Expects: Function, followed by narg arguments, on top of the stack.
// Returns either a reference to the value, or an error message. In either case
// the results are cleared from the stack.
// This one is noexcept because it does not create any lua_ref's.
inline expected<void> fcn_call_no_ret(lua_State * L, int narg) noexcept {
  expected<void> result;
  detail::fcn_call(result, L, narg);
  return result;
}

// Expects: Function, followed by narg arguments, on top of the stack.
// Returns all of the functions' results or an error message. In either case
// the results are cleared from the stack.
inline expected<lua_ref_seq> fcn_call(lua_State * L, int narg) {
  expected<lua_ref_seq> result;
  detail::fcn_call(result, L, narg);
  return result;
}


// Expects a thread stack, satisfying the preconditions to call `lua_resume(L,
// nullptr, narg)`.
//   (That means, there should be narg arguments on top of the stack.
//    If coroutine has not been started, the function should be just beneath
//    them.
//    Otherwise, it shouldn't be.)
//
// Calls lua_resume with that many arguments.
// If it returns or yields, the single (expected) return value is popped from
// the stack.
// If there is an error, an error object is returned and the error message is
// popped from the stack, after running an error handler over it.
// The expected<lua_ref> is first return value.
// Use `lua_status` to figure out if it was return or yield.
inline expected<lua_ref> resume_one_ret(lua_State * L, int narg) {
  expected<lua_ref> result;
  detail::resume_call(result, L, narg);
  return result;
}

// Expects a thread stack, satisfying the preconditions to call `lua_resume(L,
// nullptr, narg)`.
//   (That means, there should be narg arguments on top of the stack.
//    If coroutine has not been started, the function should be just beneath
//    them.
//    Otherwise, it shouldn't be.)
//
// Calls lua_resume with that many arguments.
// If it returns or yields, the single (expected) return value is popped from
// the stack.
// If there is an error, an error object is returned and the error message is
// popped from the stack, after running an error handler over it.
// The expected<void> is (potentially) the error message.
// Use `lua_status` to figure out if it was return or yield.
// This one is noexcept because it does not create any lua_ref's.
inline expected<void> resume_no_ret(lua_State * L, int narg) noexcept {
  expected<void> result;
  detail::resume_call(result, L, narg);
  return result;
}

// Expects a thread stack, satisfying the preconditions to call `lua_resume(L,
// nullptr, narg)`.
//   (That means, there should be narg arguments on top of the stack.
//    If coroutine has not been started, the function should be just beneath
//    them.
//    Otherwise, it shouldn't be.)
//
// Calls lua_resume with that many arguments.
// If it returns or yields, all of its return values are returned.
// If there is an error, an error object is returned and the error message is
// popped from the stack,
// after running an error handler over it.
// The expected<lua_ref_seq> is return sequence.
// Use `lua_status` to figure out if it was return or yield.
inline expected<lua_ref_seq> resume(lua_State * L, int narg) {
  expected<lua_ref_seq> result;
  detail::resume_call(result, L, narg);
  return result;
}

} // end namespace primer
