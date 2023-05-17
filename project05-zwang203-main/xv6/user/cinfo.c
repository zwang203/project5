// cinfo - Like ps for containers

#include "kernel/types.h"
#include "kernel/cinfo.h"
#include "kernel/param.h"
#include "user/user.h"

enum procstate {UNUSED, USED};

struct cinfo cinfo_ar[NCONT];

int
main(int argc, char **argv)
{
  struct cinfo *ci;

  if(cinfo(cinfo_ar) < 0){
    printf("cinfo - cinfo() failed\n");
    exit(-1);
  }

  for(int i = 0; i < NCONT; i++){
    ci = &cinfo_ar[i];
    if(ci->state == USED){
      printf("Container: %s\n", ci->name);
      printf("  rootpath        : %s\n", ci->rootpath);
      printf("  max_mem_bytes   : %d\n", ci->max_mem_bytes);
      printf("  used_mem_bytes  : %d\n", ci->used_mem_bytes);
      printf("  max_disk_bytes  : %d\n", ci->max_disk_bytes);
      printf("  used_mem_bytes  : %d\n", ci->used_disk_bytes);
      printf("  nprocs          : %d\n", ci->nprocs);
      printf("  nticks          : %d\n", ci->nticks);
      printf("\n");
    }
  }

  return 0;
}
