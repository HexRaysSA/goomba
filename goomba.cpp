/*
 *      Copyright (c) 2025 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *      It deobfuscates the MBA (mixed boolean arithmetic) epxressions.
 *
 */

#include <chrono>

#include "z3++_no_warn.h"
#include "consts.hpp"
#include "optimizer.hpp"
#include "equiv_class.hpp"
#include "file.hpp"
#include <hexrays.hpp>
#include <err.h>

struct plugin_ctx_t;

//--------------------------------------------------------------------------
// returns true if the environment variables indicate the plugin should
// always be enabled (i.e. in testing environments)
inline bool always_on(void)
{
  return qgetenv("VD_MBA_AUTO");
}

//--------------------------------------------------------------------------
struct run_ah_t : public action_handler_t
{
  plugin_ctx_t *plugmod;

  run_ah_t(plugin_ctx_t *_plugmod) : plugmod(_plugmod) {}

  virtual int idaapi activate(action_activation_ctx_t *ctx) override;
  virtual action_state_t idaapi update(action_update_ctx_t *ctx) override
  {
    return ctx->widget_type == BWN_PSEUDOCODE
         ? AST_ENABLE_FOR_WIDGET
         : AST_DISABLE_FOR_WIDGET;
  };
};

//--------------------------------------------------------------------------
//lint -e{958} padding of 7 bytes needed to align member on a 8 byte boundary
struct plugin_ctx_t : public plugmod_t
{
  bool run_automatically = false;
  qstring oracle_path;

  run_ah_t run_ah;
  optimizer_t optimizer;
  bool plugmod_active = false;
  bool inited_oracle = false;
  plugin_ctx_t();
  ~plugin_ctx_t() { term_hexrays_plugin(); }
  virtual bool idaapi run(size_t) override;
  void init_oracle();
};

//--------------------------------------------------------------------------
void plugin_ctx_t::init_oracle()
{
  // a guard to initialize the oracle only once
  if ( inited_oracle )
    return;
  inited_oracle = true;

  const char *idb_path = get_path(PATH_TYPE_IDB);
  if ( idb_path != nullptr )
  {
    char buf[QMAXPATH];
    set_file_ext(buf, sizeof(buf), idb_path, ".disable_oracle");
    if ( qfileexist(buf) )
      return; // do not use the oracle for this file
  }

  if ( oracle_path.empty() )
    qgetenv("VD_MBA_ORACLE_PATH", &oracle_path);

  if ( !oracle_path.empty() )
  {
    const char *path = oracle_path.c_str();
    FILE *fin = qfopen(path, "rb");
    if ( fin != nullptr )
    {
      optimizer.equiv_classes = new equiv_class_finder_lazy_t(fin);
      msg("%s: loaded MBA oracle for goomba\n", path);
    }
    else
    {
      msg("%s: %s\n", path, qstrerror(-1));
    }
  }
}

//--------------------------------------------------------------------------
static plugmod_t *idaapi init()
{
  if ( !init_hexrays_plugin() )
    return nullptr; // no decompiler

  const char *hxver = get_hexrays_version();
  msg("Hex-rays version %s has been detected, %s ready to use\n",
      hxver, PLUGIN.wanted_name);

  plugin_ctx_t *plugmod = new plugin_ctx_t;

  const cfgopt_t cfgopts[] =
  {
    cfgopt_t("MBA_RUN_AUTOMATICALLY", &plugmod->run_automatically, 1),
    cfgopt_t("MBA_Z3_TIMEOUT", &plugmod->optimizer.z3_timeout),
    cfgopt_t("MBA_ORACLE_PATH", &plugmod->oracle_path),
    cfgopt_t("MBA_Z3_ASSUME_TIMEOUTS_CORRECT", &plugmod->optimizer.z3_assume_timeouts_correct, 1)
  };

  read_config_file("goomba", cfgopts, qnumber(cfgopts), nullptr);

  qstring ifpath;
  if ( qgetenv("VD_MSYNTH_PATH", &ifpath) )
  {
    qstring ofpath = ifpath + ".b";
    FILE *fin = qfopen(ifpath.c_str(), "r");
    if ( fin == nullptr )
      error("%s: failed to open for reading", ifpath.c_str());
    FILE *fout = qfopen(ofpath.c_str(), "wb");
    if ( fout == nullptr )
      error("%s: failed to open for writing", ofpath.c_str());
    create_minsns_file(fin, fout);
    qfclose(fin);
    qfclose(fout);
    // do not save the IDB
    set_database_flag(DBFL_KILL);
    qexit(0);
  }

  if ( qgetenv("VD_MBA_MINSNS_PATH", &ifpath) )
  {
    qstring ofpath = ifpath + ".c";
    FILE *fin = qfopen(ifpath.c_str(), "rb");
    if ( fin == nullptr )
      error("%s: failed to open for reading", ifpath.c_str());
    FILE *fout = qfopen(ofpath.c_str(), "wb");
    if ( fout == nullptr )
      error("%s: failed to open for writing", ofpath.c_str());
    bool ok = create_oracle_file(fin, fout);
    qfclose(fin);
    qfclose(fout);
    if ( !ok )
      error("%s: failed to process", ifpath.c_str());
    // do not save the IDB
    set_database_flag(DBFL_KILL);
    qexit(0);
  }

  return plugmod;
}

