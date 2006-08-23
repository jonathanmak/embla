BEGIN {
  type[1]="cyan";
  type[2]="blue";
  type[3]="green";
  type[4]="red";
  ndep=0;
  maxdep=0;
  depcolw=10;
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

  if( srcTag=="h" && dstTag=="h" ) {
    depty = 1;
  } else {
    depty = $6>0 ? 4 : 
            $7>0 ? 3 : 
                   2 ;
  }

  if( srcLNum != dstLNum ) {
    if( edges[srcLNum,dstLNum] < depty ) {
      edges[srcLNum,dstLNum] = depty;
    }
    social[srcLNum] = 1;
    social[dstLNum] = 1;
  }

}
END {

  /* Construct slope table */

  latexslopes[0,1] = 1;
  latexslopes[1,0] = 1;
  latexslopes[1,1] = 1;
  latexslopes[1,2] = 1;
  latexslopes[1,3] = 1;
  latexslopes[1,4] = 1;
  latexslopes[2,1] = 1;
  latexslopes[2,3] = 1;
  latexslopes[3,1] = 1;
  latexslopes[3,2] = 1;
  latexslopes[3,4] = 1;
  latexslopes[4,1] = 1;
  latexslopes[4,3] = 1;

  for( i=1; i<=srcmax; i++ ) {
    for( x=0; x<5; x++ ) {
      for( y=0; y<5; y++ ) {
        if( (x,y) in latexslopes ) {
          slope[ i*x, i*y] = sprintf( "(%d,%d)",  x,  y );
          slope[ i*x,-i*y] = sprintf( "(%d,%d)",  x, -y );
          slope[-i*x, i*y] = sprintf( "(%d,%d)", -x,  y );
          slope[-i*x,-i*y] = sprintf( "(%d,%d)", -x, -y );
        }
      }
    }
  }

  /* Construct and place each component */

  for( i=0; i<=srcmax; i++ ) {
    if( i in social && !(i in haveseen) ) {
      /* start building a new connected component */
      haveseen[i] = 1;
      component[i] = 1;
      do {
        added=0;
        for( j=i; j<=srcmax; j++ ) {
          for( k=i; k<=srcmax; k++ ) { 
            if( (j,k) in edges ) {
              if( j in component && ! (k in component) ) {
                component[k] = 1;
                haveseen[k]=1;
                added=1;
              } else if( !( j in component ) && k in component ) {
                component[j] = 1;
                haveseen[j] = 1;
                added = 1;
              }
            }
          }
        }
      } while (added);

      /* 'component' now contains all nodes in this component */
      /* now loop over the nodes in this component, in order */

      {
        # printf "Component found: " > "/dev/stderr"
        for( dn in component ) {
          # printf "%d ", dn > "/dev/stderr"
        }
        # printf "\n" > "/dev/stderr"
      }

      node = i;
      sstate[node,"pred"]=0;
      do {
        # printf "Candidates for node %d: ", node > "/dev/stderr"
        # Compute a good candidate order in sstate
        # First: directly below a neighbour (furthest neighbour first)
        xidx = 0;
        # for( j=node-1; j>0; j-- ) {
        for( j=1; j<node; j++ ) {
          if( (j,node) in edges || (node,j) in edges ) {
            xidx ++;
            sstate[node,xidx] = sstate[j,"cand"];
            marks[j]=xidx;
          }
        }
        # Then: near a neighbour, except under a previously placed
        # non-neighbour
        for( j=1; j<node; j++ ) {
          if( j in component && !( j in marks ) ) {
            avoid[sstate[j,"cand"]] = 1;
          }
        }
        burs = xidx;                # Neighbours are between 1 and burs
        for( off=1; off<10; off++ ) {
          for( j=1; j<=burs; j++ ) {
            for( a=1; a>=-1; a-=2 ) {
              cand = sstate[j,"cand"]+a*off
              if( !( cand in marks ) && !( cand in avoid ) ) {
                xidx++;
                sstate[node,xidx] = cand;
                marks[cand] = xidx;
              }
            }
          }
        }
        # Then: anywhere close to zero, except under a previously placed
        # non-neighbour
        for( off = 0; off < 20; off++ ) {
          for( a=1; a>=-1; a-=2 ) {
            cand = a*off;
            if( !( cand in marks ) && !( cand in avoid ) ) {
              xidx++;
              sstate[node,xidx] = cand;
              marks[cand] = xidx;
            }
          }
        }
        # Then: anywhere close to zero
        for( off = 0; off < 20; off++ ) {
          for( a=1; a>=-1; a-=2 ) {
            cand = a*off;
            if( !( cand in marks ) ) {
              xidx++;
              sstate[node,xidx] = cand;
              marks[cand] = xidx;
            }
          }
        }
        delete marks;
        delete avoid;
        sstate[node,"candidx"] = 1;
        sstate[node,"cand"] = sstate[node,1];  # caches ss[n,ss[n,"ci"]]
        # Now try to place a node, backtracking if necessary
        do {
          # First: check if the implied candidate is ok
          # Slopes must exist for all placed neighbours ...
          # ... but not run over any other node in the process
          cand = sstate[node,"cand"];
          # printf "Checking %d at %d\n", node, cand > "/dev/stderr";
          ok = 1;
          for( pred=sstate[node,"pred"]; pred!=0; pred=sstate[pred,"pred"] ) {
            predcand = sstate[pred,"cand"];
            # printf "  %d at %d", pred, predcand > "/dev/stderr";
            if( (node,pred) in edges || (pred,node) in edges ) { 
               is_nb = 1;
            } else {
               is_nb = 0;
            }
            n_slope = slope[predcand-cand,node-pred];
            if( is_nb && ( n_slope == "" || n_slope in predslopes ) ) {
              ok = 0;
            }
            predslopes[n_slope] = 1;
            # printf "\n" > "/dev/stderr";
          }
          delete predslopes;
          # Now we know if it was ok; if it wasn't, we need a new one
          if( ok != 1 ) {
            # printf "Bad: node %d at %d\n", node, cand > "/dev/stderr"
            # we need to find another candidate, possibly backtracking
            nextidx = sstate[node,"candidx"] + 1;
            while( node != 0 && !( (node,nextidx) in sstate ) ) {
              node = sstate[node,"pred"];
              nextidx = sstate[node,"candidx"] + 1;
	    }
            # did we find another, or have we failed?
            if( node == 0 ) {
              print "Could not place nodes!" > "/dev/stderr";
              exit (1);
            }
            # We did find a new candidate
            sstate[node,"candidx"] = nextidx;
            sstate[node,"cand"] = sstate[node,nextidx];
          } else {
            # Candidate is ok
            # printf "Good: node %d at %d\n", node, cand > "/dev/stderr"
          }
          # If the candidate was not ok we have found a new one
        } while ( ok != 1 );
        # We have placed some nodes in the component; now try one more
        nextnode = node;
        do {
          nextnode++;
        } while( nextnode <= srcmax && !( nextnode in component ) );
        sstate[nextnode,"pred"] = node;
        node = nextnode;
      } while( node in component );
      # We have placed all of the nodes in this component. Hallelujah!
      # Now we place the component itself
      # First check its size
      cminline = -1;
      cmincol = 1000;
      cmaxcol = -1000;
      for( node=1; node<=srcmax; node++ ) {
        if( node in component ) {
          cand = sstate[node,"cand"];
          if( cminline==-1 ) cminline = node;
                             cmaxline = node;
          if( cand<cmincol ) cmincol = cand;
          if( cand>cmaxcol ) cmaxcol = cand;
        }
      }
      # Now we know the size of the layout of the component
      # Place it in the rightmost possible spot
      compwidth = cmaxcol - cmincol + 1;
      leftocc = 1;
      currcol = 0;
      while( leftocc-currcol < compwidth+1 ) {
        for( line=cminline; line<=cmaxline; line++ ) {
          if( (line,currcol) in usedpos ) {
            leftocc = currcol;
          }
        }
        currcol--;
      }
      if( maxdep < - (currcol+1) ) {
         maxdep = - (currcol+1);
      }
      # Mark the area found as allocated
      for( col=currcol+1; col<=currcol+compwidth; col++ ) {
        for( line=cminline; line<=cmaxline; line++ ) {
          usedpos[line,col] = 1;
        }
      }
      # Compute mapping from old to new coordinates and record the component
      delta = currcol + 1 - cmincol;
      for( line=1; line<=srcmax; line++ ) {
        if( line in component ) {
          placement[line] = sstate[line,"cand"]+delta;
        }
      }
      delete sstate;
      delete component;
    }
  }


  ncoff = 5+depcolw/2;


  printf "\\hrulefill\n\\[\n";
  printf "\\begin{picture}(420,%d)(%d,%d)\n\n", 
          10*srcmax, -depcolw*(maxdep+1), -10*srcmax;
 
  for( line=1; line<=srcmax; line++ ) {
    used = 1;
    for( row=line; row<=line+1; row++ ) {
      oneused = 0;
      for( col=0; col>=-maxdep; col-- ) {
        oneused = oneused || usedpos[line,col];
      }
      used = used && oneused;
    }
    printf "\\put(%d,%d){\\makebox(100,10)[l]{%s}}\n", 
           0, -10*line, src[line];
    # if( line != srcmax && used ) {
    if( line in social ) {
      printf "{\\color{black} \\dottedline{3}(%d,%d)(-0,%d)}\n", 
        # -depcolw*(maxdep+1.5),
        -10, 
        -10*line+5, -10*line+5
    }
  }
  printf "\n";

  for( line in social ) {
    printf "\\put(%d,%d){\\color{black}\\circle*{2}}\n", 
           10*placement[line]-ncoff, -10*line+5;
  }

  for( srcL=1; srcL <= srcmax; srcL++ ) {
    for( dstL=1; dstL <= srcmax; dstL++ ) {
      if( (srcL,dstL) in edges ) {
        srcC = placement[srcL];
        dstC = placement[dstL];
        
        Xoff = dstC-srcC;
        Yoff = srcL-dstL;
        dist = sqrt( Xoff*Xoff + Yoff*Yoff );

        XoffN = 2*Xoff/dist;
        YoffN = 2*Yoff/dist;

        XoffNA = XoffN < 0 ? - XoffN : XoffN;
        YoffNA = YoffN < 0 ? - YoffN : YoffN;


        if( srcC==dstC ) {
          veclen = depcolw * (srcL<dstL ? dstL-srcL : srcL-dstL) - 2*YoffNA;
        } else {
          veclen = depcolw * (srcC<dstC ? dstC-srcC : srcC-dstC) - 2*XoffNA;
        }
        if( (dstL,srcL) in edges ) {
           Xtwoff = - YoffN/2;
           Ytwoff = - XoffN/2;
        } else {
           Xtwoff = 0;
           Ytwoff = 0;
        }
             
        printf "\\put(%d,%d){\\color{%s}\\vector%s{%d}}\n",
               depcolw*srcC-ncoff+Xtwoff + XoffN, 
               -depcolw*srcL+depcolw/2 + Ytwoff + YoffN,
               type[edges[srcL,dstL]],
               slope[dstC-srcC,srcL-dstL], 
               veclen;
      }
    }
  }

  printf "\\end{picture}\n\\]\n\\hrulefill\n"

}
