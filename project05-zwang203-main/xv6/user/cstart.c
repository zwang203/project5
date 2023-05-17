// cstart - start a process in a new container.

#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"


int
main(int argc, char **argv)
{
  char *cname;
  char *console;
  int max_mem_bytes;
  char *crootdir;
  int max_disk_bytes;
  char **nargv;
  int switchto = 0;
  int consnum = 0;
  int id;
  int argidx = 1;
  
  if(argc < 3){
    printf("usage: cstart <name> <console> [-s] <max_mem> <root_dir> <max_disk> [argv]\n");
    printf("  -s switch to <console> on start\n");
    exit(-1);
  }

  cname = argv[argidx];
  argidx += 1;
  console = argv[argidx];
  argidx += 1;
  
  // Get integer console number
  consnum = console[7] - '0';

  // Get flag to switch to console on start
  if(argv[argidx][0] == '-' && argv[argidx][1] == 's' && argv[argidx][2] == '\0'){
    switchto = 1;
    argidx += 1;
  }

  max_mem_bytes = atoi(argv[argidx]);
  argidx += 1;
  crootdir = argv[argidx];
  argidx += 1;
  max_disk_bytes = atoi(argv[argidx]);
  argidx += 1;

  nargv = &argv[argidx];

  id = cfork(cname, max_mem_bytes, crootdir, max_disk_bytes);

  if(id == 0){
    close(0);
    open(console, O_RDWR);
    close(1);
    dup(0);
    close(2);
    dup(0);
    if(switchto)
      consw(consnum);
    exec(nargv[0], nargv);
    printf("cstart: exec() failed\n");
  }else if(id < 0){
    printf("cstart: cfork() failed\n");
  }

  return 0;
}
