#include <stdlib.h>
#include <stdio.h>

struct {int a; int b;} has_ab;

int main(int argc, char **argv)
{
   has_ab.a = 1;
   has_ab.b = 1;
   printf("%d ", has_ab.a);
   has_ab.a = 2;
   has_ab.b = 2;
   printf("%d %d\n", has_ab.a, has_ab.b);
   return 0;
}
