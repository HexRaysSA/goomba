#!/bin/bash

# usage: ./generate_oracle.sh all_combined.txt
# after the script finishes running, the oracle file will be available in all_combined.txt.oracle

(
  VD_MSYNTH_PATH=`realpath $1` ida64 -A -S`realpath script.py` -Llog.txt tests/idb/mba_challenge.i64
  VD_MBA_MINSNS_PATH=`realpath $1.b` ida64 -A -S`realpath script.py` -Llog.txt tests/idb/mba_challenge.i64
  mv $1.b.c $1.oracle
  echo -e "\nfinished! Result is in $1.oracle" >> log.txt
) &

tail -F log.txt