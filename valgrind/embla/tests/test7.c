#include <stdlib.h>
#include <stdio.h>

static void inc(int *p)
{
   *p=*p+1;
}

int main(int argc, char **argv)
{
   int *q=NULL,n=0;

   q = (int*) malloc( sizeof(int) );
   inc(q);
   inc(&n);
   inc(q);
   printf( "%d\n", *q+n );
   q = (int*) malloc( sizeof(int) );
   return q==NULL;
}
