BEGIN {
 FS = "\t"
 FLAG_RAW      =  "T"
 FLAG_WAR      =  "A"
 FLAG_WAW      =  "O"
 FLAG_CTL      =  "C"
 FLAG_HIDDEN   =  "H"
 FLAG_STACK    =  "S"
}

{
  depType = "";
  if ($6) depType = depType FLAG_RAW;
  if ($7) depType = depType FLAG_WAR;
  if ($8) depType = depType FLAG_WAW;
  if ($4 ~ /h/ && $5 ~ /h/) depType = depType FLAG_HIDDEN;
  if ($3 == "s") depType = depType FLAG_STACK;
  printf "%s\t%s\t%s\t%u\t%u\n", $1, $2, depType, $4 + 0, $5 + 0
}
