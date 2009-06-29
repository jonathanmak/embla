# This script takes a control flow graph where each line is of the
# form <file fname fromline toline> and represents an edge in the CFG.
# It produces the control dependencies implied by this cfg.
# Each line has the form <file fname C line-that-depends line-it-depends-on>

cfgfile=$1
bindir=`dirname $0`
O=$IFS
IFS=$(echo -en "\n\b")

for sfile in `awk -F"\t" '{print $1}' $cfgfile | sort -u`; do
  for sfname in `awk -F"\t" -v file=$sfile '$1==file {print $2}' $cfgfile | sort -u`
  do
     cat $cfgfile \
     | awk -F"\t" -v fil=$sfile -v fn=$sfname '$1==fil && $2==fn {print $3 "\t" $4}' \
     | awk -F"\t" -f $bindir/dominance.awk -v cda=1 \
     | sort -n \
     | awk -v fil=$sfile -v fn=$sfname '{print fil "\t" fn "\tC\t" $1 "\t" $2}' \
     | cat
  done
done

IFS=$O
