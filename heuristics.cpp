/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include "heuristics.hpp"

//-------------------------------------------------------------------------
inline uint64 rand64()
{
  uint32 r1 = rand();
  uint32 r2 = rand();
  return uint64(r1) << 32 | uint64(r2);
}

//-------------------------------------------------------------------------
intval64_t gen_rand_mcode_val(int size)
{
  if ( rand() > SPECIAL_PROBABILITY * RAND_MAX )
  {
    // select from uniform random distribution
    return intval64_t(rand64(), size);
  }
  else
  {
    // select from special cases
    return intval64_t(SPECIAL[rand() % NUM_SPECIAL], size);
  }
}

//-------------------------------------------------------------------------
uint8 gen_rand_byte()
{
  if ( rand() > SPECIAL_PROBABILITY * RAND_MAX )
  {
    // select from uniform random distribution
    return rand() % 256;
  }
  else
  {
    // select from special cases
    return SPECIAL8[rand() % NUM_SPECIAL];
  }
}

//-------------------------------------------------------------------------
// guesses whether or not the instruction is MBA
bool is_mba(const minsn_t &insn)
{
  struct mba_opc_counter_t : public minsn_visitor_t
  {
    int bool_cnt = 0;
    int arith_cnt = 0;
    int idaapi visit_minsn(void) override
    {
      switch ( curins->opcode )
      {
        case m_neg:
        case m_add:
        case m_sub:
        case m_mul:
        case m_udiv:
        case m_sdiv:
        case m_umod:
        case m_smod:
        case m_shl:
        case m_shr:
          arith_cnt++;
          break;
        case m_bnot:
        case m_or:
        case m_and:
        case m_xor:
        case m_sar:
          bool_cnt++;
          break;
        default:
          return 0;
      }
      return bool_cnt >= MIN_MBA_BOOL_OPS && arith_cnt >= MIN_MBA_ARITH_OPS;
    }
  };

  if ( is_mcode_xdsu(insn.opcode) )
    return false; // exclude xdsu, it is better to optimize its operand

  if ( insn.opcode >= m_jcnd )
    return false; // not supported by int64_emulator_t

  if ( insn.d.size > 8 )
    return false; // we only support 64-bit math

  mba_opc_counter_t cntr;
  return CONST_CAST(minsn_t*)(&insn)->for_all_insns(cntr) != 0;
}

//-------------------------------------------------------------------------
// runs a battery of random test cases against both expressions to see if they are equivalent
bool probably_equivalent(const minsn_t &insn, const candidate_expr_t &expr)
{
  for ( int i = 0; i < NUM_TEST_CASES; i++ )
  {
    mcode_emu_rand_vals_t emu;
    intval64_t insn_eval = emu.minsn_value(insn);
    intval64_t expr_eval = expr.evaluate(emu);

    if ( insn_eval != expr_eval )
      return false;
  }

  return true;
}

//-------------------------------------------------------------------------
// runs a battery of random test cases against both expressions to see if they are equivalent
bool probably_equivalent(const minsn_t &a, const minsn_t &b)
{
  for ( int i = 0; i < NUM_TEST_CASES; i++ )
  {
    mcode_emu_rand_vals_t emu;
    intval64_t insn_eval = emu.minsn_value(a);
    intval64_t expr_eval = emu.minsn_value(b);

    if ( insn_eval != expr_eval )
      return false;
  }

  return true;
}

//-------------------------------------------------------------------------
// estimates the "complexity" of a given instruction
int score_complexity(const minsn_t &insn)
{
  struct ida_local complexity_counter_t : public minsn_visitor_t
  {
    int cnt = 0;
    int idaapi visit_minsn() override
    {
      cnt++;
      return 0;
    }
  };
  complexity_counter_t cc;
  CONST_CAST(minsn_t&)(insn).for_all_insns(cc);
  return cc.cnt;
}
