#include <stdio.h>

int main( )
{
   int i=0,j=0,s=0;

   do {
     do {
       s++;
     } while (j++ < 10);
   } while (i++ < 10 );

   printf( "s=%d\n", s );
   return 0;
}