//--------------------------------------------------------------------------
int idaapi run_ah_t::activate(action_activation_ctx_t *ctx)
{
  vdui_t *vu = get_widget_vdui(ctx->widget);
  if ( vu != nullptr )
  {
    plugmod->plugmod_active = true;
    vu->refresh_view(true);
    return 1;
  }
  return 0;
}

//--------------------------------------------------------------------------
// To satisfy our curiosity: find and print overlapping operands
static void find_and_print_overlapped_operands(mba_t *mba)
{
#if 0
  struct ida_local overlap_finder_t : public minsn_visitor_t
  {
    //------------------------------------------------------------
    static int compare_mops(const mop_t &op1, const mop_t &op2)
    {
      int code = compare(op1.t, op2.t);
      if ( code != 0 )
        return code;
      switch ( op1.t )
      {
        case mop_S:         // stack variable
          code = ::lexcompare(op1.s->off, op2.s->off);
          break;
        case mop_v:         // global variable
          code = ::lexcompare(op1.g, op2.g);
          break;
        case mop_r:         // register
          code = compare(op1.r, op2.r);
          break;
        case mop_l:
          code = op1.l->compare(*op2.l);
          break;
        default:
          INTERR(30822);
      }
      if ( code != 0 )
        return code;
      return compare(op1.size, op2.size);
    }

    //------------------------------------------------------------
    // 0-no overlap, 1-op1 includes op2, -1-op2 includes op1, 2-partial overlap
    static int mops_overlap(const mop_t &op1, const mop_t &op2)
    {
      if ( op1.t != op2.t )
        return 0;
      uval_t off1, off2;
      switch ( op1.t )
      {
        case mop_r:
          off1 = op1.r;
          off2 = op2.r;
          break;
        case mop_S:
          off1 = op1.s->off;
          off2 = op2.s->off;
          break;
        case mop_v:
          off1 = op1.g;
          off2 = op2.g;
          break;
        case mop_l:
          if ( op1.l->idx != op2.l->idx )
            return 0;
          off1 = op1.l->off;
          off2 = op2.l->off;
          break;
        default:
          INTERR(30823);
      }
      if ( !interval::overlap(off1, op1.size, off2, op2.size) )
        return 0;
      if ( interval::includes(off1, op1.size, off2, op2.size) )
        return 1;
      if ( interval::includes(off2, op2.size, off1, op1.size) )
        return -1;
      return 2;
    }

    //----------------------------------------------------------------
    int idaapi visit_minsn() override
    {
      struct mop_collector_t : public mop_visitor_t
      {
        qvector<mop_t> seen;
        qstring info;

        int idaapi visit_mop(mop_t *op, const tinfo_t *, bool)
        {
          mopt_t t = op->t;
          if ( t == mop_r || t == mop_S || t == mop_v || t == mop_l )
          {
            for ( mop_t &op2 : seen )
            {
              int code = compare_mops(*op, op2);
              if ( code == 0 )
                return 0; // already seen
              code = mops_overlap(*op, op2);
              switch ( code )
              {
                case 0:
                  break;
                case 1: // *op includes op2
                  op2 = *op;
                  return 0;
                case -1: // op2 includes *op, nothing to do
                  return 0;
                case 2: // found overlap
                  info.sprnt("%s and %s%s", op->dstr(), op2.dstr(), op->t == mop_r ? " REG" : "");
                  return 1;
              }
            }
            seen.push_back(*op);
          }
          return 0;
        }
      };
      mop_collector_t mc;
      if ( curins->for_all_ops(mc) != 0 )
      {
        msg("%s:%a: detected overlap %s: %s\n",
            qbasename(get_path(PATH_TYPE_IDB)),
            curins->ea,
            mc.info.c_str(), curins->dstr());
      }
      return 0;
    }
  };
  overlap_finder_t find_overlaps;
  mba->for_all_topinsns(find_overlaps);
#else
  qnotused(mba);
#endif
}

