$1==theFile {
  srcLine = $5;
  dstLine = $4;
  region  = $3;
  srcLLen = length(srcLine);
  dstLLen = length(dstLine);
  srcTag = substr(srcLine,srcLLen);
  dstTag = substr(dstLine,dstLLen);
  if( srcTag=="c" || srcTag=="h" ) {
    srcLNum = substr(srcLine,1,srcLLen-1);
  } else {
    srcLNum = srcLine;
    srcTag="";
  }
  if( dstTag=="c" || dstTag=="h" ) {
    dstLNum = substr(dstLine,1,dstLLen-1);
  } else {
    dstLNum = dstLine;
    dstTag="";
  }
  if( srcLNum >= fromLine && srcLNum <= toLine ) {
     printf "%s %s %s %d%s %d%s %d %d %d\n",
            $1, $2, $3, 
            dstLNum-fromLine+1, dstTag, 
            srcLNum-fromLine+1, srcTag,
            $6, $7, $8;
  }
}
