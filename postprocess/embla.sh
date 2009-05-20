#! /bin/sh

eval EXEC=\$$#
EMBLA_BIN=/home/jchm2/temp/embla-bin
EMBLA_HOME=/home/jchm2/Work/embla
OUTPUT=$EXEC.out
TRACE_DUMP=$EXEC.trace_dump
DEPLIST_TEX=$EXEC.deplist.tex
DEPGRAPH_TEX=$EXEC.depgraph.tex

#echo $EXEC

$EMBLA_BIN/bin/valgrind --tool=embla --trace-file=$EXEC.trace $* >$OUTPUT 2>&1
grep "^[0-9]\+: " $OUTPUT >$TRACE_DUMP

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

awk -f $EMBLA_HOME/postprocess/deplist.awk $EXEC.c $EXEC.trace >> $DEPLIST_TEX
awk -f $EMBLA_HOME/postprocess/depgraph.awk $EXEC.c $EXEC.trace >> $DEPGRAPH_TEX

cat <<EOF >>$DEPLIST_TEX
\end{document}
EOF

cat <<EOF >>$DEPGRAPH_TEX
\end{document}
EOF

pdflatex $DEPLIST_TEX
pdflatex $DEPGRAPH_TEX

