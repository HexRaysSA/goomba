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

typedef qvector<intval64_t> coeff_vector_t;
typedef qvector<intval64_t> eval_trace_t;
const int LIN_CONJ_MAX_VARS = 16;

// represents a linear combination of conjunctions
class lin_conj_expr_t : public candidate_expr_t
{
protected:
  mopvec_t mops;
  coeff_vector_t coeffs;
  eval_trace_t eval_trace;

public:
  //-------------------------------------------------------------------------
  const char *dstr() const override
  {
    static char buf[MAXSTR];
    char *ptr = buf;
    char *end = buf + sizeof(buf);

    ptr += qsnprintf(ptr, end-ptr, "0x%" FMT_64 "x", coeffs[0].val);
    for ( uint32 i = 1; i < coeffs.size(); i++ )
    {
      if ( coeffs[i].val == 0 )
        continue;
      ptr += qsnprintf(ptr, end-ptr, " + 0x%" FMT_64 "x(", coeffs[i].val);
      ptr = print_assignment(ptr, end, i);
      APPEND(ptr, end, ")");
    }
    return buf;
  }

  //-------------------------------------------------------------------------
  // each boolean assignment is represented as a uint32, where the nth bit
  // represents the 0/1 value of the corresponding variable
  char *print_assignment(char *ptr, char *end, uint32 assn) const
  {
    bool first_printed = false;
    for ( int i = 0; i < mops.size(); i++ )
    {
      if ( ((assn >> i) & 1) != 0 )
      {
        if ( first_printed )
          APPCHAR(ptr, end, '&');
        APPEND(ptr, end, mops[i].dstr());
        first_printed = true;
      }
    }
    return ptr;
  }

  //-------------------------------------------------------------------------
  // each boolean assignment is represented as a uint32, where the nth bit
  // represents the 0/1 value of the corresponding variable
  void apply_assignment(uint32 assn, std::map<const mop_t, intval64_t> &dest)
  {
    // recall std::map keeps keys in sorted order
    int curr_idx = 0;
    for ( auto &kv : dest )
    {
      kv.second.val = (assn >> curr_idx) & 1;
      curr_idx++;
    }
  }

  //-------------------------------------------------------------------------
  // the i'th index in output_vals contains the output value corresponding to
  // the i'th assignment, where the i'th assignment is defined as in
  // apply_assignment.
  // the return value of this function is the corresponding coefficients in
  // the linear combination of conjunctions that would yield the output
  // behavior. The coefficients are ordered based on the same indexing pattern.
  void compute_coeffs(coeff_vector_t &dest, const qvector<intval64_t> &output_vals)
  {
    dest = coeff_vector_t();
    dest.reserve(output_vals.size());
    dest.push_back(output_vals[0]); // the zero coeff = the zero assignment

    // we can think of the problem as solving the linear equation Ax = y,
    // where y is the output_vals and x is the desired coefficient set.
    // A is defined as the binary matrix where row numbers represent
    // assignments and columns represent conjunctions. See the SiMBA paper
    // for more details.
    // We do an additional simplification, noting that
    // A_{ij} = (i & j) == j. Also, we use forward substitution since A is a
    // lower-triangular matrix.

    for ( uint32 i = 1; i < output_vals.size(); i++ )
    {
      intval64_t curr_coeff = output_vals[i];
      for ( uint32 j = 0; j < i; j++ )
      {
        if ( (i & j) == j )
          curr_coeff = curr_coeff - dest[j];
      }
      dest.push_back(curr_coeff);
    }
  }

  //-------------------------------------------------------------------------
  void recompute_coeffs()
  {
    compute_coeffs(coeffs, eval_trace);
  }

  //-------------------------------------------------------------------------
  intval64_t evaluate(int64_emulator_t &emu) const override
  {
    minsn_t *minsn = to_minsn(0);
    intval64_t res = emu.minsn_value(*minsn);
    delete minsn;
    return res;
  }

  //-------------------------------------------------------------------------
  // eliminates all variables that are not needed in the expression
  void eliminate_variables()
  {
    for ( int i = 0; i < mops.size(); i++ )
    {
      if ( can_eliminate_variable(i) )
      {
        eliminate_variable(i);
        i--; // the mop at mop[i] no longer exists
      }
    }
  }

