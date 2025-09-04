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
  msg("Testing candidate %s\n", cand_insn->dstr());
  if ( candidate_score > original_score )
  {
    msg("Candidate (%d) is not simpler than original (%d), skipping\n", candidate_score, original_score);
  }
  else
  {
    z3_converter_t converter;
    if ( probably_equivalent(*insn, *cand_insn) )
    {
      msg("Instruction is probably equivalent to candidate\n");
      if ( skip_proofs() || z3_timeout == 0 )
      {
        set_cmt(insn->ea, "goomba: z3 proof skipped, simplification assumed correct");
        ok = true;
      }
      else
      {
        z3::expr lge = converter.minsn_to_expr(*cand_insn);
        z3::expr ie = converter.minsn_to_expr(*insn);
        z3::solver s(converter.context);
        s.set("timeout", z3_timeout);
        s.add(lge != ie);
        z3::check_result res = s.check();
        msg("SMT check result: %d\n", res);

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
            qstring tmp;
            if ( qgetenv("IDA_TEST_NAME", &tmp) )
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
      msg("Candidate not equivalent, skipping\n");
    }
  }

  if ( ok )
    substitute(insn, cand_insn);

  return ok;
}

//--------------------------------------------------------------------------
bool optimizer_t::optimize_insn_recurse(minsn_t *insn)
{
  if ( optimize_insn(insn) )
    return true;

  bool result = false;

  if ( insn->l.is_insn() )
    result |= optimize_insn_recurse(insn->l.d);

  if ( insn->r.is_insn() )
    result |= optimize_insn_recurse(insn->r.d);

  return result;
}

//--------------------------------------------------------------------------
bool optimizer_t::optimize_insn(minsn_t *insn)
{
  bool success = false;
  auto start_time = std::chrono::high_resolution_clock::now();

  if ( insn->has_side_effects(true) )
  {
//    msg("Instruction has side effects, skipping\n");
  }
  else
  {
    if ( is_mba(*insn) )
    {
      msg("Found MBA instruction %s\n", insn->dstr());

      minsnptrs_t candidates;
      try
      {
        auto equiv_class_start = std::chrono::high_resolution_clock::now();
        if ( equiv_classes != nullptr )
          equiv_classes->find_candidates(&candidates, *insn); // Find candidates from the oracle file
        auto equiv_class_end = std::chrono::high_resolution_clock::now();

        // Produce one candidate using naive linear guess
        auto linear_start = equiv_class_end;
        linear_expr_t linear_guess(*insn);
//        msg("Linear guess %s\n", linear_guess.dstr());
        candidates.push_back(linear_guess.to_minsn(insn->ea));
        auto linear_end = std::chrono::high_resolution_clock::now();

        // Produce one candidate using SiMBA's algorithm
        auto lin_conj_start = linear_end;
        lin_conj_expr_t lin_conj_guess(*insn);      // MBA Solver's simplification
        simp_lin_conj_expr_t simp_lin_conj_expr_t(lin_conj_guess);      // Simba's simplification
//        msg("Simplified lin conj guess %s\n", simp_lin_conj_expr_t.dstr());
        candidates.push_back(simp_lin_conj_expr_t.to_minsn(insn->ea));
        auto lin_conj_end = std::chrono::high_resolution_clock::now();

        // Non-linear MBA optimization
        nonlin_expr_t nonlin_guess(*insn);
        if ( nonlin_guess.success() )
        {
          minsn_t *nonlin_cand = nonlin_guess.to_minsn(insn->ea);
          candidates.push_back(nonlin_cand);
        }

        // Optimize candidates before sorting them by complexity
        for ( minsn_t *cand : candidates )
          cand->optimize_solo();

        std::sort(candidates.begin(), candidates.end(), minsn_complexity_cmptr_t());
//        msg("total %d candidates:\n", candidates.size());
//        for ( minsn_t *cand : candidates )
//          msg("  %s:\n", cand->dstr());

        // Verify the candidates. Return the simplest one that passed verification.
        for ( minsn_t *cand : candidates )
        {
          if ( check_and_substitute(insn, cand, z3_timeout, z3_assume_timeouts_correct) )
          {
            if ( qgetenv("VD_MBA_LOG_PERF") )
            {
              int nvars = get_input_mops(*insn).size();
              msg("Equiv class time: %d %" FMT_64 "d us\n", nvars,
                std::chrono::duration_cast<std::chrono::microseconds>(equiv_class_end - equiv_class_start).count());
              msg("Linear time: %d %" FMT_64 "d us\n", nvars,
                std::chrono::duration_cast<std::chrono::microseconds>(linear_end - linear_start).count());
              msg("Lin conj time: %d %" FMT_64 "d us\n", nvars,
                std::chrono::duration_cast<std::chrono::microseconds>(lin_conj_end - lin_conj_start).count());
            }
            success = true;
            break;
          }
        }
      }
      catch ( const char *&e )
      {
        msg("err: %s\n", e);
      }
      // delete all candidates
      for ( minsn_t *ins : candidates )
        delete ins;
    }
  }

  if ( success )
  {
    auto end_time = std::chrono::high_resolution_clock::now();
    msg("Time taken: %" FMT_64 "d us\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());
  }

  return success;
}
