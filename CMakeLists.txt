cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(goomba)

set(CMAKE_CXX_STANDARD 17)

if (DEFINED Z3_DIR)
    set(Z3_DIR ${Z3_DIR} CACHE STRING "Z3 binary package location")
elseif (DEFINED ENV{Z3_DIR})
    set(Z3_DIR $ENV{Z3_DIR} CACHE STRING "Z3 binary package location (environment variable) ")
else()
    set(Z3_DIR "${CMAKE_CURRENT_LIST_DIR}/z3" CACHE STRING "Z3 binary package default location")
endif()

if (NOT EXISTS "${Z3_DIR}/bin/libz3${CMAKE_STATIC_LIBRARY_SUFFIX}" OR NOT EXISTS "${Z3_DIR}/include/z3++.h")
    message(FATAL_ERROR "Z3_DIR not defined or the required files are not present or properly configured. Please pass -DZ3_DIR=/path/to/z3-package or specify the Z3_DIR environment variable where both '<Z3_DIR>/lib/libz3${CMAKE_STATIC_LIBRARY_SUFFIX}' and '<Z3_DIR>/include/z3++.h' are present.")
endif()

set(PLUGIN_LINK_LIBRARIES      ${Z3_DIR}/bin/libz3${CMAKE_STATIC_LIBRARY_SUFFIX})
set(PLUGIN_INCLUDE_DIRECTORIES ${Z3_DIR}/include)

set(PLUGIN_NAME              goomba)
set(PLUGIN_SOURCES           goomba.cpp 
                             bitwise_expr_lookup_tbl.cpp bitwise_expr_lookup_tbl.hpp
                             consts.hpp
                             equiv_class.cpp equiv_class.hpp
                             file.cpp file.hpp
                             heuristics.cpp heuristics.hpp
                             lin_conj_exprs.hpp linear_exprs.cpp
                             linear_exprs.hpp
                             mcode_emu.hpp
                             minsn_template.hpp
                             msynth_parser.cpp msynth_parser.hpp 
                             optimizer.cpp optimizer.hpp
                             simp_lin_conj_exprs.hpp
                             smt_convert.cpp smt_convert.hpp)

if (DEFINED EA64)
    set(PLUGIN_RUN_ARGS "${CMAKE_CURRENT_LIST_DIR}/tests/idb/mba_challenge.i64")
endif()

include($ENV{IDASDK}/ida-cmake/addons.cmake)
