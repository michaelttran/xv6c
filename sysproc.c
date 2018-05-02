#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int sys_getcid(void)
{
  struct proc *curproc = myproc();

  if (curproc->cont == 0) {
    return 0;
  }
  return curproc->cont->cid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_cstart(void)
{
  int vcnode, used_disk, proc, mem, max_disk;
  char *path;

  if (argint(0, &vcnode) < 0 || argstr(1, &path) < 0 || argint(2, &used_disk) < 0) {
    return -1;
  }
  if (argint(3, &proc) < 0 || argint(4, &mem) < 0 || argint(5, &max_disk) < 0) {
    return -1;
  }

  return spawn_cont(vcnode, path, used_disk, proc, mem, max_disk);
}

int
sys_writeprocs(void)
{
  cprocdump();
  return 1;
}

int
sys_writemem(void)
{
  return memdump();
}

int
sys_printstats(void) {
  printdump();
  return 1;
}

int 
sys_cpause(void){
  int cid;

  if (myproc()->cont != 0) {
    return -1;
  }

  if(argint(0, &cid) < 0){
    return -1;
  }

  return cpause(cid);
}

int 
sys_cstop(void){
  int cid;

  if (myproc()->cont != 0) {
    return -1;
  }

  if(argint(0, &cid) < 0){
    return -1;
  }

  return kill_cont(cid);
}

int 
sys_cresume(void){
  int cid;

  if (myproc()->cont != 0) {
    return -1;
  }

  if(argint(0, &cid) < 0){
    return -1;
  }

  return cresume(cid);
}

int
sys_cfork(void)
{
  int cid;

  if (myproc()->cont != 0) {
    return -1;
  }

  if (argint(0, &cid) < 0) {
    return -1;
  }
  return cfork(cid);
}

int 
sys_dfmem(void) {
  return df_mem();
}

int 
sys_tdiskused(void) {
  int used_disk;

  if (argint(0, &used_disk) < 0) {
    return -1;
  }
  return total_used_disk(used_disk);
}

int
sys_cinfo(void) {
  if (myproc()->cont != 0) {
    return -1;
  }
  return c_info();
}
