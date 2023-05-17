#include "kernel/fcntl.h"
#include "kernel/types.h"
#include "user/user.h"

int
cstart(char *cname, char *console, int max_mem, char *crootdir, int max_disk,
       char **argv)
{
  int id;

  id = cfork(cname, max_mem, crootdir, max_disk);
  if(id == 0){
    close(0);
    open(console, O_RDWR);
    close(1);
    dup(0);
    close(2);
    dup(0);
    exec(argv[0], argv);
    printf("ctest: exec() failed\n");
  }else if(id < 0){
    printf("ctest: cfork() failed\n");
    exit(-1);
  }

  return id;
}

void
ctest_cpu(void)
{
  char *argv[10];

  argv[0] = "busymany";
  argv[1] = "500";
  argv[2] = "1";
  argv[3] = 0;
  cstart("foo", "console1", 0, "/", 0, argv);

  argv[0] = "busymany";
  argv[1] = "500";
  argv[2] = "4";
  argv[3] = 0;
  cstart("bar", "console2", 0, "/", 0, argv);

  wait(0);
  wait(0);
}

int
main(int argc, char **argv)
{
  ctest_cpu();
  return 0;
}
