#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "uproc.h"
#include "pdx.h"

#ifdef CS333_P3
#define statecount NELEM(states) 
#endif

static char *states[] = {
[UNUSED]    "unused",
[EMBRYO]    "embryo",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
};

#ifdef CS333_P3
struct ptrs {
  struct proc* head;
  struct proc* tail;
}; 
#endif 

static struct {
  struct spinlock lock;
  struct proc proc[NPROC];
#ifdef CS333_P3
  struct ptrs list[statecount];
#endif
#ifdef CS333_P4
  struct ptrs ready[MAXPRIO+1];
  uint PromoteAtTime;
#endif
} ptable;

static struct proc *initproc;

uint nextpid = 1;
extern void forkret(void);
extern void trapret(void);
static void wakeup1(void* chan);

#ifdef CS333_P3
// list management function prototypes 
static void initProcessLists(void);
static void initFreeList(void);
static void assertState(struct proc*, enum procstate, const char * func_name, int line);
static void stateListAdd(struct ptrs*, struct proc*);
static int  stateListRemove(struct ptrs*, struct proc* p);
static void promoteAll();
#endif

#ifdef CS333_P4 
static void assertPriority(struct proc*, int priority);
#endif 
// list management helper functions for P3 
#ifdef CS333_P3 
static void 
assertState(struct proc* p, enum procstate state, const char * func_name, int line)
{
  if(p->state != state)
  {
     cprintf("The Current State is %d\n", p->state);
     cprintf("The Expected State is %d\n", state);
     panic("Assert State Failed\n"); 
  }
}

#ifdef CS333_P4
static void 
assertPriority(struct proc* p, int priority)
{
  if(p->priority != priority)
  {
    cprintf("The current priority is %d\n", p->priority);
    cprintf("Expected priority is %d\n", priority);
    panic("Assert Priority Failed\n"); 
  }
}
#endif 

static void 
stateListAdd(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL)
  {
    (*list).head = p;
    (*list).tail = p;
    p->next = NULL;
  }
  else
  {
    ((*list).tail)->next = p; 
    (*list).tail = ((*list).tail)->next;
    ((*list).tail)->next = NULL;
  }
}

static int
stateListRemove(struct ptrs* list, struct proc* p)
{
  if((*list).head == NULL || (*list).tail == NULL || p == NULL){
    return -1;
  }

  struct proc* current = (*list).head; 
  struct proc* previous = 0; 

  if(current == p){
    (*list).head = ((*list).head)->next;
    if((*list).tail == p){
      (*list).tail = NULL;
    }
    return 0;
  }

  while(current){
    if(current == p){
      break;
    }

    previous = current;
    current = current->next;
  }

  // Process not found, return an error 
  if(current == NULL){
    return -1; 
  }
  
  //Process is found 
  if(current == (*list).tail){
    (*list).tail = previous;
    ((*list).tail)->next = NULL;
  }
  else{
    previous->next = current->next;
  }

  //Make sure p->next doesn't point into the list 
  p->next = NULL; 
  
  return 0;
}

static void 
initProcessLists()
{ 
  int i;

  for(i = UNUSED; i <= ZOMBIE; i++){
    ptable.list[i].head = NULL;
    ptable.list[i].tail = NULL;
  }
#ifdef CS333_P4
  for(i = 0; i <= MAXPRIO; i++){ 
    ptable.ready[i].head = NULL;
    ptable.ready[i].tail = NULL;
  }
#endif
}

static void 
initFreeList(void)
{
  struct proc* p;

  for(p = ptable.proc; p < ptable.proc + NPROC; p++){
    p->state = UNUSED;
    stateListAdd(&ptable.list[UNUSED], p);
  }
}

