#!/bin/sh

INFILE=$1
OUTFILE=`echo $INFILE | sed -e 's/\(.*\)\.c$/\1/'`

exec gcc -static -g -O0 -m32 -o $OUTFILE $INFILE
