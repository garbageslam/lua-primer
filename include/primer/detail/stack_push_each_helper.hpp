//  (C) Copyright 2015 - 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

/***
 * Helper which calculates how much stack space is needed to push a bunch of
 * things onto the stack
 */

#include <primer/base.hpp>

PRIMER_ASSERT_FILESCOPE;

#include <primer/detail/count.hpp>
#include <primer/maybe_int.hpp>
#include <primer/detail/type_traits.hpp>
#include <primer/traits/push.hpp>

namespace primer {
namespace detail {

template <typename... Args>
struct stack_push_each_helper {

  template <typename T>
  struct impl;

  template <std::size_t... Is>
  struct impl<SizeList<Is...>> {
    static constexpr maybe_int value() noexcept {
      return maybe_int::max(maybe_int{0},
                            (stack_space_needed<::primer::traits::push<
                               remove_reference_t<remove_cv_t<Args>>>>::value +
                             Is)...);
    }
  };


  static constexpr maybe_int value() noexcept {
    return impl<Count_t<sizeof...(Args)>>::value();
  }
};

} // end namespace detail
} // end namespace primer