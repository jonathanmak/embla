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
  usevectors=0;
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
      # printf "%2d -> %2d\n", srcLNum, dstLNum > "/dev/stderr";
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

  if( usevectors ) {
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
  } else {
    for( i=srcmax; i>=0; i-- ) {
      for( j=srcmax; j>0; j-- ) {
        for( k=0; i*k<=srcmax && j*k<=srcmax; k++ ) {
          slope[ i*k, j*k] = sprintf( "(%d,%d)",  i,  j );
          slope[ i*k,-j*k] = sprintf( "(%d,%d)",  i, -j );
          slope[-i*k, j*k] = sprintf( "(%d,%d)", -i,  j );
          slope[-i*k,-j*k] = sprintf( "(%d,%d)", -i, -j );
        }
      }
    }
    slope[0,0] = "(0,0)";
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
        # Check which places are at all possible wrt slopes
        for( col=-30; col<=30; col++ ) {
          has_slope[col]=1;
          for( nn in component ) {
            if( (nn,"cand") in sstate && 
                ( (node,nn) in edges || (nn,node) in edges ) &&
                !( slope[sstate[nn,"cand"]-col,node-nn] != 0 ) )
            {
              has_slope[col] = 0;
            }
          }
        }
        # First: directly above or below a neighbour (furthest neighbour first)
        xidx = 0;
        for( off=srcmax; off>0; off-- ) {
          for( s=-1; s<2; s+=2 ) {
            j = node + s*off;
            # Now check if j is a previously placed neighbour of node
            if( (j,"cand") in sstate && has_slope[sstate[j,"cand"]] && 
                ( (j,node) in edges || (node,j) in edges ) &&
                !( j in marks ) ) 
            {
              # printf "N%d ", sstate[j,"cand"] > "/dev/stderr";
              xidx ++;
              sstate[node,xidx] = sstate[j,"cand"];
              marks[j]=xidx;
            }
          }
        }
        # Then: near a neighbour, except above or under a previously placed
        # non-neighbour
        # First, compute already placed component members that are not 
        # neighbours of node
        for( j=1; j<=srcmax; j++ ) {
          # Check if it has neighbors that are not yet placed
          has_neigh = 0;
          for( k=1; k<=srcmax; k++ ) {
            if( !( (k,"cand") in sstate ) && 
		( (j,k) in edges || (k,j) in edges ) ) {
              has_neigh = 1;
            }
          }
          if( has_neigh && (j,"cand") in sstate && !( j in marks ) ) {
            massert( j!=node, "cand set for node " j ); 
            avoid[sstate[j,"cand"]] = 1;
          }
        }
        burs = xidx;                # Neighbours are between 1 and burs
        for( off=1; off<6; off++ ) { # changed from 10
          for( jx=1; jx<=burs; jx++ ) {
            j = sstate[node,jx];
            for( a=1; a>=-1; a-=2 ) {
              cand = j+a*off
              if( has_slope[cand] && 
                  !( cand in marks ) && 
                  !( cand in avoid ) ) 
              {
                # printf "C%d ", cand > "/dev/stderr";
                xidx++;
                sstate[node,xidx] = cand;
                marks[cand] = xidx;
              }
            }
          }
        }
        # printf "\n" > "/dev/stderr";
        # Then: near a non-neighbour with a common neighbour,
        # except under a previously placed non-neighbour
#        for( off=1; off<10; off++ ) {
#          for( nc=node-1; nc>0; nc-- ) {
#            is_close = 0;
#            for( j=node+1; j<=srcmax; j++ ) {
#              if( ( (nc,j) in edges || (j,nc) in egdes ) &&
#                  ( (node,j) in edges || (j,node) in edges ) ) {
#                is_close = 1;
#              }
#            }
#            for( a=1; a>=-1; a-=2 ) {
#              cand = sstate[nc,"cand"]+a*off;
#              if( is_close && !( cand in marks ) && !( cand in avoid ) ) {
#                xidx++;
#                sstate[node,xidx] = cand;
#                marks[cand] = xidx;
#              }
#            }
#          }
#        }
        if( node!=i ) {
          # Then: anywhere close to zero, except above or under a 
          # previously placed non-neighbour
          for( off = 0; off < 20; off++ ) {
            for( a=1; a>=-1; a-=2 ) {
              cand = a*off;
              if( has_slope[cand] && 
                  !( cand in marks ) && 
                  !( cand in avoid ) ) 
              {
                # printf "X%d ", cand > "/dev/stderr";
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
              if( has_slope[cand] && !( cand in marks ) ) {
                # printf "W%d ", cand > "/dev/stderr";
                xidx++;
                sstate[node,xidx] = cand;
                marks[cand] = xidx;
              }
            }
          }
        } else {
          sstate[node,1] = 0;
        }
        delete marks;
        delete avoid;
        sstate[node,"candidx"] = 1;
        if( (node,1) in sstate ) {
          sstate[node,"cand"] = sstate[node,1];  # caches ss[n,ss[n,"ci"]]
        } else {
          delete sstate[node,"cand"];
        }
        delete sstate[node,1];
        # printf "\n" > "/dev/stderr"
        # Now try to place a node, backtracking if necessary
        # The new candidate is not yet checked
        do {
          if( (node,"cand") in sstate ) {
            # First: check if the implied candidate is ok
            # Slopes must exist for all placed neighbours ...
            # ... but not run over any other node in the process
            cand = sstate[node,"cand"];
            # printf "Checking %d at %d\n", node, cand > "/dev/stderr";
            ok = 1;
            for( off=1; off<srcmax; off++ ) {
              for( s=-1; s<2; s+=2 ) {
                pred = node + s*off;
                # check if we found a previously placed node
                if( (pred,"cand") in sstate ) {
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
              }
            }
            for( j=sstate[node,"pred"]; j!=0; j=sstate[j,"pred"] ) {
              for( k=sstate[node,"pred"]; k!=0; k=sstate[k,"pred"] ) {
                if( ( (j,k) in edges || (k,j) in edges ) &&
                    ( j<node && node<k || k<node && node<j ) ) 
                {
                  jc = sstate[j,"cand"];
                  kc = sstate[k,"cand"];
                  jtonode = slope[node-j,jc-cand];
                  jtok    = slope[k-j,   jc-kc];
                  if( jtonode==jtok ) {
                    ok=0;
                  }
                }
              }
            }
            delete predslopes;
          } else {
            ok = 0;
          }
          # Now we know if it was ok; if it wasn't, we need a new one
          if( ok != 1 ) {
            if( (node,"cand") in sstate ) {
              # printf "Bad: node %d at %d\n", node, cand > "/dev/stderr"
            }
            # we need to find another candidate, possibly backtracking
            nextidx = sstate[node,"candidx"] + 1;
            while( node != 0 && !( (node,nextidx) in sstate ) ) {
              delete sstate[node,"cand"];
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
            delete sstate[node,nextidx];
          } else {
            # Candidate is ok
            # printf "Good: node %d at %d\n", node, cand > "/dev/stderr"
          }
          # If the candidate was not ok we have found a new one
        } while ( ok != 1 );
        # We have placed some nodes in the component; now try one more
        # Heuristic: the furthest not already placed neighbor of node
        # or, if no neghbour exists, a neighbour of a recently placed node
        nn = 0;
        for( oldn=node; nn==0 && oldn!=0; oldn=sstate[oldn,"pred"] ) {
          searchwidth = 20;
          difficulty = 2*searchwidth+1;
          distance = 0;
          diff_node = 0;
          s=-1;
          for( lnum=1; lnum<=srcmax; lnum++ ) {
            if( lnum>oldn ) s=1;
            if( !( (lnum,"cand") in sstate ) && 
		( (lnum,oldn) in edges || (oldn,lnum) in edges ) )
	      {
                # We've found an unplaced neighbor
                this_diff = 0;
                for( coln=-searchwidth; coln<=searchwidth; coln++ ) {
                  if( slope[coln,lnum-oldn] != "" ) this_diff++;
                }
                if( this_diff<difficulty || 
                    this_diff==difficulty && s*(lnum-oldn)>distance )
		  { 
                    difficulty = this_diff;
                    distance   = s*(lnum-oldn);
                    nn         = lnum;
                  }
              }
          }
        }
        sstate[nn,"pred"] = node;
        node = nn;
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

  leftmargin = -depcolw*(maxdep+1)-25;

  printf "\\hrulefill\n\\[\n";
  printf "\\begin{picture}(420,%d)(%d,%d)\n\n", 
          10*srcmax, leftmargin, -10*srcmax;
 
  for( line=1; line<=srcmax; line++ ) {

    printf "\\put(%d,%d){\\makebox(15,10)[r]{\\it %d:}}%%\n",
      leftmargin, -10*line, line;

    printf "\\put(%d,%d){\\makebox(100,10)[l]{%s}}\n", 
           0, -10*line, src[line];

    if( line in social ) {
      printf "{\\color{black} \\dottedline{3}(%d,%d)(0,%d)}\n", 
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

        XoffN = 3*Xoff/dist;
        YoffN = 3*Yoff/dist;

        XoffNA = XoffN < 0 ? - XoffN : XoffN;
        YoffNA = YoffN < 0 ? - YoffN : YoffN;


        if( srcC==dstC ) {
          veclen = depcolw * (srcL<dstL ? dstL-srcL : srcL-dstL) - 2*YoffNA;
        } else {
          veclen = depcolw * (srcC<dstC ? dstC-srcC : srcC-dstC) - 2*XoffNA;
        }
        if( (dstL,srcL) in edges ) {
           Xtwoff = - YoffN/3;
           Ytwoff =   XoffN/3;
        } else {
           Xtwoff = 0;
           Ytwoff = 0;
        }

        Xstart =  depcolw*srcC - ncoff     + Xtwoff + XoffN;
        Ystart = -depcolw*srcL + depcolw/2 + Ytwoff + YoffN;
        Xend   =  Xstart + depcolw*Xoff - 2*XoffN;
        Yend   =  Ystart + depcolw*Yoff - 2*YoffN;
        
	if( usevectors ) {
          printf "\\put(%.1f,%.1f){\\color{%s}\\vector%s{%.1f}}\n",
                 Xstart, 
                 Ystart,
                 type[edges[srcL,dstL]],
                 slope[dstC-srcC,srcL-dstL], 
                 veclen;
          printf "{\\color{%s}\\dashline[100]{2}(%.1f,%.1f)(%.1f,%.1f)}\n",
                 type[edges[srcL,dstL]],
                 Xstart,
                 Ystart,
                 Xend,
                 Yend;
        } else {
          printf "{\\color{%s}\\dashline[200]{3}(%.1f,%.1f)(%.1f,%.1f)}%%\n",
                 type[edges[srcL,dstL]],
                 Xstart,
                 Ystart,
                 Xend,
                 Yend;

          XoffArr = XoffN/3;
          YoffArr = YoffN/3;

          Xcw  = Xend - XoffN - YoffArr;
          Ycw  = Yend - YoffN + XoffArr;
          Xccw = Xend - XoffN + YoffArr;
          Yccw = Yend - YoffN - XoffArr;

          printf "{\\color{%s}\\dashline[200]{3}(%.1f,%.1f)(%.1f,%.1f)}%%\n",
                 type[edges[srcL,dstL]],
                 Xend,
                 Yend,
                 Xcw,
                 Ycw;
          printf "{\\color{%s}\\dashline[200]{3}(%.1f,%.1f)(%.1f,%.1f)}%%\n",
                 type[edges[srcL,dstL]],
                 Xend,
                 Yend,
                 Xccw,
                 Yccw;
        }
      }
    }
  }

  printf "\\end{picture}\n\\]\n\\hrulefill\n"

}


function massert(cond,msg)
{
  if( !cond ) {
    print msg > "/dev/stderr"
  }
}
