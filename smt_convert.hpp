/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include "z3++_no_warn.h"
#include <hexrays.hpp>

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

  // These maps store register and memory variables as 8-bit bitvectors.
  // Similarly as the byte_val_map_t in heuristics.hpp, it is for solving the
  // overlapping problem.
  std::map<const uval_t, z3::expr> stk_map;      // stack variable mapping
  std::map<const uval_t, z3::expr> glb_map;      // global variable mapping
  std::map<const uval_t, z3::expr> local_map;    // local variable mapping
  std::map<const uval_t, z3::expr> reg_map;      // register mapping

  std::map<const mop_t, z3::expr> cache;

  z3_converter_t() { namebuf[0] = '\0'; }
  virtual ~z3_converter_t() {}

  // create_new_z3_var is called when var_to_expr fails to find an assigned_var in the cache
  virtual z3::expr create_new_z3_var(const mop_t &mop);
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
  z3::expr intval64_to_expr(intval64_t v)
  {
    return context.bv_val(uint64_t(v.val), v.size * 8);
  }

  //-------------------------------------------------------------------------
  // find the value of the bytes [off, off+size) from the associated map,
  // assemble the result as a bitvector. Create a new bv_const for new values
  // and update the map.
  z3::expr find_update(uval_t off, size_t size, std::map<const uval_t, z3::expr> &map)
  {
    z3::expr_vector byte_exprs(context);

    for ( int i = size - 1; i >= 0; --i )
    {                                 // iterate the addresses from high to low, so the most
      uval_t mem_addr = off + i;      // significant digit is the first element of the expr_vector
      auto result = map.find(mem_addr);
      if ( result != map.end() )
      {
        byte_exprs.push_back(result->second);
      }
      else
      {
        // create a new byte variable
        const char *name = build_new_varname();
        z3::expr new_byte_var = context.bv_const(name, 8);
        byte_exprs.push_back(new_byte_var);
        map.insert( { mem_addr, new_byte_var } );
      }
    }

    return z3::concat(byte_exprs);
  }

  //-------------------------------------------------------------------------
  z3::expr lookup(const mop_t &op)
  {
    auto it = cache.find(op);
    if ( it != cache.end() )
      return it->second;

    z3::expr result(context);
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
        INTERR(30821);
    }

    cache.insert( { op, result } );
    return result;
  }

};
