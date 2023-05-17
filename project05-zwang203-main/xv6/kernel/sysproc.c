#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork(mycont());
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_sbrksz(void)
{
  return myproc()->sz;
}

uint64
sys_sname(void)
{
  struct proc *p = myproc();
  uint64 name_p;

  argaddr(0, &name_p);
  if(copyin(p->pagetable, p->name, name_p, 16) < 0)
    return -1;
  return 0;
}

uint64
sys_sproc(void)
{
  uint64 up_p;  // user pointer to struct uproc
  int slot;

  argint(0, &slot);
  argaddr(1, &up_p);
  return sproc(slot, up_p);
}

uint64
sys_consw(void)
{
  int consnum;

  argint(0, &consnum);
  consw(consnum);
  return 0;
}

uint64
sys_cfork(void)
{
  char cname[16];
  int max_mem_bytes;
  char crootdir[MAXPATH];
  int max_disk_bytes;
  int n;

  if((n = argstr(0, cname, 16)) < 0)
    return -1;

  argint(1, &max_mem_bytes);

  if((n = argstr(2, crootdir, MAXPATH)) < 0)
    return -1;

  argint(3, &max_disk_bytes);

  return cfork(cname, max_mem_bytes, crootdir, max_disk_bytes);
}

uint64
sys_cinfo(void)
{
  uint64 up_p;  // user pointer to array of struct cinfo

  argaddr(0, &up_p);
  return cinfo(up_p);
}
