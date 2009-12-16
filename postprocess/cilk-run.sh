#!/bin/sh

INFILE=$1
shift
STEM=`echo $INFILE | sed -e 's/\-cilk$//'`
BINDIR=`dirname $0`
DATE=`date -u +"%d %b %Y %k:%M:%S"`

for ((x=1; x<=20; x++))
do
  for i in 1 2 4
  do
    $INFILE --stats 1 --nproc $i
  done
done 2>&1 | $BINDIR/parse-cilk-output.pl $STEM "$DATE"

