#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

// ctable will contain bits of data relevant to the 'root container'
struct {
  struct container cont[NCONT];
  int next_cid;
} ctable; 

static struct proc *initproc;

int nextpid = 1;

// All declarations here are used elsewhere as extern
int total_mem;
int used_mem;
int total_disk = FSSIZE * 512;  // Initialize the total_disk space to blocks allocated(FSSIZE) * the block size(512)
int used_disk;

extern void forkret(void);
extern void trapret(struct file *f);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid()
{
  return mycpu()-cpus;
}

int
next_free(struct container *c)
{
  int i;

  for (i = 0 ; i < c->total_proc ; i++) {
    if (c->inner_ptable[i] == 0) {
      return i;
    }
  }
  return -1;
}

struct container*
find_cont(int cid) {
  int i;

  for (i = 0 ; i < NCONT ; i++) {
    if (ctable.cont[i].cid == cid) {
       return &ctable.cont[i];
    }
  }
  return (void*)0;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  struct proc *curproc = myproc();
  char *sp;
  int nf = -1;

  acquire(&ptable.lock);

  if (curproc != 0) {
    if (curproc->cont != 0) {
      nf = next_free(curproc->cont);
      if (nf == -1) {
        curproc->cont->tokill = 1;
        release(&ptable.lock);
        return 0;
      }
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  if (nf != -1) {
    curproc->cont->inner_ptable[nf] = p;
  }

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->ticks = 0;

  return p;
}

static struct proc*
callocproc(int cid)
{
  struct proc *p;
  struct container *cont = find_cont(cid);
  int nf = -1;
  char *sp;

  acquire(&ptable.lock);

  if (cont != 0) {
    nf = next_free(cont);
    if (nf == -1) {
      release(&ptable.lock);
      return 0;
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  cont->inner_ptable[0] = p;
  

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->ticks = 0;


  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);

  if (curproc->cont !=0) {
    if (curproc->cont->tokill) {
      cprintf("Container:%d exceeded memory limit\n", curproc->cont->cid);
      kill_cont(curproc->cont->cid);
    }
  }
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    if (curproc->cont != 0 && curproc->cont->tokill) {
      kill_cont(curproc->cont->cid);
    }
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  np->cont = curproc->cont;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

int
cfork(int cid)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct container *ncont;

  for (i = 0 ; i < NCONT ; i++) {
    if ((ncont = &ctable.cont[i])->cid == cid) {
      break;
    }
  }
  if (i == NCONT) {
    return -1;
  }

  // Allocate process.
  if((np = callocproc(cid)) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  np->cont = ncont;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid, i;
  struct proc *curproc = myproc();
  struct container *cont = curproc->cont;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        if (cont != 0) {
          for (i = 0 ; i < curproc->cont->total_proc ; i++) {
            if (cont->inner_ptable[i]->pid == pid) {
              cont->inner_ptable[i] = 0;
              break;
            }
          }
        }
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
      
  for (;;) {
    sti();
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  if (curproc->cont == 0) {
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid == pid){
        p->killed = 1;
        if (p->cont != 0) {
          int i;
          for (i = 0 ; i < p->cont->total_proc ; i++) {
            if (p->cont->inner_ptable[i]->pid == pid) {
              p->cont->inner_ptable[i] = 0;
            }
          }
        }
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING)
          p->state = RUNNABLE;
        release(&ptable.lock);
        return 0;
      }
    }  
  } else {
    struct container *c = curproc->cont;
    int i;
    for (i = 0 ; i < c->total_proc ; i++) {
      if ((p = c->inner_ptable[i])->pid == pid) {
        p->killed = 1;
        // Wake process from sleep if necessary.
        if(p->state == SLEEPING)
          p->state = RUNNABLE;
        c->inner_ptable[i] = 0;
        release(&ptable.lock);
        return 0;
      }
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    if (p->cont != 0) {
      cprintf("cid:%d", p->cont->cid);
    }
    cprintf("\n");
  }
}

int 
proc_print(struct proc *p)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  char *state;

  struct container *cont = myproc()->cont;

  if (p->state >= 0 && p->state < NELEM(states) && states[p->state]) {
    state = states[p->state];
  } else {
    state = "???";
  }
  if (cont == 0) {
    if (p->cont != 0) {
      cprintf("%d : %d : %s : %d : %s\n", p->cont->cid, p->pid, p->name, p->sz, state);
    } else {
      cprintf("root : %d : %s : %d : %s\n", p->pid, p->name, p->sz, state);
    }
  } else {
    cprintf("%d : %s : %d : %s\n", p->pid, p->name, p->sz, state);
  }


  return -1;
}

void
cprocdump(void)
{
  struct proc *curproc = myproc();
  struct container *cont;

  int i;
  struct proc *p;
  acquire(&ptable.lock);
  if ((cont = curproc->cont) == 0) {
    cprintf("cid : pid : name : size : state\n");
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      if (p->state == UNUSED) {
        continue;
      } 
      proc_print(p);
    }
  } else {
    cprintf("pid : name : size : state\n");
    for (i = 0; i < cont->total_proc; i++) {
      if ((p = cont->inner_ptable[i]) != 0) {
        if (p->state != UNUSED) {
          proc_print(p);
        }
      }
    }
  }
  release(&ptable.lock);
}

int
memdump(void)
{
  struct container *cont = myproc()->cont;

  if(cont == 0) {
    // In root container, need to display all available and used memory 
    cprintf("Available memory in kilobytes: %d\n", total_mem * 4096);
    cprintf("Used memory in kilobytes: %d\n", used_mem * 4096);
  } else {
    // In other container, only show available and used memory from within the container
    cprintf("Available memory in kilobytes: %d\n", cont->total_mem * 4096);
    cprintf("Used memory in kilobytes: %d\n", cont->used_mem * 4096);

  }

  return -1;
}

void
printdump(void)
{
  struct proc *curproc = myproc();
  struct container *cont;

  acquire(&ptable.lock);

  // if in root
  if ((cont = curproc->cont) == 0) {

    int i = 0;
    while (i < NPROC) {
      cprintf("%d\n", cont->inner_ptable[i]->pid);
      cprintf("%s\n", cont->inner_ptable[i]->name);
      cprintf("\n");
      i++;
    }

    cprintf("Max processes: %d\n", cont->total_proc);
    cprintf("Max memory: %d\n", cont->total_mem);
    cprintf("Max disk space: %d\n", cont->total_disk);
  }
  else {
    cprintf("Error: not in root.");
  }

  release(&ptable.lock); 
}

/* 
  Populates the ctable with a usable container struct
  using a given virtual console, root directory, and
  a start size for the filesystem.  If max proc mem or disk
  is 0 then sets the default, otherwise sets the limits to the
  given values.
*/
int
spawn_cont(int vcnode, char *path, int used_disk, int max_proc, int max_mem, int max_disk)
{
  int i, cid;
  struct container *ncont;
  struct inode *ip;

  for (i = 0 ; i < NCONT ; i++) {
    if (ctable.cont[i].vc_node == vcnode) {
      return -1;
    }
  }

  for (i = 0 ; i < NCONT ; i++) {
    if (ctable.cont[i].cid == 0) {
      cid = ++ctable.next_cid;
      ncont = &ctable.cont[i];
      break;
    }
  }

  if (i == NCONT) {
    return -1;
  }
  ncont->cid = cid;
  if (max_proc > 0 && max_proc < NPROC) {
    ncont->total_proc = max_proc;
  } else {
    ncont->total_proc = 20;
  }
  if (max_mem > 0 && max_mem < total_mem) {
    ncont->total_mem = max_mem;
  } else {
    ncont->total_mem = 20000;
  }
  if (max_disk > 0 && max_disk * 1024 < total_disk) {
    ncont->total_disk = max_disk * 1024;
  } else {
    ncont->total_disk = 200000;
  } 
  ncont->vc_node = vcnode;
  ncont->used_disk = used_disk;
  ncont->last_tick = 0;
  ncont->awake = 0;
  ncont->tokill = 0;

  if ((ip = namei(path)) == 0) {
    return -1;
  }

  ncont->root_dir = ip;
  strncpy(ncont->name, path, strlen(path));

  return cid;
}

/*
  Puts all processes in a container
  to sleep, saving the states so they
  can be resumed later.
*/
int
cpause(int cid)
{
  struct container *cont;
  cont = find_cont(cid);

  if(cont == (void*)0) {
    return -1;
  }
  int i;
  for(i =0; i < cont->total_proc; i++) {
    if (cont->inner_ptable[i] != 0) {
      cont->save_state[i] = cont->inner_ptable[i]->state;
      cont->inner_ptable[i]->state = SLEEPING;
    }
  }
  cont->awake = 0;
  return 0;
}

/*
  Wakes up all processes in a container
  based on their save states.
*/
int 
cresume(int cid)
{
  struct container *cont;
  cont = find_cont(cid);

  if(cont == (void*)0) {
    return -1;
  }
  int i;
  for(i =0; i < cont->total_proc; i++) {
    if (cont->inner_ptable[i] != 0) {
      cont->save_state[i] = cont->inner_ptable[i]->state;
      cont->inner_ptable[i]->state = RUNNABLE;
    }
  }

  cont->awake = 1;
  return 1;
}

/*
  Resets the data of a container struct
  with the given cid.  Also kills all
  processes running in that container.
*/
int 
kill_cont(int cid)
{  
  struct container *cont;
  cont = find_cont(cid);

  if (cont == 0) {
    return -1;
  }
  int i;
  for(i =0; i < cont->total_proc; i++) {
    if (cont->inner_ptable[i] != 0) {
      kill(cont->inner_ptable[i]->pid);
    }
  }

  cont->cid = 0;
  cont->vc_node = 0;
  cont->used_mem = 0;
  cont->used_disk = 0;
  cont->tokill = 0;

  return 0;
}

/*
  Prints the total and used disk space
  of a given container.
  cid is 0 for the root container.
*/
int 
df_mem(void)
{
  struct container *cont = myproc()->cont;

  if(cont == 0) {
    // In root container, need to display all available and used memory 
    cprintf("Total disk space available in kilobytes: %d\n", (total_disk - used_disk)/1024); 
    cprintf("Used disk space in kilobytes: %d\n", used_disk/1024);
  } else {
    // In other container, only show available and used memory from within the container
    cprintf("Total disk space in kilobytes: %d\n", (cont->total_disk)/1024); 
    cprintf("Used disk space in kilobytes: %d\n", (cont->used_disk)/1024);
  }

  return 1;
}

// Used to set the global used_disk on initialization.
int
total_used_disk(int disk_used)
{
  used_disk = disk_used;
  return 1;
}

/*
  Will print information for all containers.
  Can only be called from within the root.
*/
int
c_info(void)
{
  cprintf("\n");
  int i;
  for (i = 0 ; i < NCONT ; i++) {
    int cont_p_count = 0; // To count the number of processes per container
    //ctable.cont[i].cid
    if(ctable.cont[i].cid != 0) {
      cprintf("Name of container, CID: %d \n", ctable.cont[i].cid);
      //Directory
      cprintf("Directory: /%s \n", ctable.cont[i].name);
      //Max num procs, max amount of memory allocated, max disk space allocated
      cprintf("Max processes: %d Max amount of memory allocated: %d Max disk space allocated %d \n",
        ctable.cont[i].total_proc,
        ctable.cont[i].total_mem,
        ctable.cont[i].total_disk);
      //Processes running in each container
      cprintf("Processes running in CID: %d \n", ctable.cont[i].cid);

      int j;
      for(j = 0; j < ctable.cont[i].total_proc; j++) {
        if (ctable.cont[i].inner_ptable[j] != 0) {
          cprintf("%d: %s \n", j, ctable.cont[i].inner_ptable[j]->name);
          cont_p_count++;            
        }
      }

      cprintf("Used processes: %d Available Processes: %d \n", cont_p_count, ctable.cont[i].total_proc - cont_p_count);
      
      cprintf("Used memory: %d Available memory: %d \n", ctable.cont[i].used_mem, 
        ctable.cont[i].total_mem - ctable.cont[i].used_mem);

      cprintf("Used disk space: %d Available disk space: %d \n", ctable.cont[i].used_disk, 
        ctable.cont[i].total_disk - ctable.cont[i].used_disk);

      int k;
      for(k = 0; k < ctable.cont[i].total_proc; k++) {
        if (ctable.cont[i].inner_ptable[k] != 0) {
          cprintf("p->ticks: %d ticks: %d \n", ctable.cont[i].inner_ptable[k]->ticks, ticks);
          cprintf("proc usage: %d percent of cpu\n", (ctable.cont[i].inner_ptable[k]->ticks * 100) / ticks);   
        }
      }
      cprintf("c->ticks: %d ticks: %d \n", ctable.cont[i].ticks, ticks);
      cprintf("container usage: %d percent of cpu\n", (ctable.cont[i].ticks * 100) / ticks);
      cprintf("\n");
    }
    

  }


  return 1;
}



