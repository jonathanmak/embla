/*
C: The Complete Reference, 4th Ed. (Paperback)
by Herbert Schildt

ISBN: 0072121246
Publisher: McGraw-Hill Osborne Media; 4 edition (April 26, 2000)
*/

#include <assert.h>
#include <ctype.h>
#include <getopt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void quick(char *items, int count);
void qs(char *items, int left, int right);

int main(int argc, char ** argv)
{
  int c;
  int option_index;
  long length = 300;
  char * buf = NULL;
  unsigned seed = (unsigned) time(NULL);
  while (1)
  {
    static struct option long_options[] = {
      {"length", 1, 0, 0},
      {"seed", 1, 0, 0},
      {0, 0, 0, 0},
    };

    c = getopt_long(argc, argv, "l:s:", long_options, & option_index);
    if (c == -1)
      break;
    switch (c)
    {
    case 'l':
      length = strtol(optarg, NULL, 0);
      break;
    case 's':
      seed = strtoul(optarg, NULL, 0);
      break;
    case '?':
      break;
    default:
      assert(0);
    }
  }

  printf("Using seed %u\n", seed);
  srandom(seed);
  
  if (optind < argc)
  {
    buf = malloc(strlen(argv[optind]) + 1);
    assert(buf);
    strcpy(buf, argv[optind]);
  }
  else
  {
    int i;
    assert(length > 0);
    buf = malloc(length + 1);
    assert(buf);
    buf[length] = '\0';
    for (i = 0; i < length; ++i)
      do {
	buf[i] = random() % 0x100;
      } while (! (isalnum(buf[i])));
  }
  printf("The unsorted string is: %s.\n", buf);
  quick(buf, strlen(buf));
  printf("The sorted string is: %s.\n", buf);

  return 0;
}
/* Quicksort setup function. */
void quick(char *items, int count)
{
  qs(items, 0, count-1);
}

/* The Quicksort. */
void qs(char *items, int left, int right)
{
  register int i, j;
  char x, y;

  i = left;
  j = right;
  x = items[(left+right)/2];

  do {
    while((items[i] < x) && (i < right))
      i++;
    while((x < items[j]) && (j > left))
      j--;

    if(i <= j) {
      y = items[i];
      items[i] = items[j];
      items[j] = y;
      i++;
      j--;
    }
  } while(i <= j);

  if(left < j)
    qs(items, left, j);
  if(i < right)
    qs(items, i, right);
}
