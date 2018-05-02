// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

int used_mem;
int total_mem;
int used_disk;
int total_disk;
uint last_tick;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  uint ticks;                  // Number of ticks process has been running
  struct container *cont;      // Pointer to process's container
  uint last_tick;              // Tick that it was on when called for scheduling
};

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap

struct container {
  int cid;                            // Container ID
  struct proc *inner_ptable[NPROC];   // Table of containers processes
  enum procstate save_state[NPROC];   // Table of the saved states for a paused container
  int vc_node;                        // The minor node of the virtual console attached to the container
  int total_proc;                     // The total number of processes allowed
  int used_mem;                       // The amount of memory being used by the container
  int total_mem;                      // The total amount of memory allowed for the container
  int used_disk;                      // The amount of disk space used by the container
  int total_disk;                     // The total amount of disk space allowed for the container
  struct inode *root_dir;             // A pointer to the containers 'root' directory
  char name[16];                      // The name of the containers 'root' directory
  uint ticks;                         // Number of ticks container has been running
  uint last_tick;                     // Tick that it was on when called for scheduling
  int awake;
  int tokill;
};

int spawn_cont(int vcnode, char *path, int used_disk, int max_proc, int max_mem, int max_disk);
void cprocdump(void);
int memdump(void);
void printdump(void);
int set_cont(int cid);
int reset_cont(void);

// Helper functions for ctool system calls
int cpause(int cid);
int cstop(int cid);
int cresume(int cid);
int cfork(int cid);
int proc_print(struct proc*);
int kill_cont(int cid);
int df_mem(void);
int total_used_disk(int used_disk);
int c_info();