int 
getprocs(uint max, struct uproc* table) 
{
  int i = 0;
  struct proc *p; 
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC] && i < max; p++)
  {
    if(p->state != UNUSED && p->state != EMBRYO)
    {
      if(i == max)
      {
        break;
      }
      table[i].pid = p->pid; 
      safestrcpy(table[i].name, p->name, STRMAX); 
      table[i].uid = p->uid; 
      table[i].gid = p->gid; 
      
      if(p->parent)
      {
        table[i].ppid = p->parent->pid; 
      }
      else
      {
        table[i].ppid = p->pid; 
      }

      table[i].priority = p->priority;
      table[i].elapsed_ticks = ticks - p->start_ticks; 
      table[i].CPU_total_ticks = p->cpu_ticks_total; 

      safestrcpy(table[i].state, states[p->state], STRMAX); 
      table[i].size = p->sz; 
      i++;
    }
    
  }
  release(&ptable.lock);

  return i;
}
#endif

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
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
    if (cpus[i].apicid == apicid) {
      return &cpus[i];
    }
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

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.

#ifdef CS333_P3
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp; 
  int rc; 

  acquire(&ptable.lock); 

  //TRANSITION: UNUSED -> EMBRYO 
  p = ptable.list[UNUSED].head; 
  if(p)
  {
    rc = stateListRemove(&ptable.list[p->state], p);
    
    if(rc == 0)
    {
      assertState(p, UNUSED, __func__, __LINE__);
      p->state = EMBRYO; 
      stateListAdd(&ptable.list[p->state], p);
      p->pid = nextpid++;
    }
    else
    {
      panic("Error in allocproc(). Removal failed :(");
    }
  }

  //TRANSITION: EMBRYO -> UNUSED
  if((p->kstack = kalloc()) == 0){
    rc = stateListRemove(&ptable.list[p->state], p);
    assertState(p, EMBRYO, __func__, __LINE__);
    if(rc == 0){
      p->state = UNUSED; 
      stateListAdd(&ptable.list[p->state], p);
    }
    else
    {
      panic("Failure in allocproc()");
    }
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

  p->start_ticks = ticks;  // initialize field to global kernel var
  p->cpu_ticks_total = 0; 
  p->cpu_ticks_in = 0; 
  #ifdef CS333_P4
  p->priority = MAXPRIO;
  p->budget = BUDGET;
  #endif 
  release(&ptable.lock);

  return p;
}

#else 
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp; 
  
  acquire(&ptable.lock);
  int found = 0;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED) {
      found = 1;
      break;
    }
  if (!found) {
    release(&ptable.lock);
    return 0;
  } 
  p->state = EMBRYO;
  p->pid = nextpid++;
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

  p->start_ticks = ticks;  // initialize field to global kernel var
  p->cpu_ticks_total = 0; 
  p->cpu_ticks_in = 0; 

  return p;
}
#endif

// Set up first user process.
#ifdef CS333_P3
void
userinit(void) 
{
  struct proc *p;
  int rc;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  acquire(&ptable.lock);
  initProcessLists();
  initFreeList();
  #ifdef CS333_P4
  ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
  #endif
  release(&ptable.lock); 

  p = allocproc(); //points to single EMBRYO process 
  
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

  //TRANSITION: EMBRYO -> RUNNABLE
  acquire(&ptable.lock);
  if(p)
  {
    rc = stateListRemove(&ptable.list[p->state], p);
    assertState(p, EMBRYO, __func__, __LINE__); 
    if(rc == 0){
      p->state = RUNNABLE;
      #ifdef CS333_P4 
      stateListAdd(&ptable.ready[p->priority], p); 
      #elif CS333_P3
      stateListAdd(&ptable.list[p->state], p);
      #endif
    }
    else
    {
      panic("Failure in userinit() P3"); 
    }
  }

 release(&ptable.lock);
}

#else 
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
#endif

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
  return 0;
}
// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.

