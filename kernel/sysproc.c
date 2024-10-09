#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0) return -1;
  if (argint(1, &flags) < 0) return -1;
  return wait(p, flags);
}

uint64 sys_yield(void) {
  struct proc *p = myproc();
  uint64 user_pc = p->trapframe->epc;
  acquire(&p->lock);  // 获取打印锁
  // 打印当前进程的上下文保存地址区间
  printf("Save the context of the process to the memory region from address %p to %p\n", &p->context,
         (char *)&p->context + sizeof(p->context));

  // 打印当前进程的pid和用户态PC值
  printf("Current running process pid is %d and user pc is %p\n", p->pid, user_pc);
  release(&p->lock);  // 释放打印锁
  // 模拟调度器找下一个RUNNABLE进程
  struct proc *np;
  int found = 0;
  for (int i = 0; i < NPROC; i++) {
    np = &proc[(p - proc + i + 1) % NPROC];
    acquire(&np->lock);
    if (np != p) {
      if (np->state == RUNNABLE) {
        printf("Next runnable process pid is %d and user pc is %p\n", np->pid, np->trapframe->epc);
        found = 1;
        release(&np->lock);
        break;
      }
    }
    release(&np->lock);
  }

  if (!found) {
    printf("No other RUNNABLE process found\n");
  }

  // 调用内核的yield函数挂起当前进程
  yield();

  return 0;
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}
