/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include <chrono>

#include "z3++_no_warn.h"
#include "optimizer.hpp"

//--------------------------------------------------------------------------
// check whether or not we should skip the proving step of optimization
inline bool skip_proofs()
{
  return qgetenv("VD_MBA_SKIP_PROOFS");
}

//--------------------------------------------------------------------------
inline void set_cmt(ea_t ea, const char *cmt)
{
  func_t *pfn = get_func(ea);
  set_func_cmt(pfn, cmt, false);
}

//--------------------------------------------------------------------------
static bool check_and_substitute(
        minsn_t *insn,
        minsn_t *cand_insn,
        uint z3_timeout,
        bool z3_assume_timeouts_correct)
{
  bool ok = false;
  int original_score = score_complexity(*insn);
  int candidate_score = score_complexity(*cand_insn);
  msg("goomba: testing candidate: %s\n", cand_insn->dstr());
  if ( candidate_score > original_score )
  {
    msg("goomba: candidate (%d) is not simpler than original (%d), skipping\n", candidate_score, original_score);
  }
  else
  {
    z3_converter_t converter;
    if ( probably_equivalent(*insn, *cand_insn) )
    {
      msg("goomba: instruction is probably equivalent to candidate\n");
      if ( skip_proofs() || z3_timeout == 0 )
      {
        set_cmt(insn->ea, "goomba: z3 proof skipped, simplification assumed correct");
        ok = true;
      }
      else
      {
        z3::expr lge = converter.minsn_to_expr(*cand_insn);
        z3::expr ie = converter.minsn_to_expr(*insn);
        // msg("lge: %s\n", lge.to_string().c_str());
        // msg("ie: %s\n", ie.to_string().c_str());
        z3::solver s(converter.context);
        s.set("timeout", z3_timeout);
        s.add(lge != ie);
        z3::check_result res = s.check();
        msg("goomba: SMT check result: %d\n", res);
        if ( res == z3::check_result::sat )
        {
          msg("Satisfiable. Counterexample: \n");
          z3::model m = s.get_model();
          for ( unsigned i = 0; i < m.size(); i++ )
          {
            z3::func_decl v = m[i];
            msg("%s = %s\n", v.name().str().c_str(), m.get_const_interp(v).to_string().c_str());
          }
        }

        if ( res == z3::check_result::unsat )
        {
          ok = true;
        }

        if ( z3_assume_timeouts_correct && res == z3::check_result::unknown )
        {
          bool add_cmt = true;
#ifdef TESTABLE_BUILD
          // when running the testable build, do not append comments about z3 timeouts
          if ( add_cmt )
          {
            qstring dummy;
            if ( qgetenv("IDA_TEST_NAME", &dummy) )
              add_cmt = false;
          }
#endif
          if ( add_cmt )
            set_cmt(insn->ea, "goomba: z3 proof timed out, simplification assumed correct");
          ok = true;
        }
      }
    }
    else
    {
      msg("goomba: candidate not equivalent, skipping\n");
    }
  }

  if ( ok )
  {
    msg("goomba: SUCCESS: %s\n", cand_insn->dstr());
    substitute(insn, cand_insn);
  }
  return ok;
}

//--------------------------------------------------------------------------
bool optimizer_t::optimize_insn_recurse(minsn_t *insn)
{
  if ( optimize_insn(insn) )
    return true;

  // if unable to optimize insn, try to optimize all of its mops
  struct optimizer_visitor_t : public mop_visitor_t
  {
    optimizer_t *opt;
    optimizer_visitor_t(optimizer_t *o) : opt(o) {}
    bool result = false;

    int idaapi visit_mop(mop_t *op, const tinfo_t *, bool)
    {
      if ( op->is_insn() )
      {
        result |= opt->optimize_insn(op->d);
      }
      return 0;
    }
  };

  optimizer_visitor_t opt_mop(this);
  insn->for_all_ops(opt_mop);

  return opt_mop.result;
}

