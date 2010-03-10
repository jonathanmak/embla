#include <stdio.h>
#include <stdlib.h>

#define MANY_CHARS 1000000

#define newt(t) ((t*) acheck( malloc( sizeof( t ) ) ))

#define newts(t,n) ((t*) acheck( malloc( n * sizeof( t ) ) ))

char buf[MANY_CHARS];

void *acheck( void *arg )
{
  if( arg == NULL ) {
    fprintf( stderr, "acheck failed\n" );
    exit( 1 );
    return NULL;
  }
  return arg;
}


typedef struct _Line {
  int line;
  struct _Line *next;
} Line;

typedef struct _Loop {
  struct _Loop *leader;
  Line         *lines;
  int           nlines;
  int           id;
  int           exit; // A line number
  struct _Loop *next;
} Loop;

typedef struct _LoopList {
  Loop             *loop;
  struct _LoopList *next;
} LoopList;

typedef struct _CFG {
  int          from,to;
  struct _CFG *next;
} CFG;

static CFG *readCFG( FILE *cfg_file )
{
  int from, to;
  CFG *cfg = NULL;

  while( fscanf( cfg_file, "%d %d \n", &from, &to ) > 0 ) {
    CFG *new_cfg = newt(CFG);
    new_cfg->from = from;
    new_cfg->to   = to;
    new_cfg->next = cfg;
    cfg = new_cfg;
  }
  return cfg;
}

static Loop *readLoops( )
{
  Loop *loop = NULL;
  int i=1;

  while( fgets( buf, MANY_CHARS-1, stdin ) != NULL ) {
    Loop *h = newt(Loop);
    Line **last = &(h->lines);
    int n, m=0, s=0;

    h->leader = h;
    h->nlines = 0;
    h->id     = i++;
    h->exit   = 0;
    h->next   = loop;
    loop = h;

    while( sscanf( buf+s, "%d%n", &n, &m ) != 0 && m > 0 ) {
      Line *l = newt(Line);
      h->nlines ++;
      *last = l;
      l->line = n;
      last = &(l->next);
      s += m;
      m = 0;
    }
    *last = NULL;
  }

  return loop;
}

static Loop *leader( Loop *l )
{
  if( l->leader == l ) {
    return l;
  } else {
    return leader(l->leader);
  }
}

static Loop *sortLoops( Loop *l )
{
  int n=0,i=0,j;
  Loop *t,*r,**a;

  for( t=l; t != NULL; t = t->next ) {
    n++;
  }

  a = newts(Loop *, n);

  for( t=l; t != NULL; t = t->next ) {
    a[i++] = t;
  }

  r = NULL;
  for( i = 0; i < n-1; i++ ) {
    for( j = 0; j < n-1-i; j++ ) {
      if( a[j]->nlines < a[j+1]->nlines ) { // Could be swapped
        t = a[j];
        a[j] = a[j+1];
        a[j+1] = t;
      }
    }
    a[n-1-i]->next = r;
    r = a[n-1-i];
  }
  a[0]->next = r;
  r = a[0];

  free(a);

  return r;
}

static int member( int n, Line *l )
{
  while( l != NULL ) {
    if( l->line == n ) {
      return 1;
    }
    l = l->next;
  }
  return 0;
}
  

static Loop *mergeLoops( Loop *l )
{
  Loop *t = l,*tt;
  Loop *r;

  while( t != NULL ) {
    Loop *q = t->next;

    while( q != NULL ) {
      // Now check overlap between t and q
      Line *a = t->lines;
      int nut=0, nuq=0;

      while( a != NULL ) {
        if( !member( a->line, q->lines ) ) {
          nut++;
        }
        a = a->next;
      }
      nuq = q->nlines - (t->nlines - nut);
      if( nuq > 0 && nut > 0 ) {
        leader(t)->leader = leader(q);
      }
      q = q->next;
    }
    t = t->next;
  }

  // Done with union find, now construct the merged loops

  r = NULL;

  for( t=l; t!=NULL; t=tt ) {
    tt = t->next;
    if( t->leader != t ) {
       Loop  *l = leader(t);
       Line *lt;
       Line *tmp;

       for( lt = t->lines; lt != NULL; lt=tmp ) {
         tmp = lt->next;
         if( !member( lt->line, l->lines ) ) {
           lt->next = l->lines->next; // Keep header as first line in list!
           l->lines->next = lt;
           l->nlines ++;
         }
       }
    } else {
      t->next = r;
      r = t;
    }
  }

  // Now sort on size

  r = sortLoops( r );

  return r;

}

