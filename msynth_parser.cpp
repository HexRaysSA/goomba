/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include "msynth_parser.hpp"
#include "minsn_template.hpp"

default_mops_t *default_mops_t::instance = nullptr;

minsn_t *msynth_expr_parser_t::parse_next_expr()
{
  if ( *next == '~' )
  {
    next++;
    minsn_t *res = new minsn_t(0);
    res->opcode = m_bnot;
    minsn_t *next_expr = parse_next_expr();
    res->l.create_from_insn(next_expr);
    delete next_expr;
    next_expr = nullptr;
    res->d.size = res->l.size;
    return res;
  }

  // ExprInt(val: uint64, bitw: int)
  {
    int nread;
    uint64 val;
    int bitw;
    int sr = qsscanf(next, "ExprInt(%" FMT_64 "u, %d)%n", &val, &bitw, &nread);
    if ( sr == 2 )
    {
      next += nread;

      minsn_t *res = new minsn_t(0);
      res->opcode = m_ldc;
      res->l.make_number(val, bitw/8);
      res->r.zero();
      res->d.size = bitw/8;
      return res;
    }
  }

  // ExprId(id: str, bitw: int)
  {
    int nread;
    int varnum, bitw;
    int sr = qsscanf(next, "ExprId(\"p%d\", %d)%n", &varnum, &bitw, &nread);
    if ( sr == 2 )
    {
      next += nread;
      minsn_t *res = new minsn_t(0);
      res->opcode = bitw == 64 ? m_mov : m_low;
      res->l = vars[varnum];
      res->d.size = bitw/8;
      return res;
    }
  }

  // ExprOp(op: str, expr*)
  {
    int sc = strncmp(next, "ExprOp", 6);
    if ( sc == 0 )
    {
      int nread;
      next += 6;
      char op[3];
      int sr = qsscanf(next, "(\"%2[^\"]\"%n", op, &nread);
      QASSERT(30688, sr == 1);
      next += nread;

      minsnptrs_t args;
      while ( *next != ')' )
      {
        sc = strncmp(next, ", ", 2);
        QASSERT(30689, sc == 0);
        next += 2;

        args.push_back(parse_next_expr());
      }

      next++; // consume the ')'

      // - can be either unary or binary
      if ( streq(op, "-") )
      {
        if ( args.size() == 1 )
          return make_un(m_neg, &args);
        if ( args.size() == 2 )
          return make_bin(m_sub, &args);
        INTERR(30690);
      }
      else
      {
        mcode_t code = get_binop(op);
        if ( code != m_nop )
          return make_bin(code, &args);
      }
      INTERR(30691);
    }
  }

  // ExprSlice(expr, low, hi)
  {
    int sc = strncmp(next, "ExprSlice", 9);
    if ( sc == 0 )
    {
      next += 9;
      QASSERT(30692, *next == '(');
      next++;
      minsn_t *to_slice = parse_next_expr();
      int lo, hi, nread;
      int sr = qsscanf(next, ", %d, %d)%n", &lo, &hi, &nread);
      QASSERT(30693, sr == 2);
      next += nread;
      minsn_t *res = make_slice(to_slice, lo, hi);
      delete to_slice;
      return res;
    }
  }

  INTERR(30694);
}
