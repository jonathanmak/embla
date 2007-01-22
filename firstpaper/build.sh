#! /bin/sh

set -ex

papers=short # main

for paper in ${papers}; do
  latex ${paper}
  bibtex ${paper}
  latex ${paper}
  latex ${paper}
  dvips -o ${paper}.ps ${paper}.dvi
  dvipdf ${paper}.dvi
done
