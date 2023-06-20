/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
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
struct action_handler : public action_handler_t
{
  plugin_ctx_t *plugmod;

  action_handler(plugin_ctx_t *_plugmod) : plugmod(_plugmod) {}

  virtual int idaapi activate(action_activation_ctx_t *ctx) override;
  virtual action_state_t idaapi update(action_update_ctx_t *) override
  {
    return AST_ENABLE;
  };
};

//--------------------------------------------------------------------------
//lint -e{958} padding of 7 bytes needed to align member on a 8 byte boundary
struct plugin_ctx_t : public plugmod_t
{
  bool run_automatically = false;
  qstring oracle_path;

  action_handler ah;
  optimizer_t optimizer;
  bool plugmod_active = false;
  plugin_ctx_t();
  ~plugin_ctx_t() { term_hexrays_plugin(); }
  virtual bool idaapi run(size_t) override;
};

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

  if ( plugmod->oracle_path.empty() )
    qgetenv("VD_MBA_ORACLE_PATH", &plugmod->oracle_path);

  if ( !plugmod->oracle_path.empty() )
  {
    const char *path = plugmod->oracle_path.c_str();
    FILE *fin = qfopen(path, "rb");
    if ( fin != nullptr )
    {
      plugmod->optimizer.equiv_classes = new equiv_class_finder_lazy_t(fin);
      msg("%s: loaded MBA oracle\n", path);
    }
    else
    {
      msg("%s: %s\n", path, qstrerror(-1));
    }
  }

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
int idaapi action_handler::activate(action_activation_ctx_t *ctx)
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
      if ( plugmod->plugmod_active )
      {
        mba_t *mba = va_arg(va, mba_t *);

        struct ida_local insn_optimize_t : public minsn_visitor_t
        {
          optimizer_t &optimizer;
          int cnt = 0;
          insn_optimize_t ( optimizer_t &o ) : optimizer(o) {}
          int idaapi visit_minsn() override
          {
//            msg("Optimizing %s\n", curins->dstr());
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

        if ( visitor.cnt != 0 )
        {
          mba->verify(true);
          msg("Completed mba optimization pass, improved %d expressions\n", visitor.cnt);
        }
        plugmod->plugmod_active = false;
        mba->clr_mba_flags2(MBA2_PROP_COMPLEX);
        return MERR_LOOP; // restart optimization
      }
      break;

    default:
      break;
  }
  return 0;
}

//--------------------------------------------------------------------------
plugin_ctx_t::plugin_ctx_t() : ah(this)
{
  install_hexrays_callback(callback, this);
  register_action(ACTION_DESC_LITERAL_PLUGMOD(
                    ACTION_NAME,
                    "De-obfuscate arithmetic expressions",
                    &ah,
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
