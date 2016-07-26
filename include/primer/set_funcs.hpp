//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/***
 * Useful function that registers a sequence of luaL_Reg-like
 * objects with a table on top of the stack. Similar to luaL_setfuncs.
 *
 * set_funcs_reverse uses the fuction as a key, as needed for persist table.
 *
 * Any container of objects matching the luaL_Reg concept is valid input.
 * A C-style pointer to the first entry of a null-terminated array of objects
 * matching the luaL_Reg concept is not valid input, but may be adapted using
 * is_L_Reg_seq trait.
 *
 * set_funcs_prefix appends a fixed prefix to the name of each function.
 * set_funcs_previs_reverse is the same, for `set_funcs_reverse`.
 * This is useful for populating permanent objects tables.
 */

#include <primer/base.hpp>

PRIMER_ASSERT_FILESCOPE;

#include <primer/lua.hpp>
#include <primer/support/asserts.hpp>

#include <string>

namespace primer {

template <typename T>
void
set_funcs(lua_State * L, T && seq) {
  PRIMER_ASSERT_STACK_NEUTRAL(L);
  PRIMER_ASSERT_TABLE(L);

  for (const auto & reg : seq) {
    if (reg.name && reg.func) {
      lua_pushcfunction(L, reg.func);
      lua_setfield(L, -2, reg.name);
    }
  }
}

template <typename T>
void
set_funcs_reverse(lua_State * L, T && seq) {
  PRIMER_ASSERT_STACK_NEUTRAL(L);
  PRIMER_ASSERT_TABLE(L);

  for (const auto & reg : seq) {
    if (reg.name && reg.func) {
      lua_pushcfunction(L, reg.func);
      lua_pushstring(L, reg.name);
      lua_settable(L, -3);
    }
  }
}

template <typename T>
void
set_funcs_prefix(lua_State * L, const std::string & prefix, T && seq) {
  PRIMER_ASSERT_STACK_NEUTRAL(L);
  PRIMER_ASSERT_TABLE(L);

  for (const auto & reg : seq) {
    if (reg.name && reg.func) {
      lua_pushcfunction(L, reg.func);
      lua_setfield(L, -2, (prefix + reg.name).c_str());
    }
  }
}

template <typename T>
void
set_funcs_prefix_reverse(lua_State * L, const std::string & prefix, T && seq) {
  PRIMER_ASSERT_STACK_NEUTRAL(L);
  PRIMER_ASSERT_TABLE(L);

  for (const auto & reg : seq) {
    if (reg.name && reg.func) {
      lua_pushcfunction(L, reg.func);
      lua_pushstring(L, (prefix + reg.name).c_str());
      lua_settable(L, -3);
    }
  }
}

} // end namespace primer