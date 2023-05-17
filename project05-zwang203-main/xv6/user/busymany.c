#include "kernel/types.h"
#include "user/user.h"

void
print_busy(char *name, char *state)
{
  printf("[%s, %d, %l] busy - %s\n", 
         name, getpid(), uptime(), state);
}

void
busy(char *name, long ticks, int withsleep)
{
  long ticks_start;
  
  //print_busy(name, "start");
  
  ticks_start = uptime();

  if(ticks < 10 || !withsleep){
    while((uptime()-ticks_start) < ticks);
  }else{
    while((uptime()-ticks_start) < ticks)
      sleep(5);
  }

  //print_busy(name, "end");

  return;
}


int
main(int argc, char **argv)
{
  long ticks;
  int count = 0;
  int id;
  char name[16];
  int withsleep = 0;
  int argidx = 1;
  
  if(argc != 3){
    printf("usage: busy <ticks> [-s] <count>\n");
    printf("  -s uses sleep in busy loop to reduce CPU usage.\n");
    exit(-1);
  }

  ticks = atoi(argv[argidx]);
  argidx += 1;

  if(argv[argidx][0] == '-' && argv[argidx][1] == 's'){
    withsleep = 1;
    argidx += 1;
  }    
  
  count = atoi(argv[argidx]);

  for(int i = 0; i < count; i++){
    id = fork();
    if(id == 0){
      strcpy(name, "busy[");
      name[5] = 'A' + i;
      name[6] = ']';
      name[7] = '\0';
      sname(name);
      //print_busy(name, "start");
      busy(name, ticks, withsleep);
      exit(0);
    }
  }
  
  return 0;
}
