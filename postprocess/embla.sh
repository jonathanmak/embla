#! /bin/sh

EMBLA_BIN=/home/jchm2/temp/embla-bin
EMBLA_HOME=/home/jchm2/Work/embla
DEPLIST_TEX=$1.deplist.tex
DEPGRAPH_TEX=$1.depgraph.tex

$EMBLA_BIN/bin/valgrind --tool=embla --trace-file=$1.trace $*

cat <<EOF >$DEPLIST_TEX
\documentclass{article}
\usepackage{color}
\usepackage{epic}
\usepackage{verbdef}
\begin{document}
EOF

cat <<EOF >$DEPGRAPH_TEX
\documentclass{article}
\usepackage{color}
\usepackage{epic}
\usepackage{verbdef}
\begin{document}
EOF

awk -f $EMBLA_HOME/postprocess/deplist.awk $1.c $1.trace >> $DEPLIST_TEX
awk -f $EMBLA_HOME/postprocess/depgraph.awk $1.c $1.trace >> $DEPGRAPH_TEX

cat <<EOF >>$DEPLIST_TEX
\end{document}
EOF

cat <<EOF >>$DEPGRAPH_TEX
\end{document}
EOF

pdflatex $DEPLIST_TEX
pdflatex $DEPGRAPH_TEX

