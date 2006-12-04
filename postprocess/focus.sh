#! /bin/sh

tracename=$1
fname=$2
fromline=$3
toline=$4
basefname=`basename $fname`

ptrname=${tracename}_${basefname}_${fromline}-${toline}
pfname=${basefname}_${fromline}-${toline}

echo $tracename $fname $fromline $toline $basefname

awk -f focus.awk -v theFile=$basefname -v fromLine=$fromline -v toLine=$toline $tracename > $ptrname

echo '\documentclass{article} \usepackage{color} \usepackage{epic} \usepackage{verbdef} \begin{document}' > $ptrname.depgraph.tex

awk "FNR>=$fromline && FNR<=$toline" $fname > $pfname

awk -f depgraph.awk $pfname $ptrname >> $ptrname.depgraph.tex

echo '\end{document}' >> $ptrname.depgraph.tex


