/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#include <hexrays.hpp>

// functions that convert huge files in a streaming fashion without using too much memory

const int REPORT_FREQ = 10000; // how often we should report progress in the log
// generates a file that is just a list of minsns
void create_minsns_file(FILE *msynth_in, FILE *minsns_out);
// given a minsns file, fingerprints each minsn and serializes it into the oracle
bool create_oracle_file(FILE *minsns_in, FILE *oracle_out);