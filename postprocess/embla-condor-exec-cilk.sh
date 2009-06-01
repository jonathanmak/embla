#!/bin/sh

WORKDIR=$1
GENID=$2
PROGNAME=$3
shift 3
CLIENT_PROG=$*
EMBLA_BIN=/home/jchm2/gscratch/embla-bin
SPREADSHEET=/home/jchm2/public_html/private/embla-cilk.tsv
DATE=`date -u`
HIDDEN_FUNC_FILE=$EMBLA_BIN/embla.hidden-funcs
N_TRACE_RECS=80000000
ERR_FILE=condor.$GENID.err
TRACE_FILE=embla.$GENID.trace
EDGES_FILE=embla.$GENID.edges
DEP_FILE=embla.$GENID.deps

embla () {
  cd $WORKDIR
  echo "======================================================" >&2
  echo "Date: $DATE" >&2
  echo "Arguments: $*" >&2
  eval "$EMBLA_BIN/bin/valgrind --tool=embla --hidden-func-file=$HIDDEN_FUNC_FILE --n_trace_recs=$N_TRACE_RECS $*"
}

# Dynamic data deps, TLP, dynamic control deps, no early spawns
# Used for generating static deps file
FIRST_OPTS="--trace-file=$TRACE_FILE --edge-file=$EDGES_FILE --draw --dwar --dwaw --dctl $CLIENT_PROG"

# Dynamic data deps, TLP, static control deps, no early spawns. Base Case
OPTS[0]="--dep-file=$DEP_FILE --draw --dwar --dwaw --sctl $CLIENT_PROG" 

# Dynamic data deps, TLP, no control deps, no early spawns
OPTS[1]="--draw --dwar --dwaw $CLIENT_PROG" 

# Static data deps, TLP, static control deps, no early spawns
OPTS[2]="--dep-file=$DEP_FILE --sraw --swar --swaw --sctl $CLIENT_PROG" 

# Dynamic data deps, ILP, static control deps, no early spawns
OPTS[3]="--dep-file=$DEP_FILE --draw --dwar --dwaw --sctl --para-non-calls $CLIENT_PROG" 

# Dynamic data deps, TLP, static control deps, early spawns. Base Case
OPTS[4]="--dep-file=$DEP_FILE --draw --dwar --dwaw --sctl --early-spawns $CLIENT_PROG" 

# Go to working dir
cd $WORKDIR
# Run initial run, to gather data and generate static deps file
embla $FIRST_OPTS
/home/jchm2/Work/embla/postprocess/makedeps.sh -d $TRACE_FILE -c $EDGES_FILE -o $DEP_FILE
# Run the other ones
for i in ${!OPTS[*]}
do
    embla "${OPTS[i]}"
done

# Copy all output into embla.log
cat $ERR_FILE >>embla.log

# Also output to spreadsheet
perl /home/jchm2/Work/embla/postprocess/parse-embla-output.pl $PROGNAME <$ERR_FILE >>$SPREADSHEET