//--------------------------------------------------------------------------
// This callback handles various hexrays events.
static ssize_t idaapi callback(void *ud, hexrays_event_t event, va_list va)
{
  plugin_ctx_t *plugmod = (plugin_ctx_t *) ud;
  switch ( event )
  {
    case hxe_microcode: // microcode has been generated
      {
        mba_t *mba = va_arg(va, mba_t *);
        if ( always_on() || plugmod->run_automatically )
          plugmod->plugmod_active = true;
        if ( plugmod->plugmod_active )
          mba->set_mba_flags2(MBA2_PROP_COMPLEX); // increase acceptable complexity
      }
      break;

    case hxe_populating_popup:
      {
        TWidget *widget = va_arg(va, TWidget *);
        TPopupMenu *popup = va_arg(va, TPopupMenu *);
        attach_action_to_popup(widget, popup, ACTION_NAME);
      }
      break;

    case hxe_glbopt:
      {
        mba_t *mba = va_arg(va, mba_t *);

        find_and_print_overlapped_operands(mba);

        if ( !plugmod->plugmod_active )
          return MERR_OK;
        // read the oracle file if not done yet
        plugmod->init_oracle();

        struct ida_local insn_optimize_t : public minsn_visitor_t
        {
          optimizer_t &optimizer;
          int cnt = 0;
          insn_optimize_t ( optimizer_t &o ) : optimizer(o) {}
          int idaapi visit_minsn() override
          {
            // msg("goomba: optimizing %s\n", curins->dstr());
            if ( optimizer.optimize_insn_recurse(curins) )
            {
              cnt++;
              blk->mark_lists_dirty();
              mba->dump_mba(true, "vd_mba success %a", curins->ea);
            }
            return 0;
          }
        };

        insn_optimize_t visitor(plugmod->optimizer);
        mba->for_all_topinsns(visitor);

        plugmod->plugmod_active = false;
        mba->clr_mba_flags2(MBA2_PROP_COMPLEX);
        if ( visitor.cnt != 0 )
        {
          mba->verify(true);
          msg("goomba: completed mba optimization pass, improved %d expressions\n", visitor.cnt);
          return MERR_LOOP; // restart optimization
        }
        return MERR_OK;
      }
      break;

    default:
      break;
  }
  return 0;
}

//--------------------------------------------------------------------------
plugin_ctx_t::plugin_ctx_t() : run_ah(this)
{
  install_hexrays_callback(callback, this);
  register_action(ACTION_DESC_LITERAL_PLUGMOD(
                    ACTION_NAME,
                    "De-obfuscate arithmetic expressions",
                    &run_ah,
                    this,
                    nullptr,
                    "Attempt to simplify Mixed Boolean Arithmetic-obfuscated expressions using gooMBA",
                    -1));
}

//--------------------------------------------------------------------------
bool idaapi plugin_ctx_t::run(size_t)
{
  return true;
}

//--------------------------------------------------------------------------
static char comment[] = "gooMBA plugin for Hex-Rays decompiler";

//--------------------------------------------------------------------------
//
//      PLUGIN DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------
plugin_t PLUGIN =
{
  IDP_INTERFACE_VERSION,
  PLUGIN_MULTI          // The plugin can work with multiple idbs in parallel
| PLUGIN_HIDE,          // no menu items in Edit, Plugins
  init,                 // initialize
  nullptr,
  nullptr,
  comment,              // long comment about the plugin
  nullptr,              // multiline help about the plugin
  "gooMBA plugin",         // the preferred short name of the plugin
  nullptr,              // the preferred hotkey to run the plugin
};
