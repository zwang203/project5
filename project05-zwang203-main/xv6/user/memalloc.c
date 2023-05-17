#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  int nbytes;
  char *mem;

  if(argc != 2){
    printf("usage: memalloc <nbytes>");
    exit(-1);
  }

  nbytes = atoi(argv[1]);
  mem = sbrk(nbytes);

  if((mem) == (char*)-1){
    printf("memalloc - sbrk(%d) failed\n", nbytes);
    exit(-1);
  }
  printf("memalloc - sbrk(%d) succeeded\n", nbytes);
  
  return 0;
}
