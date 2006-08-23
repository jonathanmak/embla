#! /bin/sh
$EMBLAHOME/valgrind/install_dir/bin/valgrind --tool=embla $*
$EMBLAHOME/postprocess/sorttrace embla.trace > $1.trace
awk -f $EMBLAHOME/postprocess/deplist.awk $1.c $1.trace > $1.deplist.tex
awk -f $EMBLAHOME/postprocess/depgraph.awk $1.c $1.trace > $1.depgraph.tex
