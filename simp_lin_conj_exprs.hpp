/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <memory>
#include <hexrays.hpp>
#include "lin_conj_exprs.hpp"
#include "minsn_template.hpp"
#include "bitwise_expr_lookup_tbl.hpp"

//-------------------------------------------------------------------------
// represents a simplified linear combination of conjunctions,
// essentially just a lin_conj_expr with more bitwise expressions
// other than just conjunctions
class simp_lin_conj_expr_t : public lin_conj_expr_t
{
  minsn_template_ptr_t non_conj_term = std::make_shared<mt_constant_t>(0ull);
  qvector<intval64_t> range; // sorted lowest to highest

  //-------------------------------------------------------------------------
  void recompute_range()
  {
    std::set<intval64_t> new_range;

    for ( const auto &mval : eval_trace )
      new_range.insert(mval);

    range.qclear();
    for ( auto &mval : new_range )
      range.push_back(mval);
  }

  //-------------------------------------------------------------------------
  // returns a bitfield where the i'th bit indicates whether the i'th evaluation
  // returns the value of pos
  uint64 eval_trace_to_bit_trace(const eval_trace_t &src_trace, intval64_t pos)
  {
    QASSERT(30703, src_trace.size() <= 64);

    uint64 res = 0;
    for ( int i = 0; i < src_trace.size(); i++ )
    {
      if ( src_trace[i] == pos )
        res |= (1ull << i);
    }

    return res;
  }

  //-------------------------------------------------------------------------
  bool reset_eval_trace()
  {
    for ( auto &et : eval_trace )
      et.val = 0;
    recompute_coeffs();
    recompute_range();
    return true;
  }

public:
  //-------------------------------------------------------------------------
  simp_lin_conj_expr_t(const lin_conj_expr_t &o) : lin_conj_expr_t(o)
  {
    eliminate_variables();
    recompute_range();
    simplify();
  }

  //-------------------------------------------------------------------------
  const char *dstr() const override
  {
    static char res[MAXSTR];

    minsn_t *ins = non_conj_term->synthesize(0, coeffs[0].size, mops);
    qsnprintf(res, sizeof(res), "%s + %s", lin_conj_expr_t::dstr(), ins->dstr());
    delete ins;
    return res;
  }

  // (1) A constant expression would lead to all variables getting eliminated by eliminate_variables,
  // so there's no need for a simplification step here.

