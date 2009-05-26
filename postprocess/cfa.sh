# This script takes a control flow graph where each line is of the
# form <file fname fromline toline> and represents an edge in the CFG.
# It produces the control dependencies implied by this cfg.
# Each line has the form <file fname C line-that-depends line-it-depends-on>

cfgfile=$1
bindir=`dirname $0`

for sfile in `awk '{print $1}' $cfgfile | sort -u`; do
  for sfname in `awk -v file=$sfile '$1==file {print $2}' $cfgfile | sort -u`
  do
     cat $cfgfile \
     | awk -v fil=$sfile -v fn=$sfname '$1==fil && $2==fn {print $3 " " $4}' \
     | awk -f $bindir/dominance.awk -v cda=1 \
     | sort -n \
     | awk -v fil=$sfile -v fn=$sfname '{print fil " " fn " C " $1 " " $2}' \
     | cat
  done
done