#ifdef CS333_P3
int 
fork(void)
{
  int i, rc;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  //TRANSITION: EMBRYO -> UNUSED
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    acquire(&ptable.lock);
    rc = stateListRemove(&ptable.list[np->state], np);
    assertState(np, EMBRYO, __func__, __LINE__);
    if(rc == 0)
    {
      np->state = UNUSED; 
      stateListAdd(&ptable.list[np->state], np);
      release(&ptable.lock);
      return -1;
    }
    else
    {
      panic("Error in fork()");
    }
  }

  np->sz = curproc->sz;
  np->parent = curproc;
  np->uid = np->parent->uid; 
  np->gid = np->parent->gid;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  
  //TRANSITION: EMBRYO -> RUNNABLE

  acquire(&ptable.lock);

  rc = stateListRemove(&ptable.list[np->state], np); 
  assertState(np, EMBRYO, __func__, __LINE__);
  if(rc == 0){
    np->state = RUNNABLE; 
    #ifdef CS333_P4
    stateListAdd(&ptable.ready[np->priority], np);
    #elif CS333_P3
    stateListAdd(&ptable.list[np->state], np);
    #endif
  }
  else
  {  
     panic("Failure in fork()");
  }
  
  release(&ptable.lock);

  return pid;
}

#else 
int
fork(void)
{
  int i;
  uint pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
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
  np->uid = np->parent->uid; 
  np->gid = np->parent->gid;
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
#endif 
// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.

#ifdef CS333_P3
void 
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p; 
  struct proc * temp;
  int fd, i, rc;
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
  p = ptable.list[EMBRYO].head;
  while(p)
  {
    temp = p->next;
    if(p->parent == curproc)
    {
      p->parent = initproc; 
    }
    p = temp;
  }
  
  p = ptable.list[SLEEPING].head;
  while(p)
  {
    temp = p->next;
    if(p->parent == curproc)
    {
      p->parent = initproc; 
    }
    p = temp;
  }
 
  p = ptable.list[RUNNING].head;
  while(p)
  {
    temp = p->next;
    if(p->parent == curproc)
    {
      p->parent = initproc; 
    }
    p = temp;
  }

  p = ptable.list[ZOMBIE].head;
  while(p)
  {
    temp = p->next;
    if(p->parent == curproc)
    {
      p->parent = initproc;
      wakeup1(initproc);
    }
    p = temp;
  }

  #ifdef CS333_P4
  for(i = 0; i < MAXPRIO + 1; ++i){
    p = ptable.ready[i].head; 
    while(p)
    {
      temp = p->next;  
      if(p->parent == curproc){
        p->parent = initproc;
      }
      p = temp;
    }
  }
  #endif

  // Jump into the scheduler, never to return.
  rc = stateListRemove(&ptable.list[curproc->state], curproc); 
  assertState(curproc, RUNNING, __func__, __LINE__);
  if(rc == 0)
  { 
    curproc->state = ZOMBIE;
    stateListAdd(&ptable.list[curproc->state], curproc);
  }
  else
  {
    panic("error in exit()");
  }

  sched();
  panic("zombie exit");
}

#else
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
#endif

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
#ifdef CS333_P3
int 
wait(void)
{
  struct proc *p;
  int havekids, rc, i;
  uint pid;
  struct proc *curproc = myproc(); 

  acquire(&ptable.lock); 
  for(;;){
    // Scan through table looking for exited children. 
    havekids = 0; 
    for(i = EMBRYO; i <= ZOMBIE; i++){
      p = ptable.list[i].head; 
      while(p && i != RUNNABLE){ //Avoids going through RUNNABLE list! 
        if(p->parent != curproc)
	{
          p = p->next; 
	  continue;
	}
        havekids = 1; 
        if(p->state == ZOMBIE){
	  // Found one.
	  pid = p->pid; 
	  kfree(p->kstack);
	  p->kstack = 0;
	  freevm(p->pgdir);
          rc = stateListRemove(&ptable.list[p->state], p); 
          assertState(p, ZOMBIE, __func__, __LINE__);
          if(rc == 0){
	    p->pid = 0;
	    p->parent = 0;
	    p->name[0] = 0;
	    p->killed = 0;
	    p->state = UNUSED; 
	    stateListAdd(&ptable.list[p->state], p);
	    release(&ptable.lock);
	    return pid;
	  }
	  else
	  {
            panic("Error in wait()");
	  }
	}
	p = p->next; 
      }
     } 
     
     #ifdef CS333_P4
     for(i = 0; i < MAXPRIO + 1; ++i){
       for(p = ptable.ready[i].head; p; p = p->next){
         if(p->parent == curproc){
	   havekids = 1;
	 }
       }
     }
     #endif

     // No point waiting if we don't have any children.
     if(!havekids || curproc->killed){
       release(&ptable.lock);
       return -1;
     }
     
     // Wait for children to exit. 
     sleep(curproc, &ptable.lock);
   }    
}

