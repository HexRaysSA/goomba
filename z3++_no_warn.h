#pragma once
// using z3++.h directly leads to compiler warnings about shadowing declaractions
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <z3++.h>
#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif
