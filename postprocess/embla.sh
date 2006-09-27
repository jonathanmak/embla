#! /bin/sh
$EMBLAHOME/valgrind/install_dir/bin/valgrind --tool=embla \
  --trace-file=$1.trace $*
awk -f $EMBLAHOME/postprocess/deplist.awk $1.c $1.trace > $1.deplist.tex
awk -f $EMBLAHOME/postprocess/depgraph.awk $1.c $1.trace > $1.depgraph.tex