#else
int
wait(void)
{
  struct proc *p;
  int havekids;
  uint pid;
  struct proc *curproc = myproc();

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
#endif

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
#ifdef CS333_P3
void
scheduler(void)
{ 
  int rc, i;
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){ //infinite loop to find runnable processes.
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    //TRANSITION: RUNNABLE -> RUNNING
    acquire(&ptable.lock);
#ifdef CS333_P4
    if(ticks >= ptable.PromoteAtTime)
    {
      promoteAll(); 
      ptable.PromoteAtTime = ticks + TICKS_TO_PROMOTE;
    }
    for(i = MAXPRIO; i >= 0; --i){
      p = ptable.ready[i].head; 
      if(p)
      {
        idle = 0;
        c->proc = p; 
	switchuvm(p);
        rc = stateListRemove(&ptable.ready[p->priority], p);
        assertState(p, RUNNABLE, __func__, __LINE__);
	assertPriority(p, p->priority);
        if(rc == 0){
          p->state = RUNNING;
	  stateListAdd(&ptable.list[p->state], p); 
          p->cpu_ticks_in = ticks;
          swtch(&(c->scheduler), p->context);
          switchkvm();
          c->proc = 0;
	  break;
         } 
         else 
         {
          panic("Failure in Scheduler: P4");
         }
       }
    }
#elif CS333_P3 //THIS IS ONLY RUNS DURING P3!! NOT USED IN P4!!!
    p = ptable.list[RUNNABLE].head;
    if(p)
    {
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      rc = stateListRemove(&ptable.list[p->state], p);
      assertState(p, RUNNABLE, __func__, __LINE__);
      if(rc == 0){
	p->state = RUNNING;
	stateListAdd(&ptable.list[p->state], p);
	p->cpu_ticks_in = ticks;
        swtch(&(c->scheduler), p->context);
        switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      else
      {
	panic("Failure in Scheduler: P3");
      }
    }
#endif
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}

#else
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
#ifdef PDX_XV6
  int idle;  // for checking if processor is idle
#endif // PDX_XV6

  for(;;){
    // Enable interrupts on this processor.
    sti();

#ifdef PDX_XV6
    idle = 1;  // assume idle unless we schedule a process
#endif // PDX_XV6
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
#ifdef PDX_XV6
      idle = 0;  // not idle this timeslice
#endif // PDX_XV6
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      p->cpu_ticks_in = ticks;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);
#ifdef PDX_XV6
    // if idle, wait for next interrupt
    if (idle) {
      sti();
      hlt();
    }
#endif // PDX_XV6
  }
}
#endif

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
  p->cpu_ticks_total += ticks - (p->cpu_ticks_in);
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
#ifdef CS333_P3
void
yield(void)
{
  struct proc *curproc = myproc();
  int rc; 
  
  acquire(&ptable.lock); 
  //TRANSITION: RUNNING -> RUNNABLE
  rc = stateListRemove(&ptable.list[curproc->state], curproc);
  assertState(curproc, RUNNING, __func__, __LINE__);
  if(rc == 0)
  {
    curproc->state = RUNNABLE;
    #ifdef CS333_P4
    curproc->budget = curproc->budget - (ticks - curproc->cpu_ticks_in);
    if(curproc->budget <= 0 && curproc->priority > 0) 
    {
      curproc->priority = curproc->priority - 1;
      curproc->budget = BUDGET; 
    }
    stateListAdd(&ptable.ready[curproc->priority], curproc);
    #elif CS333_P3
    stateListAdd(&ptable.list[curproc->state], curproc); 
    #endif  
  }
  else
  {
    panic("FAILURE IN YIELD");
  }

  sched();
  release(&ptable.lock);
}

