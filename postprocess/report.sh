#! /bin/sh

tracebase=$1
dirname=$2 # fname=$2
# fromline=$3
# toline=$4
basefname=`basename $fname`

rundir=`pwd`
cd $dirname

tracename=${rundir}/${tracebase}.trace
repname=${rundir}/${tracebase}.tex
tmpname=${rundir}/${tracebase}_tmp

ptrname=${tracename}_${basefname}_${fromline}-${toline}
pfname=${basefname}_${fromline}-${toline}



echo '\documentclass{article} \usepackage{color}' > $repname
echo '\usepackage{epic} \usepackage{verbdef}' >> $repname
echo '\begin{document}' >> $repname

for fname in `awk -f findfile.awk $tracename`; do
  awk -f ${rundir}/findfun.awk $fname > $tmpname.funs
  awk "{print $0 > ${tmpname}_

  awk -f focus.awk -v theFile=$basefname -v fromLine=$fromline\
                   -v toLine=$toline $tracename > $ptrname

  awk "FNR>=$fromline && FNR<=$toline" $fname > $pfname

  awk -f depgraph.awk $pfname $ptrname >> $repname

done

echo '\end{document}' >> $repname


