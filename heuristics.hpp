/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include "mcode_emu.hpp"
#include "linear_exprs.hpp"

const uint64 SPECIAL[] = { 0, 1, 0xffffffffffffffff };
const int NUM_SPECIAL = qnumber(SPECIAL);
const double SPECIAL_PROBABILITY = 0.2; // probability of selecting a special number when sampling

// an expression must have at least this many subinstructions of each type to count as an MBA
const int MIN_MBA_BOOL_OPS = 1;
const int MIN_MBA_ARITH_OPS = 1;

// number of test cases to run when checking if an instruction matches the candidate expression's behavior
const int NUM_TEST_CASES = 256;

//-------------------------------------------------------------------------
mcode_val_t gen_rand_mcode_val(int size);

//-------------------------------------------------------------------------
// emulates the microcode, assigning random values to unknown variables
// (but keeping them consistent across executions)
struct mcode_emu_rand_vals_t : public mcode_emulator_t
{
  std::map<const mop_t, mcode_val_t> assigned_vals;

  mcode_val_t get_var_val(const mop_t &mop) override
  {
    // check that the mop is indeed a variable
    mopt_t t = mop.t;
    QASSERT(30672, t == mop_r || t == mop_S || t == mop_v || t == mop_l);

    auto assignment = assigned_vals.find(mop);
    if ( assignment != assigned_vals.end() )
      return assignment->second;

    mcode_val_t new_val = gen_rand_mcode_val(mop.size);
    assigned_vals.insert( { mop, new_val } );
    return new_val;
  }
};

//-------------------------------------------------------------------------
bool is_mba(const minsn_t &insn);

//-------------------------------------------------------------------------
bool probably_equivalent(const minsn_t &insn, const candidate_expr_t &expr);
bool probably_equivalent(const minsn_t &a, const minsn_t &b);

//-------------------------------------------------------------------------
// estimates the "complexity" of a given instruction
int score_complexity(const minsn_t &insn);

struct minsn_complexity_cmptr_t
{
  bool operator()(const minsn_t *a, const minsn_t *b) const
  {
    auto score_a = score_complexity(*a);
    auto score_b = score_complexity(*b);
    return score_a < score_b;
  }
};

inline mopvec_t get_input_mops(const minsn_t &insn)
{
  default_zero_mcode_emu_t emu;
  emu.minsn_value(insn); // populate emu.assigned_vals

  mopvec_t res;
  res.reserve(emu.assigned_vals.size());
  for ( auto const &entry : emu.assigned_vals )
    res.push_back(entry.first);

  std::sort(res.begin(), res.end());
  return res;
}