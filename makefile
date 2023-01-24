PROC=goomba

GOALS += $(R)libz3$(DLLEXT)
O2=heuristics
O3=smt_convert
O4=linear_exprs
O5=msynth_parser
O6=bitwise_expr_lookup_tbl
O7=optimizer
O8=equiv_class
O9=file

CONFIGS=goomba.cfg
include ../plugin.mak

ifeq ($(THIRD_PARTY),)
  # building outside of Hex-Rays tree, use a local z3 build 
  Z3VER=4.11.2
  Z3BIN-$(__LINUX__) = z3/z3-$(Z3VER)-x64-glibc-2.31/bin/
  Z3BIN-$(__NT__)    = z3/z3-$(Z3VER)-x64-win/bin/
  ifdef __ARM__
    Z3BIN-$(__MAC__) = z3/z3-$(Z3VER)-arm64-osx-11.0/bin/
  else
    Z3BIN-$(__MAC__) = z3/z3-$(Z3VER)-osx-10.16/
  endif
  Z3_BIN = $(Z3BIN-1)
  Z3_INCLUDE = $(Z3_BIN)../include/
endif

ifdef __NT__
  # link to the import library on Windows
  STDLIBS += $(Z3_BIN)libz3.lib
else
  # link directly to dylib/shared object on Unix
  STDLIBS += -L$(R) -lz3
endif

$(F)$(PROC)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O2)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O3)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O4)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O5)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O6)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O7)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O8)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(O9)$(O): CC_INCP += $(Z3_INCLUDE) $(Z3_INCLUDE)c++
$(F)$(PROC)$(O): $(R)libz3$(DLLEXT)

$(R)libz3$(DLLEXT): $(Z3_BIN)libz3$(DLLEXT)
	$(Q)$(CP) $? $@

# MAKEDEP dependency list ------------------
$(F)bitwise_expr_lookup_tbl$(O): $(I)bitrange.hpp $(I)bytes.hpp             \
                  $(I)config.hpp $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp       \
                  $(I)hexrays.hpp $(I)ida.hpp $(I)idp.hpp $(I)ieee.h        \
                  $(I)kernwin.hpp $(I)lines.hpp $(I)llong.hpp               \
                  $(I)loader.hpp $(I)nalt.hpp $(I)name.hpp $(I)netnode.hpp  \
                  $(I)pro.h $(I)range.hpp $(I)segment.hpp $(I)typeinf.hpp   \
                  $(I)ua.hpp $(I)xref.hpp bitwise_expr_lookup_tbl.cpp       \
                  bitwise_expr_lookup_tbl.hpp consts.hpp linear_exprs.hpp   \
                  mcode_emu.hpp minsn_template.hpp smt_convert.hpp          \
                  z3++_no_warn.h
$(F)equiv_class$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp          \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  bitwise_expr_lookup_tbl.hpp consts.hpp equiv_class.cpp    \
                  equiv_class.hpp heuristics.hpp lin_conj_exprs.hpp         \
                  linear_exprs.hpp mcode_emu.hpp minsn_template.hpp         \
                  msynth_parser.hpp optimizer.hpp simp_lin_conj_exprs.hpp   \
                  smt_convert.hpp z3++_no_warn.h
$(F)file$(O)    : $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp $(I)fpro.h  \
                  $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp $(I)ida.hpp     \
                  $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp $(I)lines.hpp      \
                  $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp $(I)name.hpp    \
                  $(I)netnode.hpp $(I)pro.h $(I)range.hpp $(I)segment.hpp   \
                  $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp                   \
                  bitwise_expr_lookup_tbl.hpp consts.hpp equiv_class.hpp    \
                  file.cpp file.hpp heuristics.hpp lin_conj_exprs.hpp       \
                  linear_exprs.hpp mcode_emu.hpp minsn_template.hpp         \
                  msynth_parser.hpp simp_lin_conj_exprs.hpp                 \
                  smt_convert.hpp z3++_no_warn.h
$(F)goomba$(O)  : $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp $(I)err.h   \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  bitwise_expr_lookup_tbl.hpp consts.hpp equiv_class.hpp    \
                  file.hpp goomba.cpp heuristics.hpp lin_conj_exprs.hpp     \
                  linear_exprs.hpp mcode_emu.hpp minsn_template.hpp         \
                  msynth_parser.hpp optimizer.hpp simp_lin_conj_exprs.hpp   \
                  smt_convert.hpp z3++_no_warn.h
$(F)heuristics$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp           \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  heuristics.cpp heuristics.hpp linear_exprs.hpp            \
                  mcode_emu.hpp smt_convert.hpp z3++_no_warn.h
$(F)linear_exprs$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp         \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  linear_exprs.cpp linear_exprs.hpp mcode_emu.hpp           \
                  smt_convert.hpp z3++_no_warn.h
$(F)msynth_parser$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp        \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  consts.hpp linear_exprs.hpp mcode_emu.hpp                 \
                  minsn_template.hpp msynth_parser.cpp msynth_parser.hpp    \
                  smt_convert.hpp z3++_no_warn.h
$(F)optimizer$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp            \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  bitwise_expr_lookup_tbl.hpp consts.hpp equiv_class.hpp    \
                  heuristics.hpp lin_conj_exprs.hpp linear_exprs.hpp        \
                  mcode_emu.hpp minsn_template.hpp msynth_parser.hpp        \
                  optimizer.cpp optimizer.hpp simp_lin_conj_exprs.hpp       \
                  smt_convert.hpp z3++_no_warn.h
$(F)smt_convert$(O): $(I)bitrange.hpp $(I)bytes.hpp $(I)config.hpp          \
                  $(I)fpro.h $(I)funcs.hpp $(I)gdl.hpp $(I)hexrays.hpp      \
                  $(I)ida.hpp $(I)idp.hpp $(I)ieee.h $(I)kernwin.hpp        \
                  $(I)lines.hpp $(I)llong.hpp $(I)loader.hpp $(I)nalt.hpp   \
                  $(I)name.hpp $(I)netnode.hpp $(I)pro.h $(I)range.hpp      \
                  $(I)segment.hpp $(I)typeinf.hpp $(I)ua.hpp $(I)xref.hpp   \
                  mcode_emu.hpp smt_convert.cpp smt_convert.hpp             \
                  z3++_no_warn.h