#else
void
yield(void)
{
  struct proc *curproc = myproc();

  acquire(&ptable.lock);  //DOC: yieldlock
  curproc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}
#endif

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
#ifdef CS333_P3
void 
sleep(void *chan, struct spinlock *lk)
{ 
  struct proc *p = myproc(); //myproc is currently running process 
  int rc; 

  if(p == 0)
    panic("sleep"); 

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
  }
  p->chan = chan;
  //TRANSITION: RUNNING -> SLEEPING
  rc = stateListRemove(&ptable.list[p->state], p);
  assertState(p, RUNNING, __func__, __LINE__);
  if(rc == 0)
  { 
    p->state = SLEEPING; 
    #ifdef CS333_P4
    p->budget = p->budget - (ticks - p->cpu_ticks_in);
    if(p->budget <= 0 && p->priority > 0)
    {
      p->priority = p->priority - 1;
      p->budget = BUDGET;
    }
    #endif
    stateListAdd(&ptable.list[p->state], p);
  }
  else
  {
    panic("Unable to remove in SLEEP()");
  }
 
  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    if (lk) acquire(lk);
  }
}

#else
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); //myproc is currently running process 
  
  if(p == 0)
    panic("sleep");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    if (lk) release(lk);
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
    if (lk) acquire(lk);
  }
}
#endif 

// Wake up all processes sleeping on chan.
// The ptable lock must be held.

#ifdef CS333_P3
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct proc *temp; 
  int rc; 

  //TRANSITION: SLEEPING -> RUNNABLE
  p = ptable.list[SLEEPING].head;
  while(p)
  {
    temp = p->next;
    if(p->chan == chan){
      rc = stateListRemove(&ptable.list[p->state], p); 
      assertState(p, SLEEPING, __func__, __LINE__);
      if(rc == 0) 
      {
        p->state = RUNNABLE;
        #ifdef CS333_P4
	stateListAdd(&ptable.ready[p->priority], p);
        #elif CS333_P3
        stateListAdd(&ptable.list[p->state], p);
	#endif 
      }
      else 
      {
        panic("Error in Wakeup1()");
      }
    }
    p = temp;
  }
}

#else
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}
#endif 

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

