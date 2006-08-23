BEGIN {
 type[6]="red";
 type[7]="green";
 type[8]="blue";
 ndep=0;
 maxdep=0;
 depcolw=6;
 cmd["c"]="\\circle*{2}\\hspace{-2\\unitlength}\\circle{4}";
 cmd["h"]="\\circle{4}";
 cmd["."]="\\circle*{2}";

 enc["0"]="a";
 enc["1"]="b";
 enc["2"]="c";
 enc["3"]="d";
 enc["4"]="e";
 enc["5"]="f";
 enc["6"]="g";
 enc["7"]="h";
 enc["8"]="i";
 enc["9"]="j";
}
ARGIND==1 {
  srcmax=FNR;
  numstr = FNR "";
  prstr = "";
  for( j=1; j<=length(numstr); j++ ) {
    prstr = prstr enc[substr(numstr, j, 1)];
  }
  prstr = "\\jkffx" prstr;
  printf "\\verbdef%s$%s $\n", prstr, $0;
  src[FNR]=prstr;
}
$1!="???" && ARGIND==2 && $3!="f" && (onlyF=="" || onlyF==$2) {
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
   srcTag=".";
 }
 if( dstTag=="c" || dstTag=="h" ) {
   dstLNum = substr(dstLine,1,dstLLen-1);
 } else {
   dstLNum = dstLine;
   dstTag=".";
 }

 srcY = -10 * srcLNum +5;
 dstY = -10 * dstLNum +5;

 if( srcLNum < dstLNum ) {
   vslope = -1;
   upperY = srcY;
   leastL = srcLNum;
   greatL = dstLNum;
 } else {
   vslope=1;
   upperY = dstY;
   leastL = dstLNum;
   greatL = srcLNum;
 }

 if( region=="o" ) {
   regll = 4;
 } else {
   regll = 0;
 }

 sepY = srcY<dstY ? (dstY-srcY)-4 : (srcY-dstY)-4;

 srcCmd = cmd[srcTag];
 dstCmd = cmd[dstTag];

 for( ti=6; ti<9; ti++ ) {
   if( $ti > 0 ) {
     ndep++;
     depind = 0;
     do {
       depind++;
       collision = 0;
       for( row=leastL; row<=greatL; row++ ) {
         collision = collision || depslots[depind,row];
       }
     } while ( collision );
     for( row=leastL; row<=greatL; row++ ) {
       depslots[depind,row] = 1;
     }
     if( depind > maxdep ) {
       maxdep = depind;
     }

     depX = -depcolw*depind;


     if( srcY != dstY ) {
       depstr[ndep]=sprintf( \
        "\\color{%s}\n"      \
        "\\put(%d,%d){%s}\n" \
        "\\put(%d,%d){\\vector(0,%d){%d}}\n" \
        "\\put(%d,%d){\\linethickness{0.7pt}\\line(1,0){%d}}\n" \
        "\\put(%d,%d){%s}\n\n",
        type[ti],
        depX, srcY, srcCmd,
        depX, srcY+vslope*2, vslope, sepY,
        depX-2, upperY-2.5, regll,
        depX, dstY, dstCmd);
     } else {
       depstr[ndep]=sprintf( \
        "\\color{%s}\n"      \
        "\\put(%d,%d){%s}\n" \
        "\\put(%d,%d){\\linethickness{0.7pt}\\line(1,0){%d}}\n" \
        "\\put(%d,%d){%s}\n\n",
        type[ti],
        depX, srcY+2.5, srcCmd,
        depX-2, srcY+0.5, regll,
        depX, dstY-2.5, dstCmd);
     }
   }
 }
}
END {

 printf "\\hrulefill\n\\[\n";
 printf "\\begin{picture}(420,%d)(%d,%d)\n\n", 
         10*srcmax, -depcolw*(maxdep+1), -10*srcmax;
 
 for( i=0; i<=srcmax; i++ ) {
   used = 1;
   for( row=i; row<=i+1; row++ ) {
     oneused = 0;
     for( col=1; col<=maxdep; col++ ) {
       oneused = oneused || depslots[col,row];
     }
     used = used && oneused;
   }
   printf "\\put(%d,%d){\\makebox(100,10)[l]{%s}}\n", 
          0, -10*i, src[i];
   if( i != srcmax && used ) {
     printf "{\\color{black} \\dottedline{3}(%d,%d)(-5,%d)}", 
       -depcolw*(maxdep+0.5), -10*i, -10*i
   }
 }
 printf "\n";

 for( i=1; i<=ndep; i++ ) {
   printf "%s", depstr[i];
 }

 printf "\\end{picture}\n\\]\n\\hrulefill\n"

}
