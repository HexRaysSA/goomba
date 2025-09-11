/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include "linear_exprs.hpp"

const uint64 SPECIAL[] = { 0, 1, 0xffffffffffffffff };
const uint8 SPECIAL8[] = { 0, 1, 0xff };
const int NUM_SPECIAL = qnumber(SPECIAL);
const double SPECIAL_PROBABILITY = 0.2; // probability of selecting a special number when sampling

// an expression must have at least this many subinstructions of each type to count as an MBA
const int MIN_MBA_BOOL_OPS = 1;
const int MIN_MBA_ARITH_OPS = 1;

// number of test cases to run when checking if an instruction matches the candidate expression's behavior
const int NUM_TEST_CASES = 256;

//-------------------------------------------------------------------------
intval64_t gen_rand_mcode_val(int size);
uint8 gen_rand_byte();

//-------------------------------------------------------------------------
// a data structure for storing the values of memory mops as bytes to solve
// the overlaping problems, e.g,.:
// mem_op1: [0x1000, 0x1003]
// mem_op2: [0x1002, 0x1005]
struct byte_val_map_t
{
  std::map<const uval_t, uint8> stk_map;      // stack variable mapping
  std::map<const uval_t, uint8> glb_map;      // global variable mapping
  std::map<const uval_t, uint8> local_map;    // local variable mapping
  std::map<const uval_t, uint8> reg_map;      // register mapping

  std::map<const mop_t, intval64_t> cache;   // save the seen mop->mcode_val pair

  //-------------------------------------------------------------------------
  // build a mcode_val from a byte qvector
  intval64_t bv2mcode_val(qvector<uint8> &bv)
  {
    uint64 result = 0;
    for ( uint8 b : bv )
    {
      result = (result << 8) | b;
    }

    return intval64_t(result, bv.size());
  }

  //-------------------------------------------------------------------------
  // find the value of the bytes [off, off+size) in map, assemble the result
  // as a intval64_t. Create random bytes for new values and update the map.
  intval64_t find_update(uval_t off, size_t size, std::map<const uval_t, uint8> &map)
  {
    qvector<uint8> bytes;

    for ( int i = size - 1; i >= 0; --i )
    {                               // iterate the addresses from high to low, so the most
      uval_t mem_addr = off + i;    // significant digit is the first element of the expr_vector
      auto result = map.find(mem_addr);
      if ( result != map.end() )
      {
        bytes.add(result->second);
      }
      else
      {
        // create a new random byte
        uint8 rand_byte = gen_rand_byte();
        bytes.add(rand_byte);
        map[mem_addr] = rand_byte;
      }
    }

    return bv2mcode_val(bytes);
  }

  //-------------------------------------------------------------------------
  intval64_t lookup(const mop_t &op)
  {
    auto it = cache.find(op);
    if ( it != cache.end() )
      return it->second;

    intval64_t result(0, 1);   // initialize intval64_t, size must be at least 1
    switch ( op.t )
    {
      case mop_S:         // stack variable
        result = find_update(op.s->off, op.size, stk_map);
        break;
      case mop_v:         // global variable
        result = find_update(op.g, op.size, glb_map);
        break;
      case mop_l:         // local variable
        result = find_update(op.l->off, op.size, local_map);
        break;
      case mop_r:         // register
        result = find_update(op.r, op.size, reg_map);
        break;
      default:
        INTERR(30824);
    }

    cache.insert( { op, result } );
    return result;
  }
};

//-------------------------------------------------------------------------
// emulates the microcode, assigning random values to unknown variables
// (but keeping them consistent across executions)
struct mcode_emu_rand_vals_t : public int64_emulator_t
{
  byte_val_map_t var_vals;

  intval64_t get_mop_value(const mop_t &mop) override
  {
    // check that the mop is indeed a variable
    mopt_t t = mop.t;
    QASSERT(30672, t == mop_r || t == mop_S || t == mop_v || t == mop_l);

    intval64_t v = var_vals.lookup(mop);
    // msg("mop: %s, mcode_val: %s\n", mop.dstr(), v.dstr());
    return v;
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