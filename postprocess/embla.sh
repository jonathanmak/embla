#!/bin/sh

CLIENT_PROG=$*
BASENAME=`basename $1`
EMBLA_BIN=/home/jchm2/gscratch/embla-bin
SCRIPT_DIR=/home/jchm2/Work/embla/postprocess
CREATE_CTLDEPS=$SCRIPT_DIR/cfa.sh
CREATE_DATADEPS="awk -f $SCRIPT_DIR/datadeps.awk"
CREATE_LOOPS=$SCRIPT_DIR/loops.sh
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=80000000
TRACE_FILE=$BASENAME.trace
EDGES_FILE=$BASENAME.edges
LOOP_FILE=$BASENAME.loops
DEPS_FILE=$BASENAME.deps
TRACE_FILE_LOOP=$BASENAME.ltrace
EDGES_FILE_LOOP=$BASENAME.ledges
DEPS_FILE_LOOP=$BASENAME.ldeps

# Dynamic data deps, ILP, dynamic control deps, early spawns, no loops
# Used for generating static deps file
eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n_trace_recs=$N_TRACE_RECS --loop-file=/dev/null --trace-file=$TRACE_FILE --edge-file=$EDGES_FILE $CLIENT_PROG 1>&2"

# Create no-loop depfile
$CREATE_CTLDEPS $EDGES_FILE >$DEPS_FILE
$CREATE_DATADEPS $TRACE_FILE >>$DEPS_FILE
# Create loop file
$CREATE_LOOPS $EDGES_FILE >$LOOP_FILE

eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n_trace_recs=$N_TRACE_RECS --loop-file=$LOOP_FILE --trace-file=$TRACE_FILE_LOOP --edge-file=$EDGES_FILE_LOOP $CLIENT_PROG 1>&2"

# Create no-loop depfile
$CREATE_CTLDEPS $EDGES_FILE_LOOP >$DEPS_FILE_LOOP
$CREATE_DATADEPS $TRACE_FILE_LOOP >>$DEPS_FILE_LOOP
