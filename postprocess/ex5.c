#include <stdlib.h>
#include <stdio.h>

static int nfib(int n)
{
   int result;
   if( n < 2 ) {
     result = 1;
   } else {
     int a = nfib( n-1 );
     int b = nfib( n-2 );
     result = a+b;
   }
   return result;
}
     
 

int main(int argc, char **argv)
{
   int m = nfib( 25 );

   printf( "%d\n", m );
   return 0;
}
