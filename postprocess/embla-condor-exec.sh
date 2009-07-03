#!/bin/sh

WORKDIR=$1
GENID=$2
SUITE=$3
PROGNAME=$4
shift 3
CLIENT_PROG=$*
EMBLA_BIN=/home/jchm2/gscratch/embla-bin
SCRIPT_DIR=/home/jchm2/Work/embla/postprocess
CREATE_CTLDEPS=$SCRIPT_DIR/cfa.sh
CREATE_DATADEPS="awk -f $SCRIPT_DIR/datadeps.awk"
CREATE_LOOPS=$SCRIPT_DIR/loops.sh
SPREADSHEET=/home/jchm2/public_html/private/embla-$SUITE.txt
DATE=`date -u`
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=80000000
ERR_FILE=condor.$GENID.err
TRACE_FILE_NOLOOP=embla.$GENID.trace
EDGES_FILE_NOLOOP=embla.$GENID.edges
DEP_FILE_NOLOOP=embla.$GENID.deps
TRACE_FILE_LOOP=embla.$GENID.ltrace
EDGES_FILE_LOOP=embla.$GENID.ledges
DEP_FILE_LOOP=embla.$GENID.ldeps
LOOP_FILE=embla.$GENID.loops
TRACE_FILE_JUNK=embla.$GENID.trace.junk
EDGES_FILE_JUNK=embla.$GENID.edges.junk

embla () {
  cd $WORKDIR
  echo "======================================================" >&2
  echo "Date: $DATE" >&2
  echo "Arguments: $*" >&2
  eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n_trace_recs=$N_TRACE_RECS $*"
}

# Dynamic data deps, ILP, dynamic control deps, early spawns, no loops
# Used for generating static deps file
FIRST_OPTS="--loop-file=/dev/null --trace-file=$TRACE_FILE_NOLOOP --edge-file=$EDGES_FILE_NOLOOP --draw --dwar --dwaw --para-non-calls --early-spawns $CLIENT_PROG"

# Dynamic data deps, ILP, dynamic control deps, early spawns, with loops
SECOND_OPTS="--loop-file=$LOOP_FILE --trace-file=$TRACE_FILE_LOOP --edge-file=$EDGES_FILE_LOOP --draw --dwar --dwaw --para-non-calls --early-spawns $CLIENT_PROG"

# Static data deps, TLP, static control deps, no early spawns, no loops. Base Case
OPTS[0]="--loop-file=/dev/null --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_NOLOOP --sraw --swar --swaw --sctl $CLIENT_PROG" 

# Static data deps, TLP, no control deps, no early spawns, no loops.
OPTS[1]="--loop-file=/dev/null --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_NOLOOP --sraw --swar --swaw $CLIENT_PROG" 

# Dynamic data deps, TLP, static control deps, no early spawns, no loops.
OPTS[2]="--loop-file=/dev/null --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_NOLOOP --draw --dwar --dwaw --sctl $CLIENT_PROG" 

# Static data deps, ILP, static control deps, no early spawns, no loops.
OPTS[3]="--loop-file=/dev/null --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_NOLOOP --sraw --swar --swaw --sctl --para-non-calls $CLIENT_PROG" 

# Static data deps, TLP, static control deps, early spawns, no loops.
OPTS[4]="--loop-file=/dev/null --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_NOLOOP --sraw --swar --swaw --sctl --early-spawns $CLIENT_PROG" 

# Static data deps, TLP, static control deps, no early spawns, with loops.
OPTS[5]="--loop-file=$LOOP_FILE --trace-file=$TRACE_FILE_JUNK --edge-file=$EDGES_FILE_JUNK --dep-file=$DEP_FILE_LOOP --sraw --swar --swaw --sctl $CLIENT_PROG" 

# Go to working dir
cd $WORKDIR

# Run initial run, to gather data and generate static deps file
embla $FIRST_OPTS
# Create no-loop depfile
$CREATE_CTLDEPS $EDGES_FILE_NOLOOP >$DEP_FILE_NOLOOP
$CREATE_DATADEPS $TRACE_FILE_NOLOOP >>$DEP_FILE_NOLOOP
# Create loop file
$CREATE_LOOPS $EDGES_FILE_NOLOOP >$LOOP_FILE

# Run second run, to gather data and generate static deps file (with loops)
embla $SECOND_OPTS
# Create loop depfile
$CREATE_CTLDEPS $EDGES_FILE_LOOP >$DEP_FILE_LOOP
$CREATE_DATADEPS $TRACE_FILE_LOOP >>$DEP_FILE_LOOP

# Run the other ones
for i in ${!OPTS[*]}
do
    embla "${OPTS[i]}"
done

# Copy all output into embla.log
cat $ERR_FILE >>embla.log

# Also output to spreadsheet
perl /home/jchm2/Work/embla/postprocess/parse-embla-output.pl $PROGNAME <$ERR_FILE >>$SPREADSHEET
