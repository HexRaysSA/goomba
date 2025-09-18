/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once

#include "equiv_class.hpp"
#include "smt_convert.hpp"
#include "heuristics.hpp"
#include "lin_conj_exprs.hpp"
#include "simp_lin_conj_exprs.hpp"
#include "nonlin_expr.hpp"

//--------------------------------------------------------------------------
inline void substitute(minsn_t *insn, minsn_t *cand)
{
  cand->d.swap(insn->d);
  insn->swap(*cand);
}

//--------------------------------------------------------------------------
class optimizer_t
{
public:
  uint z3_timeout = 1000;
  bool z3_assume_timeouts_correct = true;
  equiv_class_finder_t *equiv_classes = nullptr;
  bool optimize_insn(minsn_t *insn); // attempts to replace the instruction with a simpler version
  bool optimize_insn_recurse(minsn_t *insn); // attempts to optimize the instruction, and if it fails, optimizes its subinstructions
};
