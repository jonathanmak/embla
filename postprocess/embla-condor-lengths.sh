#!/bin/sh

if [ $# -lt 3 ]; then
  echo "Usage: $0 <benchmark-suite> <program name> <command>"
  exit 1
fi

EMBLA_BIN=/home/jchm2/gscratch/embla-bin
DATE=`date -u`
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=80000000
WORKDIR=`pwd`

GENID=`date -u +%s%N`
CONDOR_SCRIPT="universe        = vanilla
executable      = /home/jchm2/Work/embla/postprocess/embla-condor-exec-lengths.sh
nice_user       = True
requirements    = Memory > 3000
log             = condor.log
output          = condor.$GENID.out
error           = condor.$GENID.err
arguments       = $WORKDIR $GENID $*
queue"

echo "$CONDOR_SCRIPT" | condor_submit -

