#!/bin/sh

INFILE=$1
shift
STEM=`echo $INFILE | sed -e 's/\.cilk$//'`
CFILE=$STEM.c
OUTFILE=$STEM

sed -e 's/#include <cilk-lib.cilkh>//' $INFILE >$CFILE

/home/jchm2/temp/cilk-5.4.6-bin/bin/cilkc -static -g -O0 -cilk-span $INFILE -o $STEM-cilk $*

exec gcc -static -g -O0 -m32 -Dcilk= -Dspawn= -Dsync= -o $OUTFILE $CFILE $*
