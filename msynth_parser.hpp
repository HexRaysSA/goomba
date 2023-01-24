/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <hexrays.hpp>
#include "linear_exprs.hpp"

//-------------------------------------------------------------------------
struct bin_op_t
{
  const char *text;
  mcode_t opcode;
};

static const bin_op_t bin_ops[] =
{
  { "+",  m_add  }, // "-" is handled separately since it can also be unary
  { "*",  m_mul  },
  { "/",  m_udiv },
  { "&",  m_and  },
  { "|",  m_or   },
  { "^",  m_xor  },
  { "<<", m_shl  },
};

//-------------------------------------------------------------------------
inline mcode_t get_binop(const char *op)
{
  for ( size_t i=0; i < qnumber(bin_ops); i++ )
    if ( streq(bin_ops[i].text, op) )
      return bin_ops[i].opcode;
  return m_nop;
}

//-------------------------------------------------------------------------
class msynth_expr_parser_t
{
public:
  const char *next;
  const mopvec_t &vars;


  //-------------------------------------------------------------------------
  void init_from_arg(mop_t *op, minsn_t **pp_ins)
  {
    minsn_t *ins = *pp_ins;
    op->create_from_insn(ins);
    delete ins;
    *pp_ins = nullptr;
  }

  //-------------------------------------------------------------------------
  minsn_t *make_un(mcode_t opcode, minsnptrs_t *args)
  {
    QASSERT(30683, args->size() == 1);
    minsn_t *res = new minsn_t(0);
    res->opcode = opcode;
    init_from_arg(&res->l, args->begin() + 0);
    res->d.size = res->l.size;
    return res;
  }

  //-------------------------------------------------------------------------
  minsn_t *make_bin(mcode_t opcode, minsnptrs_t *args)
  {
    QASSERT(30684, args->size() == 2);
    minsn_t *res = new minsn_t(0);
    res->opcode = opcode;
    init_from_arg(&res->l, args->begin() + 0);
    init_from_arg(&res->r, args->begin() + 1);
    if ( opcode == m_shl && res->r.size != 1 )
      res->r.change_size(1);
    res->d.size = res->l.size;
    return res;
  }

  //-------------------------------------------------------------------------
  minsn_t *make_slice(minsn_t *src, int lo, int hi)
  {
    QASSERT(30686, lo == 0);
    QASSERT(30687, hi == 8 || hi == 16 || hi == 32);

    minsn_t *res = new minsn_t(0);
    res->opcode = m_low;
    res->l.create_from_insn(src);
    res->d.size = hi / 8;
    return res;
  }

  minsn_t *parse_next_expr();

public:
  msynth_expr_parser_t(const char *s, const mopvec_t &v) : next(s), vars(v) {}
};
