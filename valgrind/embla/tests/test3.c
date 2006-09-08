#include <stdio.h>
#include <stdlib.h>

#define N 5

typedef struct _ilist {
   int val;
   struct _ilist *next;
} ilist;

static ilist* mklist(int n)
{
  int i;
  ilist *p = NULL;

  for( i=0; i<n; i++ ) {
    ilist *t = (ilist *) malloc( sizeof(ilist) );
    t->val = i;
    t->next = p;
    p = t;
  }
  return p;
}

static int sumlist(ilist* p)
{
   ilist *q;
   int  sum = 0;

   for( q=p; q!=NULL; q=q->next ) {
      sum += q->val;
   }
   return sum;
}


int main(int argc, char **argv) 
{
  int m,n;
  ilist *p,*q;

  p = mklist( N );
  q = mklist( N );
  m = sumlist( p );
  n = sumlist( q );

  printf("%d\n", m);

  return 0;

}
