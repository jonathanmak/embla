# This script takes a control flow graph where each line is of the
# form <file fname fromline toline> and represents an edge in the CFG.
# From this it produces a description of the loops in the program.
# Each line is of the form <file fname loopid parent exit header line ...>
# where <loopid> is a function-unique identifier of the loop,
#       <parent> is the loopid of the immediately enclosing loop,
#       <exit> is the loop exit line,
#       <header> is the loop header (that is, entry), and finally
#       <line> ... are the lines on the loop in addition to <header>

cfgfile=$1
bindir=`dirname $0`

for sfile in `awk '{print $1}' $cfgfile | sort -u`; do
  for sfname in `awk -v file=$sfile '$1==file {print $2}' $cfgfile | sort -u`
  do
     cat $cfgfile \
     | awk -v fil=$sfile -v fn=$sfname '$1==fil && $2==fn {print $3 " " $4}' \
     | cat > $cfgfile.TEMP 
     cat $cfgfile.TEMP \
     | awk -f $bindir/dominance.awk -v cda=0 \
     | $bindir/loops $cfgfile.TEMP \
     | awk -v fil=$sfile -v fn=$sfname '{print fil " " fn " " $0}' \
     | cat 
  done
done
rm $cfgfile.TEMP