static int insertOne( Loop *bigger, Loop *smaller )
{
  int sh = smaller->lines->line; // Header of smaller loop
  int nest_here = 0;

  // Loop over the loops in a header class, outmost first
  for( ; bigger != NULL; bigger = bigger->next ) {
    if( member( sh, bigger->lines ) ) {
      smaller->leader = bigger;
      nest_here = 1;
    } else {
      break;
    }
  }
  return nest_here;
}
 

static LoopList *insertLL( LoopList *list, Loop *loop )
{
  LoopList *tlist = list, *res = list, **old = &res, *new;

  // Loop over the header classes seen so far, biggest first
  while( tlist != NULL && tlist->loop->nlines > loop->nlines ) {
    insertOne( tlist->loop, loop );
    old = &(tlist->next);
    tlist = tlist->next;
  }
  new = newt(LoopList);
  new->loop = loop;
  new->next = tlist;
  *old = new;

  // Now check if we should place other loops inside the new nest

  while( tlist != NULL ) {
    Loop *tloop = tlist->loop;
    Line line;
    
    if( tloop->leader == tloop || tloop->leader == loop->leader ) {
      insertOne( loop, tlist->loop );
    }

    tlist = tlist->next;
  }

  return res;
}

static int singleExitLoop( Line **a, Loop *loop )
{
  Line *line;

  for( line = loop->lines; line != NULL; line = line->next ) {
    Line *succ;
    // Check for each successor whether it is in or out of the loop
    for( succ = a[line->line]; succ != NULL; succ = succ->next ) {
      if( !member( succ->line, loop->lines ) ) {
        // found an exit
        if( loop->exit == 0 ) {
          loop->exit = line->line;
        } else if( loop->exit != line->line ) {
          return 0;
        }
      }
    }
  }
  return 1;
}

static Line **makeCFGMap( CFG *cfg )
{
  int     max;
  Line  **a = NULL;
  CFG           *e;
  int            i;

  if( cfg == NULL ) {
    return NULL;
  }

  max = cfg->from;
  for( e = cfg; e != NULL; e = e->next ) {
    if( e->from > max ) {
      max = e->from;
    }
  }
  a = newts( Line *, max + 1 );
  for( i = 0; i < max+1; i++ ) {
    a[i] = NULL;
  }

  for( e = cfg; e != NULL; e = e->next ) {
    Line *l = newt(Line);
    int   ix = e->from;

    l->next = a[ix];
    l->line = e->to;
    a[ix] = l;
  }

  return a;
}


static Loop *filterUsingExits( Line **cfg, Loop *loops )
{
  Loop          *res = NULL, 
                *ls;

  if( cfg == NULL ) {
    return loops;
  }

  while( loops != NULL ) {
    if( singleExitLoop( cfg, loops ) ) {
      Loop *tmp = loops->next;

      loops->next = res;
      res = loops;
      loops = tmp;
    } else {
      loops = loops->next;
    }
  }

  ls = res;
  res = NULL;
  while( ls != NULL ) {
    Loop *tmp = ls->next;
    ls->next = res;
    res = ls;
    ls = tmp;
  }

  return res;

}



int main(int argc, char **argv)
{
  Loop *loops = readLoops( );
  Loop *tmp = loops;
  LoopList *inserted = NULL;
  CFG  *cfg = readCFG( fopen( argc > 1 ? argv[1] : "/dev/null", "r" ) );
  Line **a;

  while( 0 && cfg != NULL ) {
    printf( "%d %d\n", cfg->from, cfg->to );
    cfg = cfg->next;
  }

  a = makeCFGMap( cfg );

  while( tmp != NULL ) {
    int h = tmp->lines->line;
    Loop *acc = NULL,*tt;

    while( tmp != NULL && tmp->lines->line == h ) {
      tt = tmp->next;
      tmp->next = acc;
      acc = tmp;
      tmp = tt;
    }
    // now acc points to a reversed list of loops with same header

    acc = mergeLoops( acc );

    acc = filterUsingExits( a, acc );

    if (acc != NULL) {
      acc->leader = acc;
      for( tt = acc; tt->next != NULL; tt = tt->next ) {
        tt->next->leader = tt;
      }

      inserted = insertLL( inserted, acc );
    }
  }
    
  while( inserted != NULL ) {
    loops = inserted->loop;
    while( loops != NULL ) {
      Line *line = loops->lines;
      printf( "%d\t%d\t%d\t", loops->id, loops->leader->id, loops->exit );
      while( line != NULL ) {
        printf( "%d\t", line->line );
        line = line->next;
      }
      printf( "\n" );
      loops = loops->next;
    }
    inserted = inserted->next;
  }

  return 0;
}
