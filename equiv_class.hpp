/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <hexrays.hpp>
#include "msynth_parser.hpp"
#include "heuristics.hpp"
#include "linear_exprs.hpp"
#include "consts.hpp"

struct minsn_with_mapping_t;

typedef std::set<minsn_t*, minsn_complexity_cmptr_t> minsn_set_t;

typedef qvector<uint64> output_behavior_t;
typedef qvector<uint64> testcase_t;
typedef std::map<mop_t, int> var_mapping_t;
typedef uint64 func_fingerprint_t;
typedef std::map<func_fingerprint_t, minsn_set_t> equiv_class_map_t;

#define CHECK_SERIALIZATION_CONSISTENCY true

//-------------------------------------------------------------------------
// output behavior is summarized as a list of uint64's, each corresponding to a test case
inline func_fingerprint_t compute_fingerprint_from_outputs(const output_behavior_t &outputs)
{
  // FNV-1a, as per Wikipedia
  const uint64 FNV_BASIS = 0xcbf29ce484222325;
  const uint64 FNV_PRIME = 0x100000001b3;
  uint64 sum = FNV_BASIS;
  for ( uint64 c : outputs )
  {
    sum ^= c;
    sum *= FNV_PRIME;
  }
  return sum;
}

//-------------------------------------------------------------------------
inline void gen_testcase(testcase_t *tc)
{
  tc->resize(CANDIDATE_EXPR_NUMINPUTS);
  for ( auto &v : *tc )
    v = gen_rand_mcode_val(8).val;
}

//-------------------------------------------------------------------------
class equiv_class_finder_t
{
public:
  equiv_class_map_t equiv_classes;
  qvector<testcase_t> testcases;

  //-------------------------------------------------------------------------
  // helper_emu_t evaluates expressions for a given test case and variable mapping
  struct helper_emu_t : public mcode_emulator_t
  {
    const testcase_t &tc;
    const var_mapping_t *var_mapping; // maps variables to input index
    // assigning a nullptr var_mapping indicates that the indexing should be done
    // according to the abstract mop's self-declared index

    helper_emu_t (const testcase_t &t, const var_mapping_t *vm)
      : tc(t), var_mapping(vm) {}

    virtual mcode_val_t get_var_val(const mop_t &mop) override
    {
      if ( var_mapping == nullptr )
      {
        // the instruction must be abstract, get the index from the mop itself
        QASSERT(30773, mop.t == mop_l);
        return mcode_val_t(tc[mop.l->idx], mop.size);
      }
      return mcode_val_t(tc.at(var_mapping->at(mop)), mop.size);
    }
  };

  virtual ~equiv_class_finder_t() {}

  //-------------------------------------------------------------------------
  equiv_class_finder_t()
  {
    testcases.resize(TCS_PER_EQUIV_CLASS);
    for ( auto &tc : testcases )
      gen_testcase(&tc);
  }

  //-------------------------------------------------------------------------
  // mapping = nullptr means the instruction is abstract (all terminal mops
  // have type mop_l), and mop indices will be retrieved by querying mop.l->idx
  func_fingerprint_t compute_fingerprint(
        const minsn_t &ins,
        const var_mapping_t *mapping = nullptr)
  {
    output_behavior_t res;
    res.reserve(testcases.size());
    for ( const auto &tc : testcases )
    {
      helper_emu_t emu(tc, mapping);
      res.push_back(emu.minsn_value(ins).val);
    }
    return compute_fingerprint_from_outputs(res);
  }

  //-------------------------------------------------------------------------
  func_fingerprint_t compute_fingerprint_from_serialization(
        uchar *buf, uint32 sz,
        int version = -1,
        const var_mapping_t *mapping = nullptr)
  {
    if ( version == -1 ) // use current serialization version
    {
      bytevec_t bv;
      version = minsn_t(0).serialize(&bv);
    }
    minsn_t minsn(0);
    minsn.deserialize(buf, sz, version);

    return compute_fingerprint(minsn, mapping);
  }

  //-------------------------------------------------------------------------
  // computes the fingerprint of the abstract minsn and adds it to the index
  void add_abstract_minsn(minsn_t *ins)
  {
    auto fingerprint = compute_fingerprint(*ins);
    auto it = equiv_classes.find(fingerprint);
    if ( it != equiv_classes.end() )
    {
      // check if semantically equivalent expression already exists
      for ( const auto &o : it->second )
        if ( probably_equivalent(*o, *ins) )
          return;
      it->second.insert(ins);
    }
    else
    {
      minsn_set_t new_entry;
      new_entry.insert(ins);
      equiv_classes.insert( { fingerprint, new_entry } );
    }
  }

  //-------------------------------------------------------------------------
  virtual const minsn_set_t *find_equiv_class(func_fingerprint_t fingerprint)
  {
    auto p = equiv_classes.find(fingerprint);
    if ( p != equiv_classes.end() )
      return &p->second;
    return nullptr;
  }

  //-------------------------------------------------------------------------
  // find candidate minsns that match the fingerprint of the given minsn
  // before being added, these are made concrete -- the abstract mop_l's are
  // replaced by real mops from the input insn
  void find_candidates(minsnptrs_t *out, const minsn_t &insn);
};

