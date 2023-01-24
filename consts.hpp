/*
 *      Copyright (c) 2023 by Hex-Rays, support@hex-rays.com
 *      ALL RIGHTS RESERVED.
 *
 *      gooMBA plugin for Hex-Rays Decompiler.
 *
 */

#pragma once
#define ACTION_NAME "goomba:run"
// Z3_TIMEOUT_MS defines the amount of time we allow the z3 theorem prover to
// take to prove any given statement
#define Z3_TIMEOUT_MS 1000

// Only used for *generating* oracles: how many test cases to run against each
// function to generate fingerprints. Note that an existing oracle will report
// its own number, and the below constant will not be used
#define TCS_PER_EQUIV_CLASS 128
// The number of inputs used when evaluating functions for fingerprinting
#define CANDIDATE_EXPR_NUMINPUTS 5
// The maximum number of candidates to consider which have the same fingerprint
// as the expression being simplified
#define EQUIV_CLASS_MAX_CANDIDATES 10
// The maximum number of fingerprints to consider for each expression being
// simplified -- this number is greater than one since we consider every
// possible assignment of input variables
#define EQUIV_CLASS_MAX_FINGERPRINTS 50