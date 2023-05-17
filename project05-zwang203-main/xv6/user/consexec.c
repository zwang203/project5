// consexec - start a process on a new console

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

void
print_busy(char *name, char *state)
{
  printf("[%s, %d, %l] busy - %s\n", 
         name, getpid(), uptime(), state);
}

int
main(int argc, char **argv)
{
  char *name;
  char **nargv;
  int nstart = 2;
  int switchto = 0;
  int consnum = 0;
  int id;
  
  if(argc < 3){
    printf("usage: consexec <console> [-s] [argv]\n");
    printf("  -s switch to <console> on exec\n");
    exit(-1);
  }

  name = argv[1];
  
  // Get integer console number
  consnum = name[7] - '0';

  // Get flag to switch to console on start
  if(argv[2][0] == '-' && argv[2][1] == 's'){
    switchto = 1;
    nstart = 3;
  }

  nargv = &argv[nstart];

  id = fork();

  if(id == 0){
    close(0);
    open(name, O_RDWR);
    close(1);
    dup(0);
    close(2);
    dup(0);
    if(switchto)
      consw(consnum);
    exec(nargv[0], nargv);
    printf("consexec: exec failed\n");
  }

  return 0;
}