//-------------------------------------------------------------------------
struct equiv_class_idx_entry_t
{
  func_fingerprint_t fingerprint;
  uint64_t offset;
  // offset relative to the beginning of where minsns are stored within the oracle file

  bool operator<(const equiv_class_idx_entry_t &o) const
  {
    return fingerprint < o.fingerprint;
  }
};

//-------------------------------------------------------------------------
struct equiv_class_idx_t
{
  qvector<equiv_class_idx_entry_t> index;

  //-------------------------------------------------------------------------
  void read_from_file(FILE *file)
  {
    uint32 idx_sz;
    if ( qfread(file, &idx_sz, sizeof(idx_sz)) != sizeof(idx_sz) )
      INTERR(30719);
    CASSERT(sizeof(equiv_class_idx_entry_t) == 16);

    index.resize_noinit(idx_sz);
    size_t nbytes = idx_sz * sizeof(equiv_class_idx_entry_t);
    if ( qfread(file, index.begin(), nbytes) != nbytes )
      INTERR(30767);
  }

  //-------------------------------------------------------------------------
  size_t find(func_fingerprint_t fp)
  {
    equiv_class_idx_entry_t key;
    key.fingerprint = fp;
    auto p = std::lower_bound(index.begin(), index.end(), key);
    if ( p == index.end() || p->fingerprint != fp )
      return -1;
    return p->offset;
  }
};

//-------------------------------------------------------------------------
// lazy-loading collection of equivalence classes
struct equiv_class_finder_lazy_t : public equiv_class_finder_t
{
  FILE *file;
  qoff64_t fsize;
  uint32 format_version; // format version used to serialize minsn_t's
  equiv_class_idx_t index;
  uint64 minsns_offset; // offset at which the minsns table begins

  virtual ~equiv_class_finder_lazy_t() { qfclose(file); }

  //-------------------------------------------------------------------------
  //lint -sem(equiv_class_finder_lazy_t::equiv_class_finder_lazy_t, custodial(1))
  equiv_class_finder_lazy_t(FILE *f) : file(f)
  {
    fsize = qfsize(file);

    // read in the format version
    if ( qfread(file, &format_version, sizeof(format_version)) != sizeof(format_version) )
      INTERR(30774);

    // read and validate the number of the test cases
    uint32 n_tcs;
    if ( qfread(file, &n_tcs, sizeof(n_tcs)) != sizeof(n_tcs) )
      INTERR(30775);
    if ( n_tcs > fsize )
      INTERR(30768);

    // read in the test cases
    testcases.resize(n_tcs);
    for ( auto &new_tc : testcases )
    {
      new_tc.resize(CANDIDATE_EXPR_NUMINPUTS);
      for ( uint64 &new_inp : new_tc )
        if ( qfread(file, &new_inp, sizeof(new_inp)) != sizeof(new_inp) )
          INTERR(30776);
    }

    // read in the index
    index.read_from_file(file);

    minsns_offset = qftell(file);
//    msg("minsns offset %llu", minsns_offset);
  }

  //-------------------------------------------------------------------------
  // populates the equiv_classes map with the minsn set included in the file
  // for the given fingerprint
  void read_minsn_set_from_file(func_fingerprint_t fp)
  {
    int64 idx_lookup = index.find(fp);
    if ( idx_lookup < 0 )
      return; // fingerprint doesn't exist in oracle
    if ( equiv_classes.count(fp) != 0 )
      return; // we already loaded in the equiv class

    uint64 minsn_offset = minsns_offset + idx_lookup;
    if ( qfseek(file, minsn_offset, SEEK_SET) != 0 )
      INTERR(30722);

    uint32 n_minsns;
    if ( qfread(file, &n_minsns, sizeof(n_minsns)) != sizeof(n_minsns) )
      INTERR(30723);
    if ( n_minsns > fsize ) // sanity check
      INTERR(30769);

    bytevec_t bv;
    minsn_set_t &set = equiv_classes[fp];
    for ( uint32 i = 0; i < n_minsns; i++ )
    {
      uint32 minsn_sz;
      if ( qfread(file, &minsn_sz, sizeof(minsn_sz)) != sizeof(minsn_sz) )
        INTERR(30724);
      if ( minsn_sz > fsize ) // sanity check
        INTERR(30770);
      bv.resize(minsn_sz);
      if ( qfread(file, bv.begin(), minsn_sz) != minsn_sz )
        INTERR(30725);
      minsn_t *minsn = new minsn_t(0);
      minsn->deserialize(bv.begin(), minsn_sz, format_version);
      set.insert(minsn);
    }
  }

  //-------------------------------------------------------------------------
  const minsn_set_t *find_equiv_class(func_fingerprint_t fingerprint) override
  {
    read_minsn_set_from_file(fingerprint);
    return equiv_class_finder_t::find_equiv_class(fingerprint);
  }

  //-------------------------------------------------------------------------
  bool optimize(minsn_t &insn);
};