//--------------------------------------------------------------------------
static void add_candidate(minsnptrs_t *out, minsn_t *cand, const char *source)
{
  cand->optimize_solo();
  msg("goomba: %s guess: %s\n", source, cand->dstr());
  out->push_back(cand);
}

//--------------------------------------------------------------------------
bool optimizer_t::optimize_insn(minsn_t *insn)
{
  if ( insn->has_side_effects(true) )
  {
    // msg("goomba: instruction has side effects, skipping\n");
    return false;
  }

  if ( !is_mba(*insn) )
    return false; // not an MBA instruction
  msg("goomba: found an MBA instruction %s\n", insn->dstr());

  bool success = false;
  auto start_time = std::chrono::high_resolution_clock::now();
  minsnptrs_t candidates;
  try
  {
    auto equiv_class_start = std::chrono::high_resolution_clock::now();
    if ( equiv_classes != nullptr )
    { // Find candidates from the oracle file
      minsnptrs_t tmp;
      equiv_classes->find_candidates(&tmp, *insn);
      for ( minsn_t *i : tmp )
        add_candidate(&candidates, i, "Oracle");
    }
    auto equiv_class_end = std::chrono::high_resolution_clock::now();

    // Produce one candidate using naive linear guess
    auto linear_start = equiv_class_end;
    linear_expr_t linear_guess(*insn);
    add_candidate(&candidates, linear_guess.to_minsn(insn->ea), "Linear");
    auto linear_end = std::chrono::high_resolution_clock::now();

    // Produce one candidate using SiMBA's algorithm
    auto lin_conj_start = linear_end;
    lin_conj_expr_t lin_conj_guess(*insn);      // MBA Solver's simplification
    simp_lin_conj_expr_t simp_lin_conj_expr(lin_conj_guess);      // Simba's simplification
    add_candidate(&candidates, simp_lin_conj_expr.to_minsn(insn->ea), "Simplified lin conj");
    auto lin_conj_end = std::chrono::high_resolution_clock::now();

    // Produce one candidate using non-linear MBA simplification
    auto nonlin_start = lin_conj_end;
    nonlin_expr_t nonlin_guess(*insn);
    if ( nonlin_guess.success() )
    {
      add_candidate(&candidates, nonlin_guess.to_minsn(insn->ea), "Non-linear");
    }
    auto nonlin_end = std::chrono::high_resolution_clock::now();

    // Verify the candidates. Return the simplest one that passed verification.
    std::sort(candidates.begin(), candidates.end(), minsn_complexity_cmptr_t());
    for ( minsn_t *cand : candidates )
    {
      if ( check_and_substitute(insn, cand, z3_timeout, z3_assume_timeouts_correct) )
      {
        if ( qgetenv("VD_MBA_LOG_PERF") )
        {
          int nvars = get_input_mops(*insn).size();
          msg("goomba: Equiv class time: %d %" FMT_64 "d us\n", nvars,
            std::chrono::duration_cast<std::chrono::microseconds>(equiv_class_end - equiv_class_start).count());
          msg("goomba: Linear time: %d %" FMT_64 "d us\n", nvars,
            std::chrono::duration_cast<std::chrono::microseconds>(linear_end - linear_start).count());
          msg("goomba: Lin conj time: %d %" FMT_64 "d us\n", nvars,
            std::chrono::duration_cast<std::chrono::microseconds>(lin_conj_end - lin_conj_start).count());
          msg("goomba: Non-linear time: %d %" FMT_64 "d us\n", nvars,
            std::chrono::duration_cast<std::chrono::microseconds>(nonlin_end - nonlin_start).count());
        }
        success = true;
        break;
      }
    }
  }
  catch ( const vd_failure_t &vf )
  {
    msg("goomba: %s\n", vf.hf.str.c_str());
  }

  // delete all candidates
  for ( minsn_t *cand : candidates )
    delete cand;

  if ( success )
  {
    auto end_time = std::chrono::high_resolution_clock::now();
    msg("goomba: Time taken: %" FMT_64 "d us\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());
  }
  return success;
}