  //-------------------------------------------------------------------------
  // creates a linear combination of conjunctions based on the minsn behavior
  lin_conj_expr_t(const minsn_t &insn)
  {
    default_zero_mcode_emu_t emu;
    intval64_t const_term = emu.minsn_value(insn);     // first-time emulation returns the result when setting all inputs as 0

    int nvars = emu.assigned_vals.size();
    if ( nvars > LIN_CONJ_MAX_VARS )
      throw "lin_conj_expr_t: too many input variables";

    uint32 max_assignment = 1 << nvars;       // 2^n possible values in the truth table
    // we have already gotten the value for the all-zeroes assignment, which is const_term
    eval_trace.push_back(const_term);
    eval_trace.reserve(max_assignment);

    // Compute signature vectors
    for ( uint32 assn = 1; assn < max_assignment; assn++ )
    {
      apply_assignment(assn, emu.assigned_vals);
      intval64_t output_val = emu.minsn_value(insn);

      eval_trace.push_back(output_val);
    }
    compute_coeffs(coeffs, eval_trace);

    // Collect all the input operands from the emulator
    mops.reserve(emu.assigned_vals.size());
    for ( const auto &kv : emu.assigned_vals )
      mops.push_back(kv.first);

    QASSERT(30679, coeffs.size() == (1ull << mops.size()));
  }

  //-------------------------------------------------------------------------
  z3::expr to_smt(z3_converter_t &cvtr) const override
  {
    minsn_t *minsn = to_minsn(0);
    z3::expr res = cvtr.minsn_to_expr(*minsn);
    delete minsn;
    return res;
  }

  //-------------------------------------------------------------------------
  // converts an assignment to the corresponding conjunction. e.g.
  // 0b1101 => x0 & x2 & x3
  minsn_t *assn_to_minsn(uint32 assn, int size, ea_t ea) const
  {
    QASSERT(30680, assn != 0);
    minsn_t *res = nullptr;

    for ( int i = 0; i < mops.size(); i++ )
    {
      if ( ((assn >> i) & 1) != 0 )
      {
        if ( res == nullptr )
        {
          res = resize_mop(ea, mops[i], size, false);
        }
        else
        {
          minsn_t *new_res = new minsn_t(ea);
          new_res->opcode = m_and;
          new_res->l.create_from_insn(res);
          minsn_t *rsz = resize_mop(ea, mops[i], size, false);
          new_res->r.create_from_insn(rsz);
          delete rsz;
          new_res->d.size = size;

          delete res;
          res = new_res;
        }
      }
    }

    QASSERT(30681, res->opcode != m_ldc);

    return res;
  }

  //-------------------------------------------------------------------------
  minsn_t *to_minsn(ea_t ea) const override
  {
    minsn_t *res = new minsn_t(ea);
    res->opcode = m_ldc;
    res->l.make_number(coeffs[0].val, coeffs[0].size, ea);
    res->r.zero();
    res->d.size = coeffs[0].size;

    for ( uint32 assn = 1; assn < coeffs.size(); assn++ )
    {
      auto coeff = coeffs[assn];
      if ( coeff.val == 0 )
        continue;

      // mul = coeff * F(mops)
      minsn_t mul(ea);
      mul.opcode = m_mul;
      mul.l.make_number(coeff.val, coeff.size);
      minsn_t *F = assn_to_minsn(assn, coeff.size, ea);
      mul.r.create_from_insn(F);
      delete F;
      mul.d.size = coeff.size;

      // add = res + mul
      minsn_t *add = new minsn_t(ea);
      add->opcode = m_add;
      add->l.create_from_insn(res);
      add->r.create_from_insn(&mul);
      add->d.size = coeff.size;

      delete res; // mop_t::create_from_insn makes a copy of the insn
      res = add;
    }

    return res;
  }

private:
  //-------------------------------------------------------------------------
  // returns true if the variable can be eliminated safely
  // i.e. all terms containing it have coeff = 0
  bool can_eliminate_variable(int idx)
  {
    for ( uint32 assn = 0; assn < coeffs.size(); assn++ )
    {
      if ( ((assn >> idx) & 1) != 0 && coeffs[assn].val != 0 )
        return false;
    }
    return true;
  }

  //-------------------------------------------------------------------------
  // removes the variable from the expression
  // make sure to check can_eliminate_variable before calling
  void eliminate_variable(int idx)
  {
    coeff_vector_t new_coeffs;
    eval_trace_t new_evals;
    new_coeffs.reserve(coeffs.size() / 2);
    new_evals.reserve(coeffs.size() / 2);
    for ( uint32 assn = 0; assn < coeffs.size(); assn++ )
    {
      if ( ((assn >> idx) & 1) == 0 )
      {
        new_coeffs.push_back(coeffs[assn]);
        new_evals.push_back(eval_trace[assn]);
      }
      else
      {
        QASSERT(30682, coeffs[assn].val == 0);
      }
    }
    coeffs = new_coeffs;
    eval_trace = new_evals;
    mops.erase(mops.begin() + idx);
  }
};
