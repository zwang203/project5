#include "debug.h"
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "uproc.h"
#include "cinfo.h"


struct cpu cpus[NCPU];

// Containers

// Array of containers just like the proc array.
// See proc.h for the definition of struct cont.
struct cont cont[NCONT];

// Just like queue-based scheduling we will have an unused list of containers.
struct list cont_unused_list;
struct spinlock cont_unused_lock;

// Just like queue-based scheduling we will have a run list of container.
struct list cont_run_list;
struct spinlock cont_run_lock;

// We start with one "root" container.
struct cont *cont_root;
int cont_init = 0;

// Proc array
struct proc proc[NPROC];

// Queue-based scheduling data
struct list unused_list;
struct spinlock unused_lock;

struct list run_list;
struct spinlock run_lock; 

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Print run_list
void
proc_print_run_list(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runnable",
  [RUNNING]   "running",
  [ZOMBIE]    "zombie"
  };

  struct list_elem *e;
  int count = 0;

  acquire(&run_lock);
  for (e = list_begin(&run_list); e != list_end(&run_list); e = list_next(e)) {
    struct proc *p = list_entry(e, struct proc, elem);
    printf("SLOT=%d PID=%d NAME=%s STATE=%s\n",
           (int)(p - &proc[0]), p->pid, p->name, states[p->state]);
    count += 1;
  }
  if(count == 0)
    printf("EMPTY\n");
  release(&run_lock);
  return;
}

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}


// Containers

// Allocate a new container just like allocproc().
static struct cont*
alloccont(char *cname, int max_mem_bytes, char *crootdir, int max_disk_bytes)
{
  struct cont *c;
  struct list_elem *e;
  int isroot = 0;

  // Determine if we are allcating the root container at kernal start.
  if(strncmp("root", cname, 4) == 0)
    isroot = 1;

  // Remove a container from the container unused list if one is available.
  e = 0;
  acquire(&cont_unused_lock);
  if(!list_empty(&cont_unused_list)){
    e = list_begin(&cont_unused_list);
    list_remove(e);
  }
  release(&cont_unused_lock);

  if(e == 0)
    return 0;

  c = list_entry(e, struct cont, elem);
  acquire(&c->lock);
  // Set container state to USED
  c->state = USED;
  // Each container has it's own nextpid so that PIDs within a container
  // are separate from PIDs in other containers (name-space isolation).
  c->nextpid = 1;
  list_init(&c->proc_list);
  list_init(&c->run_list);

  // Set the name of the container
  safestrcpy(c->name, cname, sizeof(c->name));

  // Set the max number of bytes of memory that should be used by the container.
  c->max_mem_bytes = max_mem_bytes;

  // Add a reference to the root director for this container.
  safestrcpy(c->rootpath, crootdir, sizeof(c->rootpath));
  if(!isroot){
    begin_op();
    if((c->rootdir = namei(crootdir)) == 0){
      end_op();
      return 0;
    }
    end_op();
  } 

  // Set the max number of bytes of disk that should be used by the container.
  c->max_disk_bytes = max_disk_bytes;

  // Keep track of bytes used and total ticks for the container.
  c->used_mem_bytes = 0;
  c->used_disk_bytes = 0;
  c->nticks = 0;
  
  release(&c->lock);

  return c;
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  struct cont *c;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  // Containers
  
  initlock(&cont_run_lock, "cont_run_lock");
  list_init(&cont_run_list);

  initlock(&cont_unused_lock, "cont_unused_lock");
  list_init(&cont_unused_list);

  for(c = cont; c < &cont[NCONT]; c++){
    initlock(&c->lock, "cont");
    c->state = UNUSED;
    list_push_back(&cont_unused_list, &c->elem);
  }
    
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  initlock(&unused_lock, "unused_list_lock");
  list_init(&unused_list);

  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
      list_push_back(&unused_list, &p->elem);
  }  
  // Queue-based scheduling
  
  initlock(&run_lock, "run_list_lock");
  list_init(&run_list);
  //printf("procinit() run_list: ");
  //proc_print_run_list();

  // Containers

  // Allocate the root container and put on the container run list.
  cont_root = alloccont("root", 0, "/",  0);
  list_push_back(&cont_run_list, &cont_root->elem);
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

// Containers

// We will need to get the current container just like myproc()
// gets the current process.
struct cont*
mycont(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  struct cont *cp = p->contp;
  pop_off();
  return cp;
}

