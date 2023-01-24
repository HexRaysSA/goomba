/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#include "z3++_no_warn.h"
#include <hexrays.hpp>
#include <fpro.h>
#include "file.hpp"
#include "msynth_parser.hpp"
#include "simp_lin_conj_exprs.hpp"
#include "heuristics.hpp"
#include "equiv_class.hpp"

//-------------------------------------------------------------------------
// In fact this function is not really needed. The user can simply turn on
// the timestamp display in the output window.
static qstring curtime()
{
  char buf[64];
  char *ptr = buf;
  char *end = buf + sizeof(buf);
  qtime64_t ts = qtime64();
  ptr += qstrftime64(ptr, end-ptr, "%H:%M:%S", ts);
  uint32 msecs = get_usecs(ts) / 1000;
  qsnprintf(ptr, end-ptr, ".%03d", msecs);
  return qstring(buf);
}

//-------------------------------------------------------------------------
void create_minsns_file(FILE *msynth_in, FILE *minsns_out)
{
  qstring line;
  int n_proc = 0;
  int n_written = 0;
  while ( qgetline(&line, msynth_in) >= 0 )
  {
    n_proc++;
    if ( line.size() == 0 )
      continue;
    if ( n_proc % REPORT_FREQ == 0 )
      msg("%s: Processed %d, Wrote %d\n", curtime().c_str(), n_proc, n_written);
    mopvec_t default_vars;
    //-------------------------------------------------------------------------
    // an *abstract* mop is a mop_l that does not refer to anything within a
    // specific program, it is a placeholder for minsn templates
    for ( int i = 0; i < CANDIDATE_EXPR_NUMINPUTS; i++ )
    {
      mop_t new_var;
      new_var.t = mop_l;
      new_var.l = new lvar_ref_t(nullptr, i);
      new_var.size = 8;
      default_vars.push_back(new_var);
    }

    msynth_expr_parser_t mep(line.c_str(), default_vars);
    minsn_t *insn = mep.parse_next_expr();

    bytevec_t bv;
    insn->serialize(&bv);
    uint32 bv_sz = bv.size();
    qfwrite(minsns_out, &bv_sz, sizeof(bv_sz));
    qfwrite(minsns_out, bv.begin(), bv_sz);
    n_written++;

    delete insn;
  }

  msg("%s: Processed %d, Wrote %d\n", curtime().c_str(), n_proc, n_written);
}

//-------------------------------------------------------------------------
// bytevec comparison based on length
struct bv_len_cmptr_t
{
  inline bool operator()(const bytevec_t &a, const bytevec_t &b) const
  {
    auto asz = a.size();
    auto bsz = b.size();
    return std::tie(asz, a) < std::tie(bsz, b);
  }
};
typedef std::set<bytevec_t, bv_len_cmptr_t> bvset_t;

//-------------------------------------------------------------------------
inline size_t bv_sz_on_disk(const bytevec_t &bv)
{
  return sizeof(uint32) + bv.size();
}

//-------------------------------------------------------------------------
static void write_bv_to_disk(FILE *fout, const bytevec_t &bv)
{
  uint32 bv_sz = bv.size();
  qfwrite(fout, &bv_sz, sizeof(bv_sz));
  qfwrite(fout, bv.begin(), bv_sz);
}

//-------------------------------------------------------------------------
static size_t bvset_sz_on_disk(const bvset_t &bvset)
{
  size_t res = sizeof(uint32);
  for ( const auto &bv : bvset )
    res += bv_sz_on_disk(bv);
  return res;
}

//-------------------------------------------------------------------------
static void write_bvset_to_disk(FILE *fout, const bvset_t &bvset)
{
  uint32 bvset_sz = bvset.size();
  qfwrite(fout, &bvset_sz, sizeof(bvset_sz));
  for ( const auto &bv : bvset )
    write_bv_to_disk(fout, bv);
}

//-------------------------------------------------------------------------
bool create_oracle_file(FILE *minsns_in, FILE *oracle_out)
{
  // begin by loading the minsns from the file and generating fingerprints
  // keeping full minsns in memory would take too much space, so we store them as strings
  // and use string length as a proxy for complexity
  std::map<func_fingerprint_t, bvset_t> oracle;
  equiv_class_finder_t ecf;

  int n_proc = 0;
  while ( true )
  {
    if ( n_proc % REPORT_FREQ == 0 )
      msg("%s: Processed %d, #Fingerprints %" FMT_Z "\n", curtime().c_str(), n_proc, oracle.size());
    n_proc++;
    uint32 minsn_sz;
    if ( qfread(minsns_in, &minsn_sz, sizeof(minsn_sz)) != sizeof(minsn_sz) )
      break;
    if ( minsn_sz > qfsize(minsns_in) ) // sanity check on minsn_sz
    {
      msg("Wrong instruction size %d in the oracle file, stopped reading it\n", minsn_sz);
      return false;
    }
    bytevec_t buf;
    buf.resize(minsn_sz);
    if ( qfread(minsns_in, buf.begin(), minsn_sz) != minsn_sz )
      break;

    func_fingerprint_t fp = ecf.compute_fingerprint_from_serialization(buf.begin(), minsn_sz);

    if ( oracle.count(fp) == 0 )
      oracle.insert( { fp, std::set<bytevec_t, bv_len_cmptr_t>() } );

    oracle[fp].insert(buf);
  }

  msg("%s: Processed %d, #Fingerprints %" FMT_Z "\n", curtime().c_str(), n_proc, oracle.size());

  // write the resulting oracle to the file
  // begin by writing the format version
  {
    bytevec_t bv;
    uint32 format_version = minsn_t(0).serialize(&bv);
    qfwrite(oracle_out, &format_version, sizeof(format_version));
  }

  // write the ecf's test cases to file
  uint32 n_tcs = ecf.testcases.size();
  qfwrite(oracle_out, &n_tcs, sizeof(n_tcs));
  for ( const testcase_t &tc : ecf.testcases )
    for ( const uint64 input : tc )
      qfwrite(oracle_out, &input, sizeof(input));

  msg("Wrote test cases to file\n");

  // write the index to file
  // the index is a list of entries, each consisting of a uint64 (fingerprint) and a uint64 (offset)
  uint32 index_sz = oracle.size();
  qfwrite(oracle_out, &index_sz, sizeof(index_sz));
  qoff64_t current_offset = 0;
  int n_written = 0;
  for ( const auto &entry : oracle )
  {
    if ( n_written % REPORT_FREQ == 0 )
      msg("%s: Wrote %d index entries\n", curtime().c_str(), n_written);
    n_written++;

    auto fingerprint = entry.first;
    auto bvset = entry.second;
    qfwrite(oracle_out, &fingerprint, sizeof(fingerprint));
    qfwrite(oracle_out, &current_offset, sizeof(current_offset));

    current_offset += bvset_sz_on_disk(bvset);
  }

  msg("Size of oracle on disk: %llu\n", current_offset);
  msg("Current file position: %llu\n", qftell(oracle_out));

  // write the actual microinstructions to disk
  n_written = 0;
  for ( const auto &entry : oracle )
  {
    if ( n_written % REPORT_FREQ == 0 )
      msg("%s: Wrote %d microinstruction vectors\n", curtime().c_str(), n_written);
    n_written++;

    write_bvset_to_disk(oracle_out, entry.second);
  }

  msg("%s: Wrote %d microinstruction vectors\n", curtime().c_str(), n_written);
  msg("Current file position: %" FMT_64 "u\n", qftell(oracle_out));
  return true;
}
