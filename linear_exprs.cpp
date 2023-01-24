/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include "linear_exprs.hpp"

//-------------------------------------------------------------------------
const char *linear_expr_t::dstr() const
{
  static char buf[MAXSTR];
  char *ptr = buf;
  char *end = buf + sizeof(buf);

  ptr += qsnprintf(ptr, end-ptr, "0x%" FMT_64 "x", const_term.val);
  for ( const auto &term : coeffs )
  {
    if ( term.second.val == 0 )
      continue;
    ptr += qsnprintf(ptr, end-ptr, " + 0x%" FMT_64 "x*", term.second.val);
    if ( term.first.size < const_term.size )
    {
      ptr += qsnprintf(ptr, end-ptr, "%s(%s)",
                       sext.count(term.first) ? "SEXT" : "ZEXT",
                       term.first.dstr());
    }
    else if ( term.first.size > const_term.size )
    {
      ptr += qsnprintf(ptr, end-ptr, "TRUNC(%s)", term.first.dstr());
    }
    else
    {
      APPEND(ptr, end, term.first.dstr());
    }
  }
  return buf;
}

//-------------------------------------------------------------------------
linear_expr_t::linear_expr_t(const minsn_t &insn) // creates a linear expression based on the instruction behavior
{
  default_zero_mcode_emu_t emu;
  const_term = emu.minsn_value(insn); // the value when all variables are assigned to zero

  for ( auto &p : emu.assigned_vals )
  {
    mop_t mop = p.first;
    p.second = mcode_val_t(1, mop.size);
    mcode_val_t coeff = emu.minsn_value(insn) - const_term;

    if ( mop.size < const_term.size )
    {
      // check if a sign extension is necessary
      p.second = mcode_val_t(-1, mop.size);
      mcode_val_t eval = emu.minsn_value(insn); // eval = const + (-1)*coeff if x was sign extended

      if ( const_term - eval == coeff )
        sext.insert(mop);
    }

    coeffs.insert( { mop, emu.minsn_value(insn) - const_term } );
    p.second = mcode_val_t(0, mop.size);
  }
}

//-------------------------------------------------------------------------
mcode_val_t linear_expr_t::evaluate(mcode_emulator_t &emu) const
{
  mcode_val_t res = const_term;

  for ( const auto &term : coeffs )
  {
    const mop_t &mop = term.first;
    const mcode_val_t &coeff = term.second;
    mcode_val_t mop_val = emu.get_var_val(mop);

    // extend the value to 64 bits first
    uint64 ext_val = sext.count(mop) ? mop_val.signed_val() : mop_val.val;

    res = res + coeff * mcode_val_t(ext_val, coeff.size);
  }

  return res;
}

//-------------------------------------------------------------------------
z3::expr linear_expr_t::to_smt(z3_converter_t &cvtr) const
{
  z3::expr res = cvtr.mcode_val_to_expr(const_term);

  for ( const auto &term : coeffs )
  {
    const mop_t &mop = term.first;
    const mcode_val_t &coeff = term.second;
    z3::expr mop_expr = cvtr.mop_to_expr(mop);

    z3::expr ext_expr = cvtr.bv_resize_to_len(mop_expr, const_term.size * 8, sext.count(mop) != 0);

    res = res
        + cvtr.mcode_val_to_expr(coeff) * ext_expr;
  }

  return res;
}

//-------------------------------------------------------------------------
minsn_t *linear_expr_t::to_minsn(ea_t ea) const
{
  minsn_t *res = new minsn_t(ea);
  res->opcode = m_ldc;
  res->l.make_number(const_term.val, const_term.size);
  res->r.zero();
  res->d.size = const_term.size;

  for ( const auto &term : coeffs )
  {
    const mop_t &mop = term.first;
    const mcode_val_t &coeff = term.second;

    if ( coeff.val == 0 )
      continue;

    // mul = coeff * ext(mop)
    minsn_t mul(ea);
    mul.opcode = m_mul;
    mul.l.make_number(coeff.val, coeff.size);
    minsn_t *rsz = resize_mop(ea, mop, const_term.size, sext.count(mop) != 0);
    mul.r.create_from_insn(rsz);
    delete rsz;

    mul.d.size = const_term.size;

    // add = res + mul
    minsn_t *add = new minsn_t(ea);
    add->opcode = m_add;
    add->l.create_from_insn(res);
    add->r.create_from_insn(&mul);
    add->d.size = const_term.size;

    delete res; // mop_t::create_from_insn makes a copy of the insn
    res = add;
  }

  return res;
}