int
allocpid(struct cont *contp)
{
  int pid;
  
  // Containers

  // Use the container-specific nextpid.
  acquire(&contp->lock);
  pid = contp->nextpid;
  contp->nextpid = contp->nextpid + 1;
  release(&contp->lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.

// Containers - now we pass a container points into allocproc so that
// the new proc can point to the container it belongs to.
static struct proc*
allocproc(struct cont *contp)
{
  struct proc *p;
  struct list_elem *e;

  e = 0;
  acquire(&unused_lock);
  if(!list_empty(&unused_list)){
    e = list_begin(&unused_list);
    list_remove(e);
  }
  release(&unused_lock);

  if(e == 0)
    return 0;

  p = list_entry(e, struct proc, elem);
  acquire(&p->lock);

  // Containers - now we get a new pid from the nextpid in the container.
  p->pid = allocpid(contp);
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  p->nsched = 0;
  p->nticks = 0;

  p->contp = contp;

  // Containers
  
  // Put the process onto the proc list for the container.
  // The proc list just keeps track of all the processes that exist
  // in a container. It is different from the container run list which
  // is used for scheduling processes within a container.
  acquire(&contp->lock);
  list_push_back(&contp->proc_list, &p->elem_c);
  release(&contp->lock);
  
  return p;
}


// Containers

// Free a container like we free a process.
// We do this when there are no more processes on a container's run list.
static void
freecont(struct cont *contp)
{
  contp->state = UNUSED;
  contp->nextpid = 1;
  contp->name[0] = '\0';
  contp->rootdir = 0;
  contp->max_mem_bytes = 0;
  contp->max_disk_bytes = 0;
  contp->used_mem_bytes = 0;
  contp->used_disk_bytes = 0;
  contp->nticks = 0;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  int cont_empty = 0;

  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  acquire(&unused_lock);
  list_push_back(&unused_list, &p->elem);
  release(&unused_lock);

  // Containers

  // Check to see if we are freeing the last process within a container.
  // If so, then we will free the entire container.

  acquire(&p->contp->lock);
  list_remove(&p->elem_c);
  cont_empty = list_empty(&p->contp->proc_list);
  release(&p->contp->lock);

  // Check if this is the last process in the container
  if(cont_empty){
    acquire(&cont_run_lock);
    list_remove(&p->contp->elem);
    release(&cont_run_lock);
    freecont(p->contp);

    // Put the container back on the container unused list.
    acquire(&cont_unused_lock);
    list_push_back(&cont_unused_list, &p->contp->elem);
    release(&cont_unused_lock);
  }
  p->contp = 0;
}


// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc(cont_root);
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);

  // Containers

  // Put the first process on the run list of the root container.
  acquire(&cont_root->lock);
  list_push_back(&cont_root->run_list, &p->elem);
  release(&cont_root->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Containers
// Now we send a pointer to a container so that we know which container
// to add the newly forked process to.

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(struct cont *contp)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc(contp)) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  // Containers

  // Add the new process to the container's run list.
  acquire(&contp->lock);
  list_push_back(&contp->run_list, &np->elem);
  release(&contp->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      // Containers hack to prevent init from thinking the shell
      // has exited when if fact it is only a process in a
      // container.
      if(pp->contp != cont_root)
        pp->pid = -2;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();
  int nprocs;

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  // Containers

  // If this is the last process in a container we need to release our
  // access to the container root directory inode. We do this here in
  // the context of a process because this may require a sleep().
  acquire(&p->contp->lock);
  nprocs = list_size(&p->contp->proc_list);
  release(&p->contp->lock);
  if(nprocs == 1){
    begin_op();
    iput(p->contp->rootdir);
    end_op();
  }

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);

  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}


// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  struct list_elem *e;
  struct cont *contp;
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    // Containers

    // This code only schedules processes in the root container.
    contp = cont_root;

    // Get a process from the container's run_list.
    acquire(&contp->lock);
    e = 0;
    if(!list_empty(&contp->run_list)){
      e = list_begin(&contp->run_list);
      list_remove(e);
    }
    release(&contp->lock);

    if(e == 0){
      asm volatile("wfi");
      continue;
    }

    p = list_entry(e, struct proc, elem);

    acquire(&p->lock);
    //debug_print("p->name = %s\n", p->name);
    ASSERT(p->state == RUNNABLE);
    p->state = RUNNING;
    p->nsched += 1;
    c->proc = p;

    swtch(&c->context, &p->context);

    // Process is done running for now.
    // It should have changed its p->state before coming back.
    c->proc = 0;
    release(&p->lock);
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  struct cont *contp = mycont();
  acquire(&p->lock);
  p->state = RUNNABLE;

  // Containers

  // Put the current process at the end of the container's run list.
  acquire(&contp->lock);
  list_push_back(&contp->run_list, &p->elem);
  release(&contp->lock);
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    cont_init = 1;
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;
  struct cont *contp;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;

        // Containers

        // Put the process we are waking up at the end of the container's
        // run list in which the process exists.
        contp = p->contp;
        acquire(&contp->lock);
        list_push_back(&contp->run_list, &p->elem);
        release(&contp->lock);
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct cont *contp;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;

        // Containers

        // Put the process we are killing at the end of the container's
        // run list to allow it to be scheduled, and then exit.
        contp = p->contp;
        acquire(&contp->lock);
        list_push_back(&contp->run_list, &p->elem);
        release(&contp->lock);
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("[%s] %d %s %s %d", p->contp->name, p->pid, state, p->name, p->nticks);

    printf("\n");
  }
}

int
uproc(int pid, uint64 up_p)
{
  struct proc *myp = myproc();
  struct proc *p;
  struct uproc up;
  int rv = -1;
  
  // Find proc struct for pid
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state != UNUSED && p->pid == pid){
      acquire(&p->lock);
      up.pid = p->pid;
      up.state = p->state;
      up.sz = p->sz;
      strncpy(up.name, p->name, 16);
      up.nsched = p->nsched;
      up.nticks = p->nticks;
      release(&p->lock);
      if(copyout(myp->pagetable, up_p, (char *)&up, sizeof(up)) < 0)
        return -1;
      rv = pid;
      break;
    }
  }

  return rv;
}

int
sproc(int slot, uint64 up_p)
{
  struct proc *myp = myproc();
  struct proc *p;
  struct uproc up;

  p = &proc[slot];
  acquire(&p->lock);
  up.pid = p->pid;
  up.state = p->state;
  up.sz = p->sz;
  strncpy(up.name, p->name, 16);
  up.nsched = p->nsched;
  up.nticks = p->nticks;
  release(&p->lock);
  if(copyout(myp->pagetable, up_p, (char *)&up, sizeof(up)) < 0)
    return -1;

  return slot;
}


// Containers
void
contdump(void)
{
  struct cont *c;
  int nprocs = 0;

  printf("\n");
  for(c = cont; c < &cont[NCONT]; c++){
    if(c->state == UNUSED)
      continue;
    acquire(&c->lock);
    nprocs = list_size(&c->proc_list);
    release(&c->lock);
    printf("[%s] %d procs %d/%d mem %d/%d disk %d ticks", c->name, nprocs,
           c->used_mem_bytes, c->max_mem_bytes,
           c->used_disk_bytes, c->max_disk_bytes,
           c->nticks);

    printf("\n");
  }
}

int
cinfo(uint64 up_p)
{
  struct proc *myp = myproc();

  struct cont *c;
  struct cinfo ci;
  int rv = 0;
  
  // Find proc struct for pid
  for(c = cont; c < &cont[NCONT]; c++){
    acquire(&c->lock);
    if(c->state != UNUSED){
      ci.state = c->state;
      strncpy(ci.name, c->name, sizeof(ci.name));
      strncpy(ci.rootpath, c->rootpath, sizeof(ci.rootpath));
      ci.max_mem_bytes = c->max_mem_bytes;
      ci.max_disk_bytes = c->max_disk_bytes;
      ci.used_mem_bytes = c->used_mem_bytes;
      ci.used_disk_bytes = c->used_disk_bytes;
      ci.nprocs = list_size(&c->proc_list);
      ci.nticks = c->nticks;
      release(&c->lock);
      if(copyout(myp->pagetable, up_p, (char *)&ci, sizeof(ci)) < 0)
        return -1;
      up_p = up_p + sizeof(struct cinfo);
    }else{
      release(&c->lock);
    }
  }

  return rv;
}

int
cfork(char *cname, int max_mem_bytes, char *crootdir, int max_disk_bytes)
{
  struct cont *contp;
  
  contp = alloccont(cname, max_mem_bytes, crootdir, max_disk_bytes);
  if(contp == 0){
    return -1;
  }

  acquire(&cont_run_lock);
  list_push_back(&cont_run_list, &contp->elem);
  release(&cont_run_lock);

  return fork(contp);
}
