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
#include "consts.hpp"

//-------------------------------------------------------------------------
struct default_mops_t
{
  mopvec_t mops;

  static default_mops_t *get_instance()
  {
    if ( instance == nullptr )
      instance = new default_mops_t();
    return instance;
  }

private:
  static default_mops_t *instance;
  default_mops_t()
  {
    for ( int i = 0; i < CANDIDATE_EXPR_NUMINPUTS; i++ )
    {
      mop_t new_var;
      new_var.t = mop_l;
      new_var.l = new lvar_ref_t(nullptr, i);
      new_var.size = 8;
      mops.push_back(new_var);
    }
  }
};

//-------------------------------------------------------------------------
// a minsn template has no defined size or assigned terminal mops
class minsn_template_t
{
public:
  // caller is responsible for freeing the minsn_t *
  virtual minsn_t *synthesize(ea_t ea, int size, const qvector<mop_t> &mops) const = 0;
  virtual ~minsn_template_t() {}

  const char *dstr() const
  {
    minsn_t *insn = synthesize(0, 8, default_mops_t::get_instance()->mops);
    const char *res = insn->dstr();
    delete insn;
    return res;
  }
};

typedef std::shared_ptr<minsn_template_t> minsn_template_ptr_t;
typedef qvector<minsn_template_ptr_t> minsn_templates_t;

//-------------------------------------------------------------------------
struct mt_constant_t : public minsn_template_t
{
  uint64_t val;

  mt_constant_t(uint64_t v) : val(v) {}
  minsn_t *synthesize(ea_t ea, int size, const qvector<mop_t>&) const override
  {
    minsn_t *res = new minsn_t(ea);
    res->opcode = m_ldc;
    res->l.make_number(val, size, ea);
    res->r.zero();
    res->d.size = size;
    return res;
  }
};

//-------------------------------------------------------------------------
struct mt_varref_t : public minsn_template_t
{
  int var_idx;

  mt_varref_t(int v) : var_idx(v) {}
  minsn_t *synthesize(ea_t ea, int size, const qvector<mop_t> &mops) const override
  {
    QASSERT(30704, var_idx < mops.size());
    return resize_mop(ea, mops[var_idx], size, false);
  }
};

//-------------------------------------------------------------------------
struct mt_comp_t : public minsn_template_t
{
  mcode_t opc;
  minsn_templates_t operands;

  mt_comp_t(mcode_t op, minsn_templates_t opr) : opc(op), operands(opr) {}

  minsn_t *synthesize(ea_t ea, int size, const qvector<mop_t> &mops) const override
  {
    minsn_t *res = new minsn_t(ea);
    res->opcode = opc;
    res->l.zero();
    res->r.zero();

    if ( operands.size() >= 1 )
    {
      minsn_t *l = operands[0]->synthesize(ea, size, mops);
      res->l.create_from_insn(l);
      delete l;
    }
    if ( operands.size() >= 2 )
    {
      minsn_t *r = operands[1]->synthesize(ea, size, mops);
      res->r.create_from_insn(r);
      delete r;
    }

    res->d.size = size;
    return res;
  }
};

inline minsn_template_ptr_t make_un(mcode_t opc, minsn_template_ptr_t a)
{
  minsn_templates_t operands;
  operands.push_back(a);
  return std::make_shared<mt_comp_t>(opc, operands);
}

inline minsn_template_ptr_t make_bin(mcode_t opc, minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  minsn_templates_t operands;
  operands.push_back(a);
  operands.push_back(b);
  return std::make_shared<mt_comp_t>(opc, operands);
}

inline minsn_template_ptr_t operator+(minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  return make_bin(m_add, a, b);
}
inline minsn_template_ptr_t operator*(minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  return make_bin(m_mul, a, b);
}
inline minsn_template_ptr_t operator&(minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  return make_bin(m_and, a, b);
}
inline minsn_template_ptr_t operator|(minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  return make_bin(m_or, a, b);
}
inline minsn_template_ptr_t operator^(minsn_template_ptr_t a, minsn_template_ptr_t b)
{
  return make_bin(m_xor, a, b);
}
inline minsn_template_ptr_t operator~(minsn_template_ptr_t a)
{
  return make_un(m_bnot, a);
}
