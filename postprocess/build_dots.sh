#! /bin/sh

set -ex

for t in *.trace; do
  ./dep_graph_dot.py -vvvvvv $t
done

for d in *.dot; do
  dot -Tps -G8,10 -o$(basename $d .dot).ps $d
done
