#! /bin/sh

set -ex

latex main
bibtex main
latex main
latex main
dvips -o main.ps main.dvi
dvipdf main.dvi
