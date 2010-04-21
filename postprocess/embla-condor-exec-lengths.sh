#!/bin/sh

WORKDIR=$1
GENID=$2
SUITE=$3
PROGNAME=$4
shift 4
CLIENT_PROG=$*
EMBLA_BIN=/home/jchm2/gscratch/embla-bin
SCRIPT_DIR=/home/jchm2/Work/embla/postprocess
CREATE_CTLDEPS=$SCRIPT_DIR/cfa.sh
CREATE_DATADEPS="awk -f $SCRIPT_DIR/datadeps.awk"
GRAN_ANALYSIS=$SCRIPT_DIR/gran_analyse
DATE=`date -u`
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=160000000
ERR_FILE=condor.$GENID.err
TRACE_FILE=embla.$GENID.traces
EDGE_FILE=embla.$GENID.edges
DEP_FILE=embla.$GENID.deps
LENGTHS_FILE=embla.$GENID.lengths
TASK_SIZE_FILE=embla.$GENID.tasks
SPREADSHEET=/home/jchm2/public_html/private/embla-gran-$SUITE.txt

embla () {
  cd $WORKDIR
  echo "======================================================" >&2
  echo "Date: $DATE" >&2
  echo "Arguments: $*" >&2
  eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS $*"
}

# Go to working dir
cd $WORKDIR

embla "--trace-file=$TRACE_FILE --edge-file=$EDGE_FILE --lengths-file=$LENGTHS_FILE $CLIENT_PROG"

# Create no-loop depfile
$CREATE_CTLDEPS $EDGE_FILE >$DEP_FILE
$CREATE_DATADEPS $TRACE_FILE >>$DEP_FILE
$GRAN_ANALYSIS --sraw --swar --swaw --sctl --dep-file $DEP_FILE --edge-file $EDGE_FILE --lengths-file $LENGTHS_FILE >$TASK_SIZE_FILE

for s in 0 10 20 40 80 160 320 ; do
  embla "--dep-file=$DEP_FILE --task-size-file=$TASK_SIZE_FILE --spawn-threshold=$s --sraw --swar --swaw --sctl --no-reductions $CLIENT_PROG"
done

# Copy all output into embla.log
cat $ERR_FILE >>embla.log

# Also output to spreadsheet
perl $SCRIPT_DIR/parse-embla-output.pl $PROGNAME <$ERR_FILE >>$SPREADSHEET

