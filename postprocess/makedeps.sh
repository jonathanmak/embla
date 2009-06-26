#!/bin/sh

datafile="embla.trace"
controlfile="embla.edges"
outfile="embla.deps"
loopfile="embla.loops"

while getopts 'd:c:o:l:' OPTION
do
  case $OPTION in 
  d) datafile="$OPTARG"
      ;;
  c) controlfile="$OPTARG"
      ;;
  o) outfile="$OPTARG"
      ;;
  l) loopfile="$OPTARG"
      ;;
  ?) printf "Usage: %s [-d data file (default:embla.trace)] [-c control file (default:embla.edges)] [-o output file (default:embla.deps)] [-l loop file (default:embla.loops)]\n" $(basename $0) >&2
      exit 2
      ;;
  esac
done

bindir=`dirname $0`

$bindir/cfa.sh $controlfile >$outfile

awk -f $bindir/datadeps.awk $datafile >>$outfile

# $bindir/loops.sh $controlfile >$loopfile
