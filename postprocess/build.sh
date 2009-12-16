#!/bin/sh

INFILE=`echo $* | awk '{print $NF}'`

OUTFILE=`echo $INFILE | sed -e 's/\(.*\)\.c$/\1/'`

echo "gcc -static -g -O0 -m32 -o $OUTFILE $*"
exec gcc -static -g -O0 -m32 -o $OUTFILE $*
