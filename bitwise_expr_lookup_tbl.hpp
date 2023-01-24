/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include "minsn_template.hpp"

// bw_expr_tbl_t is a singleton class that maintains a lookup table mapping
// boolean function evaluation traces (i.e. I/O behavior) to the shortest
// representation of each boolean function.
// for instance, if you found a boolean function f(x, y) with the following
// behavior: f(0, 0) = 0, f(0, 1) = 0, f(1, 0) = 0, f(1, 1) = 1, then you
// can query this object to find that f(x, y) = x & y.
// note that we do not consider any functions that return 1 on the all-zeros
// input.
class bw_expr_tbl_t
{
  qvector<minsn_templates_t> tbl;

public:
  static bw_expr_tbl_t instance;

  // do not call directly, use instance instead
  bw_expr_tbl_t();

  // eval_trace is a bitmap whose i'th bit contains the
  // boolean function's evaluation on the i'th conjunction,
  // where conjunctions are ordered in the same way as in lin_conj_exprs.hpp
  minsn_template_ptr_t lookup(int nvars, uint64_t bit_trace)
  {
    QASSERT(30698, (bit_trace & 1) == 0);
    QASSERT(30699, nvars <= 3);
    QASSERT(30700, nvars >= 1);
    QASSERT(30701, bit_trace < (1ull << (1ull << (nvars))));
    return tbl[nvars-1][bit_trace >> 1];
    // since the 0th conjunction is never considered, all vector indices are
    // divided by 2. See the corresponding .cpp file for more info.
  }
};
