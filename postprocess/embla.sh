#!/bin/sh

CLIENT_PROG=$*
BASENAME=`basename $1`
EMBLA_BIN=/home/jchm2/gscratch/embla-bin
SCRIPT_DIR=/home/jchm2/Work/embla/postprocess
CREATE_CTLDEPS=$SCRIPT_DIR/cfa.sh
CREATE_DATADEPS="awk -f $SCRIPT_DIR/datadeps.awk"
CREATE_LOOPS=$SCRIPT_DIR/loops.sh
GRAN_ANALYSIS=$SCRIPT_DIR/gran_analyse
FILTER_LOOPS=$SCRIPT_DIR/filter-loops.pl
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=80000000
TRACE_FILE=$BASENAME.trace
EDGES_FILE=$BASENAME.edges
LOOP_FILE=$BASENAME.loops
DEPS_FILE=$BASENAME.deps
TASK_SIZE_FILE=$BASENAME.tasks
TRACE_FILE_LOOP=$BASENAME.ltrace
EDGES_FILE_LOOP=$BASENAME.ledges
DEPS_FILE_LOOP=$BASENAME.ldeps
LENGTHS_FILE_LOOP=$BASENAME.llengths
TRACE_FILE_PLOOP=$BASENAME.pltrace
EDGES_FILE_PLOOP=$BASENAME.pledges
DEPS_FILE_PLOOP=$BASENAME.pldeps
PLOOP_FILE=$BASENAME.ploops

# Dynamic data deps, ILP, dynamic control deps, early spawns, no loops
# Used for generating static deps file
echo "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=/dev/null --trace-file=$TRACE_FILE --edge-file=$EDGES_FILE $CLIENT_PROG 1>&2"
eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=/dev/null --trace-file=$TRACE_FILE --edge-file=$EDGES_FILE $CLIENT_PROG 1>&2"

# Create no-loop depfile
$CREATE_CTLDEPS $EDGES_FILE >$DEPS_FILE
$CREATE_DATADEPS $TRACE_FILE >>$DEPS_FILE
# Create loop file
$CREATE_LOOPS $EDGES_FILE >$LOOP_FILE

echo "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=$LOOP_FILE --trace-file=$TRACE_FILE_LOOP --edge-file=$EDGES_FILE_LOOP --lengths-file=$LENGTHS_FILE_LOOP $CLIENT_PROG 1>&2"
eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=$LOOP_FILE --trace-file=$TRACE_FILE_LOOP --edge-file=$EDGES_FILE_LOOP --lengths-file=$LENGTHS_FILE_LOOP $CLIENT_PROG 1>&2"

# Create loop depfile
$CREATE_CTLDEPS $EDGES_FILE_LOOP >$DEPS_FILE_LOOP
$CREATE_DATADEPS $TRACE_FILE_LOOP >>$DEPS_FILE_LOOP

# Work out task sizes
$GRAN_ANALYSIS --sraw --swar --swaw --sctl --dep-file $DEPS_FILE --edge-file $EDGES_FILE --lengths-file $LENGTHS_FILE_LOOP >$TASK_SIZE_FILE

# Filter loops based on depfile
$FILTER_LOOPS --sraw --swar --swaw --sctl $DEPS_FILE_LOOP <$LOOP_FILE >$PLOOP_FILE

echo "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=$PLOOP_FILE --trace-file=$TRACE_FILE_PLOOP --edge-file=$EDGES_FILE_PLOOP $CLIENT_PROG 1>&2"
eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n-trace-recs=$N_TRACE_RECS --loop-file=$PLOOP_FILE --trace-file=$TRACE_FILE_PLOOP --edge-file=$EDGES_FILE_PLOOP $CLIENT_PROG 1>&2"

# Create parallel loop depfile
$CREATE_CTLDEPS $EDGES_FILE_PLOOP >$DEPS_FILE_PLOOP
$CREATE_DATADEPS $TRACE_FILE_PLOOP >>$DEPS_FILE_PLOOP

