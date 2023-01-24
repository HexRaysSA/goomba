/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include "smt_convert.hpp"

//--------------------------------------------------------------------------
z3::expr z3_converter_t::create_new_z3_var(const mop_t &mop)
{
  const char *name = build_new_varname();
  return context.bv_const(name, mop.size * 8);
}

//--------------------------------------------------------------------------
z3::expr z3_converter_t::var_to_expr(const mop_t &mop)
{
  if ( assigned_vars.count(mop) )
    return assigned_vars.at(mop);

  // mop has not yet been assigned a z3 var, make one now
  z3::expr new_var = create_new_z3_var(mop);
  input_vars.push_back(new_var);
  assigned_vars.insert( { mop, new_var } );
  return new_var;
}

//--------------------------------------------------------------------------
z3::expr z3_converter_t::mop_to_expr(const mop_t &mop)
{
  switch ( mop.t )
  {
    case mop_n: // immediate value
      {
        int bytesz = mop.size;
        uint64_t value = mop.nnn->value;
        return context.bv_val(value, bytesz * 8); // z3 counts size in bits
      }

    case mop_d: // result of another instruction
      return minsn_to_expr(*mop.d);

    case mop_r: // register
    case mop_S: // stack variable
    case mop_v: // global variable
      {
        auto p = assigned_vars.find(mop);
        if ( p != assigned_vars.end() )
          return p->second;

        // mop has not yet been assigned a z3 var, make one now
        const char *name = build_new_varname();
        z3::expr new_var = context.bv_const(name, mop.size * 8);
        input_vars.push_back(new_var);
        assigned_vars.insert( { mop, new_var } );
        return new_var;
      }
    default:
      INTERR(30696); // it is better to check this before running z3, when detecting mba
  }
}

//--------------------------------------------------------------------------
z3::expr z3_converter_t::minsn_to_expr(const minsn_t &insn)
{
  switch ( insn.opcode )
  {
    case m_ldc: // load constant
    case m_mov: // move
      return mop_to_expr(insn.l);
    case m_neg:
      return -mop_to_expr(insn.l);
    case m_lnot:
      {
        int bitsz = insn.l.size * 8;
        z3::expr bool_res = mop_to_expr(insn.l) == context.bv_val(0, bitsz);
        // !x === (x == 0)
        return bool_to_bv(bool_res, bitsz);
      }
    case m_bnot:
      return ~mop_to_expr(insn.l);
    case m_xds: // signed extension
    case m_xdu: // unsigned (zero) extension
      {
        auto e = mop_to_expr(insn.l);
        int orig_bitsz = e.get_sort().bv_size();
        int dest_bitsz = insn.d.size * 8;
        QASSERT(30674, dest_bitsz >= orig_bitsz);
        if ( insn.opcode == m_xdu )
          return z3::zext(e, dest_bitsz - orig_bitsz);
        else
          return z3::sext(e, dest_bitsz - orig_bitsz);
      }
    case m_low:
      {
        auto dest_bitsz = insn.d.size * 8;
        return mop_to_expr(insn.l).extract(dest_bitsz - 1, 0);
      }
    case m_high:
      {
        auto src_bitsz = insn.l.size * 8;
        auto dest_bitsz = insn.d.size * 8;
        return mop_to_expr(insn.l).extract(src_bitsz - 1, src_bitsz - dest_bitsz);
      }
    case m_add:
      return mop_to_expr(insn.l) + mop_to_expr(insn.r);
    case m_sub:
      return mop_to_expr(insn.l) - mop_to_expr(insn.r);
    case m_mul:
      return mop_to_expr(insn.l) * mop_to_expr(insn.r);
    case m_udiv:
      return z3::udiv(mop_to_expr(insn.l), mop_to_expr(insn.r));
    case m_sdiv:
      return mop_to_expr(insn.l) / mop_to_expr(insn.r);
    case m_umod:
      return mop_to_expr(insn.l) % mop_to_expr(insn.r);
    case m_smod:
      return z3::smod(mop_to_expr(insn.l), mop_to_expr(insn.r));
    case m_or:
      return mop_to_expr(insn.l) | mop_to_expr(insn.r);
    case m_and:
      return mop_to_expr(insn.l) & mop_to_expr(insn.r);
    case m_xor:
      return mop_to_expr(insn.l) ^ mop_to_expr(insn.r);
    case m_shl:
      return z3::shl(
        mop_to_expr(insn.l),
        bv_zext_to_len(mop_to_expr(insn.r), insn.l.size * 8));
    case m_shr:
      return z3::lshr(
        mop_to_expr(insn.l),
        bv_zext_to_len(mop_to_expr(insn.r), insn.l.size * 8));
    case m_sar:
      return z3::ashr(
        mop_to_expr(insn.l),
        bv_zext_to_len(mop_to_expr(insn.r), insn.l.size * 8));
    case m_sets: // get sign bit of expression
      return bool_to_bv(mop_to_expr(insn.l) < 0, insn.d.size * 8);
    // TODO: m_seto, m_setp
    case m_setnz:
      return bool_to_bv(mop_to_expr(insn.l) != mop_to_expr(insn.r), insn.d.size * 8);
    case m_setz:
      return bool_to_bv(mop_to_expr(insn.l) == mop_to_expr(insn.r), insn.d.size * 8);
    case m_setae:
      return bool_to_bv(z3::uge(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setb:
      return bool_to_bv(z3::ult(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_seta:
      return bool_to_bv(z3::ugt(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setbe:
      return bool_to_bv(z3::ule(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setg:
      return bool_to_bv(z3::sgt(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setge:
      return bool_to_bv(z3::sge(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setl:
      return bool_to_bv(z3::slt(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    case m_setle:
      return bool_to_bv(z3::sle(mop_to_expr(insn.l), mop_to_expr(insn.r)), insn.d.size * 8);
    default:
      INTERR(30697); // it is better to check this before running z3, when detecting mba
  }
}
