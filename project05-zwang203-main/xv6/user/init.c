// init: The initial user-level program

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;
  char name[16];
  int fd;

  // Create console devices
  strcpy(name, "console0");
  for(int i = 0; i < NCONS; i++){
    name[7] = '0' + i;
    if((fd = open(name, O_RDWR)) < 0){
      mknod(name, CONSOLE, i);
    }else{
      close(fd);
    }
  }

  open("console0", O_RDWR);

  dup(0);  // stdout
  dup(0);  // stderr

  for(;;){
    printf("init: starting sh\n");
    pid = fork();
    if(pid < 0){
      printf("init: fork failed\n");
      exit(1);
    }
    if(pid == 0){
      exec("sh", argv);
      printf("init: exec sh failed\n");
      exit(1);
    }

    for(;;){
      // this call to wait() returns if the shell exits,
      // or if a parentless process exits.
      wpid = wait((int *) 0);
      if(wpid == pid){
        // the shell exited; restart it.
        break;
      } else if(wpid == -2){
        continue;
      } else if(wpid < 0){
        printf("init: wait returned an error\n");
        exit(1);
      } else {
        // it was a parentless process; do nothing.
      }
    }
  }
}
