/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include "equiv_class.hpp"
#include "optimizer.hpp"


//-------------------------------------------------------------------------
// replaces all references to abstract mop_l's with variables from new_vars
minsn_t *make_concrete_minsn(ea_t ea, const minsn_t &minsn, const mopvec_t &new_vars, int newsz)
{
  struct mop_reassigner_t : public mop_visitor_t
  {
    const mopvec_t &new_vars;
    ea_t ea;
    mop_reassigner_t(ea_t e, const mopvec_t &nm)
      : new_vars(nm), ea(e) {}
    int idaapi visit_mop(mop_t *op, const tinfo_t *, bool)
    {
      if ( op->t == mop_l )
      {
        int idx = op->l->idx;
        if ( idx >= new_vars.size() )
          return -1;
        op->t = mop_d;
        op->d = resize_mop(ea, new_vars.at(idx), op->size, false);
      }
      return 0;
    }
  };

  minsn_t *res = nullptr;
  minsn_t *copy = new minsn_t(minsn);

  mop_reassigner_t mr(ea, new_vars);
  int code = copy->for_all_ops(mr);
  if ( code >= 0 )
  {
    copy->setaddr(ea);

    // resize res to the correct output size
    mop_t res_mop;
    res_mop.create_from_insn(copy);
    res = resize_mop(ea, res_mop, newsz, false);
  }
  delete copy;
  return res;
}

//-------------------------------------------------------------------------
static void create_var_mapping(var_mapping_t &dest, const mopvec_t &mops)
{
  for ( size_t i = 0; i < mops.size(); i++ )
    dest.insert( { mops[i], i } );
}

//-------------------------------------------------------------------------
void equiv_class_finder_t::find_candidates(minsnptrs_t *out, const minsn_t &insn)
{
  std::set<func_fingerprint_t> seen;
  int num_fingerprints = 0; // includes duplicate fingerprints
  int num_candidates = 0;

  mopvec_t input_mops = get_input_mops(insn);
  do
  {
    var_mapping_t mapping;
    create_var_mapping(mapping, input_mops);

    func_fingerprint_t fingerprint = compute_fingerprint(insn, &mapping);
//    msg("goomba: computed fingerprint %" FMT_64 "x\n", fingerprint);

    num_fingerprints++;
    if ( num_fingerprints > EQUIV_CLASS_MAX_FINGERPRINTS )
      break;

    if ( !seen.insert(fingerprint).second )
      continue; // already seen

    const minsn_set_t *equiv_class = find_equiv_class(fingerprint);
    if ( equiv_class != nullptr )
    {
      for ( const auto &mi : *equiv_class )
      {
        num_candidates++;
//        msg("goomba: fingerprint matches: %s\n", mi->dstr());
        minsn_t *concrete = make_concrete_minsn(insn.ea, *mi, input_mops, insn.d.size);
        if ( concrete != nullptr )
          out->push_back(concrete);

        if ( num_candidates >= EQUIV_CLASS_MAX_CANDIDATES )
          break;
      }
    }

  } while ( std::next_permutation(input_mops.begin(), input_mops.end()) );
}
