{
  if( cda != 1 ) {
    src=$1;
    dst=$2;
  } else {
    src=$2;
    dst=$1
  }

  if( seen[src] != 1 ) {
    seen[src] = 1;
    succ[src,"N"] = 0;
  }
  if( seen[dst] != 1 ) {
    seen[dst] = 1;
    pred[dst,"N"] = 0;
  }

  succ[src,"N"]++;
  succ[src,succ[src,"N"]] = dst;

  pred[dst,"N"]++;
  pred[dst,pred[dst,"N"]] = src;
}

END {
  lmin = 1000000;
  lmax = 0;
  wp=0;
  for( l in seen ) {
    if( l<lmin ) lmin = l;
    if( l>lmax ) lmax = l;
    if( pred[l,"N"]==0 ) {
      # An initial node in the graph
      dom[l,l] = 1;
      wp++;
      wl[wp] = l;
      in_w[l] = 1;
    } else {
      for( j in seen ) {
        dom[j,l] = 1;
      }
    }
  }

  while( wp>0 ) {
    l = wl[wp];
    wp--;
    in_w[l] = 0;
    for( i = 1; i <= succ[l,"N"]; i++ ) {
      s = succ[l,i];
      c = 0;
      for( j in seen ) {
        if( j!=s && dom[j,s]==1 && dom[j,l]!=1 ) {
          dom[j,s] = 0;
          c = 1;
        }
      }
      if( c==1 && in_w[s]!=1 ) {
        wp++;
        wl[wp] = s;
        in_w[s] = 1;
      }
    }
  }

  # Computing control dependencies
  # Here the input CFG is the reverse of the real CFG
  
  if( cda==1 ) {
    # Perform control dependence analysis
    for( j in seen ) {
      # now find the nodes that j control depends on
      # by looping over all nodes that j postdominates
      # because of the reversal, it is those nodes that j dominates
      # printf "%d", j;
      cdep[j,"N"] = 0;
      for( n in seen ) {
        if( dom[j,n]==1 ) {
          # Now check all predecessors of n to see which ones are not
          # postdominated by j
          # Because of the reversal, predecessors are successors and 
          # postdominance is dominance
          for( kk=1; kk<=succ[n,"N"]; kk++ ) {
            k = succ[n,kk];
            if( dom[j,k]!=1 && dep[j,k]!=1 ) {
              # Is not postdominated by j and not seen
              # cdep[j,"N"]++;
              # cdep[j,cdep[j,"N"]] = k;
              dep[j,k] = 1;
              printf "%d %d\n", j, k;
            }
          }
        }
      }
      # printf "\n";
    }

  } else {
    # Perform loop analysis

    # Computing and printing loops 
    for( j in seen ) {
      for( kk=1; kk<=succ[j,"N"]; kk++ ) {
        k = succ[j,kk];
        if( dom[k,j]==1 ) {    # Found a back edge j->k
          delete in_w;
          in_w[k] = 1;
          printf "%d", k;
          wp = 1;
          wl[wp] = j;
          in_w[j] = 1;
          while( wp>0 ) {
            l = wl[wp];
            wp--;
            printf " %d", l;
            for( m=1; m<=pred[l,"N"]; m++ ) {
              p = pred[l,m];
              if( in_w[p] != 1 ) {
                wp++;
                wl[wp] = p;
                in_w[p] = 1;
              }
            }
          }
          printf "\n";       
        }
      }
    }

    printf "\n";

    # Printing the dominator relation
    for( j=1; j<=lmax; j++ ) {
      if( j in seen ) {
        printf "%4d:", j;
        for( k=1; k<=lmax; k++ ) {
          if( k in seen ) {
            if( dom[j,k] == 1 ) {
              printf " %d", k;
            }
          }
        }
        printf "\n";
      }
    }
    }
}