#ifdef CS333_P3
int 
kill(int pid)
{
  struct proc *p;
  struct proc *temp;
  int i, rc; 

  acquire(&ptable.lock);
  
  p = ptable.list[SLEEPING].head;
  while(p)
  {
    temp = p->next; 
    if(p->pid == pid)
    {
      p->killed = 1; 
      rc = stateListRemove(&ptable.list[p->state], p); 
      assertState(p, SLEEPING, __func__, __LINE__);
      if(rc == 0)
      {
        p->state = RUNNABLE; 
	#ifdef CS333_P4 
	stateListAdd(&ptable.ready[p->priority], p);
	#elif CS333_P3
	stateListadd(&ptable.list[p->state], p);
	#endif
	release(&ptable.lock);
	return 0;
      }
    }
    p = temp;
  }

  p = ptable.list[EMBRYO].head; 
  while(p)
  {
    temp = p->next;
    if(p->pid == pid)
    {
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = temp;
  }

  p = ptable.list[RUNNING].head; 
  while(p)
  {
    temp = p->next;
    if(p->pid == pid)
    {
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = temp;
  }

  p = ptable.list[ZOMBIE].head; 
  while(p)
  {
    temp = p->next;
    if(p->pid == pid)
    {
      p->killed = 1;
      release(&ptable.lock);
      return 0;
    }
    p = temp;
  }


  #ifdef CS333_P4
  for(i = 0; i < MAXPRIO +1; ++i) 
  {
    p = ptable.ready[i].head; 
    while(p)
    {
      temp = p->next; 
      if(p->pid == pid){
        p->killed = 1;
	release(&ptable.lock);
	return 0;
      }
      p = temp;
    }
   }
   #endif 
   release(&ptable.lock); 
   return -1; 
}

#else
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}
#endif 

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.

#ifdef CS333_P3
void
ctrlr(void)
{

  struct proc *p;
  struct proc *temp;

  acquire(&ptable.lock);
  #ifdef CS333_P4
  cprintf("\nReady List Processes:\n");
  for (int i = MAXPRIO; i >= 0; i--)
  {
    cprintf("\n");
    cprintf("Priority Level %d: ", i);
    p = ptable.ready[i].head; 
    while(p)
    {
      temp = p->next; 
      cprintf("(%d, %d)", p->pid, p->budget);
      if(temp)
      {
        cprintf(" -> "); 
      }
      p = temp;
    }
    cprintf("\n");
  }
  cprintf("\n");

  #elif CS333_P3
  p = ptable.list[RUNNABLE].head; 
  cprintf("\nReady List Processes:\n");
  while(p)
  { 
     temp = p->next; 
     cprintf("%d", p->pid);
     if(temp)
     {
       cprintf(" -> ");
     }
     p = temp; 
  }
  cprintf("\n");
  #endif 
  release(&ptable.lock);
}

void 
ctrlf(void)
{ 
  struct proc *p; 
  int numFree = 0; 
  acquire(&ptable.lock);
  p = ptable.list[UNUSED].head;
  while(p)
  {
     numFree = numFree + 1; 
     p = p->next;
  }
  release(&ptable.lock);
  cprintf("\nFree List Size: %d processes\n", numFree);
  cprintf("\n");
}

void
ctrls(void)
{
  struct proc *p;
  struct proc *temp; 
  acquire(&ptable.lock);
  p = ptable.list[SLEEPING].head;
  cprintf("\nSleep List Processes:\n");
  while(p)
  {
    temp = p->next;
    cprintf("%d", p->pid);
    if(temp)
    {
      cprintf(" -> ");
    }
    p = temp;
  }
  cprintf("\n");
  release(&ptable.lock);
}

void 
ctrlz(void)
{ 
  struct proc *p;
  struct proc *temp;
  acquire(&ptable.lock);
  p = ptable.list[ZOMBIE].head;
  cprintf("\nZombie List Processes:\n");
  while(p)
  {
    temp = p->next;
    cprintf("(%d, %d)", p->pid, p->parent->pid);
    if(temp)
    {
      cprintf(" -> ");
    }
    p = temp;
  }
  cprintf("\n");
  release(&ptable.lock);  
}
#endif

void
procdump(void)
{
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
#if defined(CS333_P4)
#define HEADER "\nPID\tName\tUID\tGID\tPPID\tPrio\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P2)
#define HEADER "\nPID\tName\tUID\tGID\tPPID\tElapsed\tCPU\tState\tSize\t PCs\n"
#elif defined(CS333_P1)
#define HEADER "\nPID\tName\tElapsed\tState\tSize\t PCs\n"
#else
#define HEADER "\n"
#endif
  cprintf(HEADER);  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
#if defined(CS333_P4)
    procdumpP4(p, state);
#elif defined(CS333_P2)
    procdumpP2(p, state);
#elif defined(CS333_P1)
    procdumpP1(p, state);
#else
    cprintf("%d\t%s\t%s\t", p->pid, p->name, state);
#endif

    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


#ifdef CS333_P4
void 
procdumpP4(struct proc *p, char *state)
{
  uint seconds, milliseconds, cpu_seconds, cpu_milli;

  cprintf("%d\t", p->pid);
  cprintf("%s\t", p->name);
  cprintf("%d\t", p->uid);
  cprintf("%d\t", p->gid);
  if(p->parent != NULL)
    cprintf("%d\t", p->parent->pid); 
  else 
    cprintf("%d\t", p->pid);
  cprintf("%d\t", p->priority); 
  seconds = (ticks - p->start_ticks)/1000;
  milliseconds = (ticks - p->start_ticks) % 1000; 
  if(milliseconds < 10)
    cprintf("%d.00%d\t", seconds,  milliseconds);
  else if(milliseconds < 100)
    cprintf("%d.0%d\t", seconds,  milliseconds);
  else 
    cprintf("%d.%d\t", seconds,  milliseconds);
  
  cpu_seconds = (p->cpu_ticks_total)/1000; 
  cpu_milli = (p->cpu_ticks_total) % 1000; 
  if(cpu_milli < 10)
    cprintf("%d.00%d\t", cpu_seconds,  cpu_milli);
  else if(cpu_milli < 100)
    cprintf("%d.0%d\t", cpu_seconds,  cpu_milli);
  else 
    cprintf("%d.%d\t", cpu_seconds,  cpu_milli);
  cprintf("%s\t", state);
  cprintf("%d\t", p->sz); 

}
#endif 

#ifdef CS333_P2
void
procdumpP2(struct proc *p, char *state)
{ 
  uint seconds, milliseconds, cpu_seconds, cpu_milli;

  cprintf("%d\t", p->pid);
  cprintf("%s\t", p->name);
  cprintf("%d\t", p->uid);
  cprintf("%d\t", p->gid);
  if(p->parent != NULL)
    cprintf("%d\t", p->parent->pid); 
  else 
    cprintf("%d\t", p->pid);
  cprintf("d\t", p->priority); 
  seconds = (ticks - p->start_ticks)/1000;
  milliseconds = (ticks - p->start_ticks) % 1000; 
  if(milliseconds < 10)
    cprintf("%d.00%d\t", seconds,  milliseconds);
  else if(milliseconds < 100)
    cprintf("%d.0%d\t", seconds,  milliseconds);
  else 
    cprintf("%d.%d\t", seconds,  milliseconds);
  
  cpu_seconds = (p->cpu_ticks_total)/1000; 
  cpu_milli = (p->cpu_ticks_total) % 1000; 
  if(cpu_milli < 10)
    cprintf("%d.00%d\t", cpu_seconds,  cpu_milli);
  else if(cpu_milli < 100)
    cprintf("%d.0%d\t", cpu_seconds,  cpu_milli);
  else 
    cprintf("%d.%d\t", cpu_seconds,  cpu_milli);
  cprintf("%s\t", state);
  cprintf("%d\t", p->sz); 
}
int 
setgid(uint _gid_)
{
  struct proc * p = myproc(); //passes current running process 
  acquire(&ptable.lock);
  if(_gid_ < 0 || _gid_ > 32767)
  {
    release(&ptable.lock);
    return -1; 
  }
  p->gid = _gid_;
  release(&ptable.lock);
  return 0;
} 

int 
setuid(uint _uid_)
{
  struct proc * p = myproc();
  acquire(&ptable.lock);
  if(_uid_ < 0 || _uid_ > 32767)
  {
    release(&ptable.lock);
    return -1; 
  }
  p->uid = _uid_;
  release(&ptable.lock);
  return 0;
}
#endif

#ifdef CS333_P1
void
procdumpP1(struct proc *p, char *state)
{
  uint seconds, milliseconds; 

  cprintf("%d\t", p->pid); 
  cprintf("%s\t", p->name);
  seconds = (ticks - p->start_ticks)/1000;
  milliseconds = (ticks - p->start_ticks) % 1000; 
  if(milliseconds < 10)
    cprintf("%d.00%d\t", seconds,  milliseconds);
  else if(milliseconds < 100)
    cprintf("%d.0%d\t", seconds,  milliseconds);
  else 
    cprintf("%d.%d\t", seconds,  milliseconds);
  cprintf("%s\t", state);
  cprintf("%d\t", p->sz); 
}
#endif 

#ifdef CS333_P4
int 
setpriority(int pid, int priority)
{
  struct proc * p; 
  struct proc * temp; 
  int i, rc;
  int onReady = 0; 
  acquire(&ptable.lock);
  //Go through priority queues and look for desired PID
  if(pid < 0 || pid > 32767 || priority > MAXPRIO || priority < 0)
  {
    release(&ptable.lock);
    return -1;
  }
  //Go throught ready lists and find the PID
  for(i = 0; i <= MAXPRIO; i++)
  {
    p = ptable.ready[i].head; 
    while(p)
    { 
      temp = p->next; 
      if(p->pid == pid){
        onReady = 1; 
	goto match; 
      }
      p = temp;
    }
  }
  //Go through SLEEPING list and find PID
  p = ptable.list[SLEEPING].head; 
  while(p)
  {
    temp = p->next; 
    if(p->pid == pid) 
    {
      goto match; 
    }
    p = temp; 
  }
 //Go through RUNNING list and find PID
  p = ptable.list[RUNNING].head; 
  while(p)
  {
    temp = p->next; 
    if(p->pid == pid) 
    {
      goto match; 
    }
    p = temp; 
  }
  release(&ptable.lock); 
  return -1;

  match: 
    if(onReady == 1)
    {
      if(priority != p->priority)
      {
        rc = stateListRemove(&ptable.ready[p->priority], p);
        assertState(p, RUNNABLE, __func__, __LINE__);
        assertPriority(p, p->priority);
        if(rc == 0)
        {
        
          p->priority = priority; 
	  p->budget = BUDGET; 
	  stateListAdd(&ptable.ready[p->priority], p); 
        }
        else
        {
          panic("Error CANNOT remove that process :(");
        }
      }
    }
    else
    {
      if(priority != p->priority)
      {
        p->priority = priority; 
        p->budget = BUDGET;
      }
    }
    release(&ptable.lock);
    return 0;
}

int getpriority(int pid)
{ 

  struct proc * p;
  struct proc * temp; 
  int i; 
  acquire(&ptable.lock);
 //Go through priority queues and look for desired PID
  for(i = 0; i < MAXPRIO+1; i++)
  {
    p = ptable.ready[i].head; 
    while(p)
    { 
      temp = p->next;
      assertState(p, RUNNABLE, __func__, __LINE__);
      if(p->pid == pid)
      {
	release(&ptable.lock);
        return p->priority;
      }
      p = temp;
    }
  }

  //Go through SLEEPING list and find PID
  p = ptable.list[SLEEPING].head; 
  while(p)
  {
    temp = p->next; 
    assertState(p, SLEEPING, __func__, __LINE__); 
    if(p->pid == pid) 
    {
      release(&ptable.lock); 
      return p->priority; 
    }
    p = temp; 
  }
  
  //Go through RUNNING list and find PID
  p = ptable.list[RUNNING].head; 
  while(p)
  {
    temp = p->next; 
    assertState(p, RUNNING, __func__, __LINE__); 
    if(p->pid == pid) 
    {
      release(&ptable.lock); 
      return p->priority; 
    }
    p = temp; 
  }


  release(&ptable.lock);
  return -1;
}

static void 
promoteAll() 
{
  struct proc* active; 
  struct proc* temp;
  int rc, i;
  if(MAXPRIO == 0)
  {
    return;
  }
  for(i = MAXPRIO - 1; i >= 0; i--) 
  {
    active = ptable.ready[i].head;
    while(active)
    {
      temp = active->next;
     
      if(active->priority < MAXPRIO)
      { 
        rc = stateListRemove(&ptable.ready[active->priority], active); 
	assertState(active, RUNNABLE, __func__, __LINE__);
	assertPriority(active, active->priority);
        if(rc == 0) 
	{
          active->priority =  active->priority + 1;
	  active->budget = BUDGET;
          stateListAdd(&ptable.ready[active->priority], active); 
        }
	else
	{
          panic("Error in Promotion"); 
	}
       }
       active = temp;
     }
  }
  
  active = ptable.list[SLEEPING].head; 
  while(active)
  {
    if(active->priority < MAXPRIO)
    {
      active->priority = active->priority + 1;
      active->budget = BUDGET;
    }
    active = active->next; 
  }

  active = ptable.list[RUNNING].head; 
  while(active)
  {
    if(active->priority < MAXPRIO)
    {
      active->priority = active->priority + 1;
      active->budget = BUDGET;
    }
    active = active->next; 
  }

}
#endif 

