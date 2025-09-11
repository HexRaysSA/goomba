/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <hexrays.hpp>

#include "smt_convert.hpp"

//-------------------------------------------------------------------------
class candidate_expr_t
{
public:
  virtual ~candidate_expr_t() {}
  virtual intval64_t evaluate(int64_emulator_t &emu) const = 0;
  virtual z3::expr to_smt(z3_converter_t &converter) const = 0;
  virtual minsn_t *to_minsn(ea_t ea) const = 0;
  virtual const char *dstr() const = 0;
};

//-------------------------------------------------------------------------
// resize_mop generates a minsn that resizes the source operand (truncates or extends)
inline minsn_t *resize_mop(ea_t ea, const mop_t &mop, int dest_sz, bool sext)
{
  minsn_t *res = new minsn_t(ea);
  if ( dest_sz == mop.size )
    res->opcode = m_mov;
  else if ( dest_sz < mop.size )
    res->opcode = m_low;
  else
    res->opcode = sext ? m_xds : m_xdu;

  res->l = mop;
  res->d.size = dest_sz;
  return res;
}

//-------------------------------------------------------------------------
// this emulator automatically assigns variables to 0
// after the first run, the assigned_vals field can be modified
// and the emulation can be rerun to obtain coefficients
class default_zero_mcode_emu_t : public int64_emulator_t
{
public:
  std::map<const mop_t, intval64_t> assigned_vals;

  intval64_t get_mop_value(const mop_t &mop) override
  {
    // check that the mop is indeed a variable
    mopt_t t = mop.t;
    QASSERT(30695, t == mop_r || t == mop_S || t == mop_v || t == mop_l);

    auto p = assigned_vals.find(mop);
    if ( p != assigned_vals.end() )
      return p->second;

    intval64_t new_val = intval64_t(0, mop.size);
    assigned_vals.insert( { mop, new_val } );
    return new_val;
  }
};

//-------------------------------------------------------------------------
class linear_expr_t : public candidate_expr_t
{
public:
  intval64_t const_term { 0, 1 };
  std::map<mop_t, intval64_t> coeffs;
  std::set<mop_t> sext;

  const char *dstr() const override;
  linear_expr_t(const minsn_t &insn);
  intval64_t evaluate(int64_emulator_t &emu) const override;
  z3::expr to_smt(z3_converter_t &cvtr) const override;
  minsn_t *to_minsn(ea_t ea) const override;
};
