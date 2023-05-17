struct cinfo {
  int state;
  char name[16];
  char rootpath[128];
  int max_mem_bytes;
  int max_disk_bytes;
  int used_mem_bytes;
  int used_disk_bytes;
  int nprocs;
  int nticks;
};