  //-------------------------------------------------------------------------
  // (2) If F has two unique entries and its first entry is zero, we replace the nonzero element a by
  // 1, find the lookup table's entry for the corresponding truth vector and multiply the found
  // expression by a.
  bool simp_2()
  {
    if ( range.size() != 2 )
      return false;
    if ( eval_trace[0].val != 0 )
      return false;

    intval64_t a = range[1];

    uint64 bit_trace = eval_trace_to_bit_trace(eval_trace, a);
    auto minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(a.val) * minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  // (3) If F has two unique entries a and b, both of them are nonzero, w.l.o.g., b = 2a mod 2^n, and
  // F's first entry is a, we can express the result in terms of a negated single expression. We
  // replace all occurences of a by zeros and that of b by ones, find the corresponding expression
  // in the lookup table, negate it, and multiply it by -a.
  bool simp_3()
  {
    if ( range.size() != 2 )
      return false;

    intval64_t a = eval_trace[0];
    intval64_t b = range[0] == a ? range[1] : range[0];

    if ( a * intval64_t(2, b.size) != b )
      return false;

    uint64 bit_trace = eval_trace_to_bit_trace(eval_trace, b);
    auto minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(0-a.val) * ~minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  // (4) If F has two unique entries a and b, but the previous cases do not apply, and F's very first
  // entry is a, we first identify a as the constant term. Then we find an expression with ones
  // exactly where F has the entry b in the lookup table, multiply it by b - a and add the term to
  // the constant.
  bool simp_4()
  {
    if ( range.size() != 2 )
      return false;

    intval64_t a = eval_trace[0];
    intval64_t b = range[0] == a? range[1] : range[0];

    uint64 bit_trace = eval_trace_to_bit_trace(eval_trace, b);
    auto minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(a.val)
                  + std::make_shared<mt_constant_t>((b-a).val) * minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  // (5) If F has two unique nonzero entries a and b and its first one is zero, we split it into two vectors
  // with ones where F has entries a or b, resp., find the corresponding expressions in the lookup
  // table, multiply them by a and b, resp., and add the terms together.
  bool simp_5()
  {
    if ( range.size() != 3 )
      return false;
    if ( eval_trace[0].val != 0ull )
      return false;

    intval64_t a = range[1];
    intval64_t b = range[2];

    uint64 a_bit_trace = eval_trace_to_bit_trace(eval_trace, a);
    uint64 b_bit_trace = eval_trace_to_bit_trace(eval_trace, b);
    auto a_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), a_bit_trace);
    auto b_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), b_bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(a.val) * a_minsn_template
                  + std::make_shared<mt_constant_t>(b.val) * b_minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  // (6) If F has three unique nonzero entries a, b and c and its first one is 0, we try to express one
  // of them as a sum of the others modulo 2n, e.g., a = b + c. In that case we split F into two
  // vectors with ones where F has entries b or c, resp., or a, find the corresponding expressions in
  // the lookup table, multiply them by b and c, resp., and add the terms together.
  bool simp_6()
  {
    if ( range.size() != 4 )
      return false;
    if ( eval_trace[0].val != 0ull )
      return false;

    intval64_t a = range[1];
    intval64_t b = range[2];
    intval64_t c = range[3];

    // make sure that a = b + c
    if ( b == a + c )
      qswap(a, b);
    else if ( c == a + b )
      qswap(a, c);
    else if ( a != b + c )
      return false;

    QASSERT(30705, a == b + c);

    uint64 a_bit_trace = eval_trace_to_bit_trace(eval_trace, a);
    uint64 b_bit_trace = eval_trace_to_bit_trace(eval_trace, b);
    uint64 c_bit_trace = eval_trace_to_bit_trace(eval_trace, c);
    auto ab_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), a_bit_trace | b_bit_trace);
    auto ac_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), a_bit_trace | c_bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(b.val) * ab_minsn_template
                  + std::make_shared<mt_constant_t>(c.val) * ac_minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  // (7) If F has three unique nonzero entries a, b and c, its first one is 0 and the previous case does
  // not apply, we split it into three vectors with ones where F has entries a, b or c, resp., find the
  // corresponding expressions in the lookup table, multiply them by a, b and c, resp., and add the
  // terms together.
  bool simp_7()
  {
    if ( range.size() != 4 )
      return false;
    if ( eval_trace[0].val != 0ull )
      return false;

    intval64_t a = range[1];
    intval64_t b = range[2];
    intval64_t c = range[3];

    uint64 a_bit_trace = eval_trace_to_bit_trace(eval_trace, a);
    uint64 b_bit_trace = eval_trace_to_bit_trace(eval_trace, b);
    uint64 c_bit_trace = eval_trace_to_bit_trace(eval_trace, c);
    auto a_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), a_bit_trace);
    auto b_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), b_bit_trace);
    auto c_minsn_template = bw_expr_tbl_t::instance.lookup(mops.size(), c_bit_trace);

    non_conj_term = non_conj_term
                  + std::make_shared<mt_constant_t>(a.val) * a_minsn_template
                  + std::make_shared<mt_constant_t>(b.val) * b_minsn_template
                  + std::make_shared<mt_constant_t>(c.val) * c_minsn_template;

    return reset_eval_trace();
  }

  //-------------------------------------------------------------------------
  bool simp_8()
  {
    if ( range.size() != 4 )
      return false;
    if ( eval_trace[0].val == 0ull )
      return false;

    intval64_t a = eval_trace[0];

    non_conj_term = non_conj_term + std::make_shared<mt_constant_t>(a.val);

    for ( int i = 0; i < eval_trace.size(); i++ )
      eval_trace[i] = eval_trace[i] - a;
    recompute_coeffs();
    recompute_range();
    return simplify(); // start again
  }

  //-------------------------------------------------------------------------
  bool simplify()
  {
    if ( mops.size() < 1 || mops.size() > 3 )
      return false;
    if ( simp_2() )
      return true;
    if ( simp_3() )
      return true;
    if ( simp_4() )
      return true;
    if ( simp_5() )
      return true;
    if ( simp_6() )
      return true;
    if ( simp_7() )
      return true;
    if ( simp_8() )
      return true;
    return false;
  }

  //-------------------------------------------------------------------------
  minsn_t *to_minsn(ea_t ea) const override
  {
    minsn_t *res = new minsn_t(ea);
    minsn_t *l = lin_conj_expr_t::to_minsn(ea);
    minsn_t *r = non_conj_term->synthesize(ea, coeffs[0].size, mops);

    res->opcode = m_add;
    res->l.create_from_insn(l);
    res->r.create_from_insn(r);
    res->d.size = coeffs[0].size;

    delete l;
    delete r;
    return res;
  }
};
