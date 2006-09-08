#include <stdlib.h>
#include <stdio.h>

static int a[] = 
   {17, 3, 84, 89, 4, 5, 23, 43, 
    21, 7, 2, 1, 55, 63, 21};
static int n = 15;

static int *part(int *a, int n)
{
   int i = a[0];
   int k = a[n-1];
   int *lp = a;
   int *hp = a+n-1;

   while( lp<hp ) {
      if( k<i ) {
         *lp = k;
          lp++;
          k = *lp;
      } else {
          *hp = k;
           hp--;
           k = *hp;
      }
   }
   *lp = i;
   return lp;
}

static void qs(int *a, int n)
{
   if( n>1 ) {
      int *lp = part( a, n );
      int m = lp-a;
      qs( a, m );
      qs( lp+1, n-m-1 );
   }
}

 

int main(int argc, char **argv)
{
   int i;
   
   qs( a, n );
   for( i=0; i<n; i++ ) {
      printf( "%d ", a[i] );
   }
   printf( "\n" );
   return 0;
}
