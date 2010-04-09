#!/bin/sh

VALGRIND_DIR=/home/jchm2/Work/embla/valgrind
EMBLA_DIR=$VALGRIND_DIR/embla
INC_DIR=$VALGRIND_DIR/include
CUR_DIR=`dirname $0`

exec $CUR_DIR/build.sh -I$EMBLA_DIR -I$INC_DIR $*
