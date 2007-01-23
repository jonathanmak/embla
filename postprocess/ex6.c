#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SIZE_ARG          "--size"
#define SEED_ARG          "--seed"
#define HELP_ARG          "--help"

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
  int n = 0;
  int i = 0;
  unsigned int seed = 0;
  int *a;
  
  while (++i < argc && *argv[i] == '-') {
    char *arg = argv[i];
    if (strncmp(arg, SIZE_ARG, strlen(SIZE_ARG)) == 0) {
      arg += strlen(SIZE_ARG) + 1;
      if ((n = atoi(arg)) <= 0) {
	perror("bad number of iterations");
        exit(1);
      }
      continue;
    }
    if (strncmp(arg, SEED_ARG, strlen(SEED_ARG)) == 0) {
      arg += strlen(SEED_ARG) + 1;
      seed = (unsigned int) atoi(arg);
      continue;
    }
    if (strncmp(arg, HELP_ARG, strlen(HELP_ARG)) == 0) {
      printf("usage: <command> [--size=<n>] [--seed=<n>] [--help]\n");
      continue;
    }
  }

  a = (int *) malloc(n * sizeof(int));
  if (a == (void *) NULL) {
    perror("cann't allocate the array to sort");
    exit(2);
  }

  srand(seed);
  for (i = n; i--; ) {
    a[i] = rand();		/* 0..RAND_MAX */
    // printf("seed=%d, i=%d, value=%d\n", seed, i, a[i]);
  }
	 
  qs( a, n );
  for( i=0; i<n; i++ ) {
    printf( "%d ", a[i] );
  }
  printf( "\n" );

}
