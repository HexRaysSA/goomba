/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include "z3++_no_warn.h"
#include "mcode_emu.hpp"

//-------------------------------------------------------------------------
class z3_converter_t
{
  char namebuf[12];
  int next_free_varnum = 0;
  const char *build_new_varname()
  {
    qsnprintf(namebuf, sizeof(namebuf), "y%d", next_free_varnum++);
    return namebuf;
  }

public:
  z3::context context;
  z3::expr_vector input_vars;

  // the next integer we can use to generate a z3 variable name
  std::map<mop_t, z3::expr> assigned_vars;

  z3_converter_t() : input_vars(context) { namebuf[0] = '\0'; }
  virtual ~z3_converter_t() {}

  // create_new_z3_var is called when var_to_expr fails to find an assigned_var in the cache
  virtual z3::expr create_new_z3_var(const mop_t &mop);
  z3::expr var_to_expr(const mop_t &mop); // for terminal mops, i.e. stack vars, registers, global vars
  z3::expr mop_to_expr(const mop_t &mop);
  z3::expr minsn_to_expr(const minsn_t &insn);

  //-------------------------------------------------------------------------
  z3::expr bool_to_bv(z3::expr boolean, uint bitsz)
  {
    return z3::ite(boolean, context.bv_val(1, bitsz), context.bv_val(0, bitsz));
  }

  //-------------------------------------------------------------------------
  z3::expr bv_zext_to_len(z3::expr bv, uint target_bitsz)
  {
    uint orig_bitsz = bv.get_sort().bv_size();
    if ( target_bitsz == orig_bitsz )
      return bv; // no need to extend
    return z3::zext(bv, target_bitsz - orig_bitsz);
  }

  //-------------------------------------------------------------------------
  z3::expr bv_sext_to_len(z3::expr bv, uint target_bitsz)
  {
    uint orig_bitsz = bv.get_sort().bv_size();
    if ( target_bitsz == orig_bitsz )
      return bv; // no need to extend
    return z3::sext(bv, target_bitsz - orig_bitsz);
  }

  //-------------------------------------------------------------------------
  z3::expr bv_resize_to_len(z3::expr bv, uint target_bitsz, bool sext)
  {
    uint orig_bitsz = bv.get_sort().bv_size();
    if ( target_bitsz == orig_bitsz )
      return bv;
    if ( target_bitsz < orig_bitsz )
      return bv.extract(target_bitsz - 1, 0);
    else
      return sext
           ? bv_sext_to_len(bv, target_bitsz)
           : bv_zext_to_len(bv, target_bitsz);
  }

  //-------------------------------------------------------------------------
  z3::expr mcode_val_to_expr(mcode_val_t v)
  {
    return context.bv_val(uint64_t(v.val), v.size * 8);
  }
};
