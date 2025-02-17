// thread.cc 
//	Routines to manage threads.  There are four main operations:
//
//	Fork -- create a thread to run a procedure concurrently
//		with the caller (this is done in two steps -- first
//		allocate the Thread object, then call Fork on it)
//	Finish -- called when the forked procedure finishes, to clean up
//	Yield -- relinquish control over the CPU to another ready thread
//	Sleep -- relinquish control over the CPU, but thread is now blocked.
//		In other words, it will not run again, until explicitly 
//		put back on the ready queue.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "thread.h"
#include "switch.h"
#include "synch.h"
#include "system.h"

#define STACK_FENCEPOST 0xdeadbeef	// this is put at the top of the
					// execution stack, for detecting 
					// stack overflows

//----------------------------------------------------------------------
// Thread::Thread
// 	Initialize a thread control block, so that we can then call
//	Thread::Fork.
//
//	"threadName" is an arbitrary string, useful for debugging.
//----------------------------------------------------------------------

Thread::Thread(char* threadName) {
    DEBUG('t', "Initing thread \"%s\"\n", threadName);
    strcpy(name, threadName);
    stackTop = NULL;
    stack = NULL;
    status = JUST_CREATED;
#ifdef USER_PROGRAM
    pcb = new PCB();
#endif
}

//----------------------------------------------------------------------
// Thread::~Thread
// 	De-allocate a thread.
//
// 	NOTE: the current thread *cannot* delete itself directly,
//	since it is still running on the stack that we need to delete.
//
//      NOTE: if this is the main thread, we can't delete the stack
//      because we didn't allocate it -- we got it automatically
//      as part of starting up Nachos.
//----------------------------------------------------------------------

Thread::~Thread() {
    DEBUG('t', "Deleting thread \"%s\"\n", name);
    ASSERT(this != currentThread);
    if (stack != NULL)
		DeallocBoundedArray((char *) stack, StackSize * sizeof(_int));
#ifdef USER_PROGRAM
    if (pcb)
        delete pcb;
    pcb = NULL;
#endif
}

//----------------------------------------------------------------------
// Thread::Fork
// 	Invoke (*func)(arg), allowing caller and callee to execute 
//	concurrently.
//
//	NOTE: although our definition allows only a single integer argument
//	to be passed to the procedure, it is possible to pass multiple
//	arguments by making them fields of a structure, and passing a pointer
//	to the structure as "arg".
//
// 	Implemented as the following steps:
//		1. Allocate a stack
//		2. Initialize the stack so that a call to SWITCH will
//		cause it to run the procedure
//		3. Put the thread on the ready queue
// 	
//	"func" is the procedure to run concurrently.
//	"arg" is a single argument to be passed to the procedure.
//----------------------------------------------------------------------

void 
Thread::Fork(VoidFunctionPtr func, _int arg) {
#ifdef HOST_ALPHA
    DEBUG('t', "Forking thread \"%s\" with func = 0x%lx, arg = %ld\n",
	  name, (long) func, arg);
#else
    DEBUG('t', "Forking thread \"%s\" with func = 0x%x, arg = %d\n",
	  name, (int) func, arg);
#endif
    
    StackAllocate(func, arg);

    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    scheduler->ReadyToRun(this);	// ReadyToRun assumes that interrupts 
					// are disabled!
    (void) interrupt->SetLevel(oldLevel);
}    

//----------------------------------------------------------------------
// Thread::CheckOverflow
// 	Check a thread's stack to see if it has overrun the space
//	that has been allocated for it.  If we had a smarter compiler,
//	we wouldn't need to worry about this, but we don't.
//
// 	NOTE: Nachos will not catch all stack overflow conditions.
//	In other words, your program may still crash because of an overflow.
//
// 	If you get bizarre results (such as seg faults where there is no code)
// 	then you *may* need to increase the stack size.  You can avoid stack
// 	overflows by not putting large data structures on the stack.
// 	Don't do this: void foo() { int bigArray[10000]; ... }
//----------------------------------------------------------------------

void
Thread::CheckOverflow() {
    if (stack != NULL)
#ifdef HOST_SNAKE			// Stacks grow upward on the Snakes
	ASSERT((unsigned int)stack[StackSize - 1] == STACK_FENCEPOST);
#else
	ASSERT((unsigned int)*stack == STACK_FENCEPOST);
#endif
}

//----------------------------------------------------------------------
// Thread::Finish
// 	Called by ThreadRoot when a thread is done executing the 
//	forked procedure.
//
// 	NOTE: we don't immediately de-allocate the thread data structure 
//	or the execution stack, because we're still running in the thread 
//	and we're still on the stack!  Instead, we set "threadToBeDestroyed", 
//	so that Scheduler::Run() will call the destructor, once we're
//	running in the context of a different thread.
//
// 	NOTE: we disable interrupts, so that we don't get a time slice 
//	between setting threadToBeDestroyed, and going to sleep.
//----------------------------------------------------------------------

//
void
Thread::Finish () {
    (void) interrupt->SetLevel(IntOff);		
    ASSERT(this == currentThread);
#ifdef USER_PROGRAM
    // step 1: 获取waitingList
    List *waitingList = scheduler->getWaitingList();
    // step 2: 
    // 在waitingList中找到Joiner
    // 将其从队列移除, 调度运行Joiner
    Thread *thread = FindThread(waitingList, pcb->parentPid);
    if (thread) {
        thread->pcb->waitProcessExitCode = this->pcb->exitCode;
        scheduler->ReadyToRun(thread);
        waitingList->RemoveByItem(thread);
    }
    // step 3：完成Joinee的终止收尾工作
    Terminated();
#else
    DEBUG('t', "Finishing thread \"%s\"\n", getName());
    threadToBeDestroyed = currentThread;
    Sleep();					// invokes SWITCH
    // not reached
#endif
}

//----------------------------------------------------------------------
// Thread::Yield
// 	Relinquish the CPU if any other thread is ready to run.
//	If so, put the thread on the end of the ready list, so that
//	it will eventually be re-scheduled.
//
//	NOTE: returns immediately if no other thread on the ready queue.
//	Otherwise returns when the thread eventually works its way
//	to the front of the ready list and gets re-scheduled.
//
//	NOTE: we disable interrupts, so that looking at the thread
//	on the front of the ready list, and switching to it, can be done
//	atomically.  On return, we re-set the interrupt level to its
//	original state, in case we are called with interrupts disabled. 
//
// 	Similar to Thread::Sleep(), but a little different.
//----------------------------------------------------------------------

void
Thread::Yield () {
    Thread *nextThread;
    IntStatus oldLevel = interrupt->SetLevel(IntOff);
    
    ASSERT(this == currentThread);
    
    DEBUG('t', "Yielding thread \"%s\"\n", getName());
    
    nextThread = scheduler->FindNextToRun();
    if (nextThread != NULL) {
	scheduler->ReadyToRun(this);
	scheduler->Run(nextThread);
    }
    (void) interrupt->SetLevel(oldLevel);
}

//----------------------------------------------------------------------
// Thread::Sleep
// 	Relinquish the CPU, because the current thread is blocked
//	waiting on a synchronization variable (Semaphore, Lock, or Condition).
//	Eventually, some thread will wake this thread up, and put it
//	back on the ready queue, so that it can be re-scheduled.
//
//	NOTE: if there are no threads on the ready queue, that means
//	we have no thread to run.  "Interrupt::Idle" is called
//	to signify that we should idle the CPU until the next I/O interrupt
//	occurs (the only thing that could cause a thread to become
//	ready to run).
//
//	NOTE: we assume interrupts are already disabled, because it
//	is called from the synchronization routines which must
//	disable interrupts for atomicity.   We need interrupts off 
//	so that there can't be a time slice between pulling the first thread
//	off the ready list, and switching to it.
//----------------------------------------------------------------------
void
Thread::Sleep () {
    Thread *nextThread;
    
    ASSERT(this == currentThread);
    ASSERT(interrupt->GetLevel() == IntOff);
    
    DEBUG('t', "Sleeping thread \"%s\"\n", getName());

    status = BLOCKED;
    while ((nextThread = scheduler->FindNextToRun()) == NULL)
	    interrupt->Idle();	// no one to run, wait for an interrupt
        
    scheduler->Run(nextThread); // returns when we've been signalled
}

//----------------------------------------------------------------------
// ThreadFinish, InterruptEnable, ThreadPrint
//	Dummy functions because C++ does not allow a pointer to a member
//	function.  So in order to do this, we create a dummy C function
//	(which we can pass a pointer to), that then simply calls the 
//	member function.
//----------------------------------------------------------------------

static void ThreadFinish()    { currentThread->Finish(); }
static void InterruptEnable() { interrupt->Enable(); }
void ThreadPrint(_int arg){ Thread *t = (Thread *)arg; t->Print(); }

//----------------------------------------------------------------------
// Thread::StackAllocate
//	Allocate and initialize an execution stack.  The stack is
//	initialized with an initial stack frame for ThreadRoot, which:
//		enables interrupts
//		calls (*func)(arg)
//		calls Thread::Finish
//
//	"func" is the procedure to be forked
//	"arg" is the parameter to be passed to the procedure
//----------------------------------------------------------------------

void
Thread::StackAllocate (VoidFunctionPtr func, _int arg) {
    stack = (int *) AllocBoundedArray(StackSize * sizeof(_int));

#ifdef HOST_SNAKE
    // HP stack works from low addresses to high addresses
    stackTop = stack + 16;	// HP requires 64-byte frame marker
    stack[StackSize - 1] = STACK_FENCEPOST;
#else
    // i386 & MIPS & SPARC & ALPHA stack works from high addresses to low addresses
#ifdef HOST_SPARC
    // SPARC stack must contains at least 1 activation record to start with.
    stackTop = stack + StackSize - 96;
#else  // HOST_MIPS  || HOST_i386 || HOST_ALPHA
    stackTop = stack + StackSize - 4;	// -4 to be on the safe side!
#ifdef HOST_i386
    // the 80386 passes the return address on the stack.  In order for
    // SWITCH() to go to ThreadRoot when we switch to this thread, the
    // return addres used in SWITCH() must be the starting address of
    // ThreadRoot.

    //    *(--stackTop) = (int)ThreadRoot;
    // This statement can be commented out after a bug in SWITCH function
    // of i386 has been fixed: The current last three instruction of 
    // i386 SWITCH is as follows: 
    // movl    %eax,4(%esp)            # copy over the ret address on the stack
    // movl    _eax_save,%eax
    // ret
    // Here "movl    %eax,4(%esp)" should be "movl   %eax,0(%esp)". 
    // After this bug is fixed, the starting address of ThreadRoot,
    // which is stored in machineState[PCState] by the next stament, 
    // will be put to the location pointed by %esp when the SWITCH function
    // "return" to ThreadRoot.
    // It seems that this statement was used to get around that bug in SWITCH.
    //
    // However, this statement will be needed, if SWITCH for i386 is
    // further simplified. In fact, the code to save and 
    // retore the return address are all redundent, because the
    // return address is already in the stack (pointed by %esp).
    // That is, the following four instructions can be removed:
    // ...
    // movl    0(%esp),%ebx            # get return address from stack into ebx
    // movl    %ebx,_PC(%eax)          # save it into the pc storage
    // ...
    // movl    _PC(%eax),%eax          # restore return address into eax
    // movl    %eax,0(%esp)            # copy over the ret address on the stack#    

    // The SWITCH function can be as follows:
//         .comm   _eax_save,4

//         .globl  SWITCH
// SWITCH:
//         movl    %eax,_eax_save          # save the value of eax
//         movl    4(%esp),%eax            # move pointer to t1 into eax
//         movl    %ebx,_EBX(%eax)         # save registers
//         movl    %ecx,_ECX(%eax)
//         movl    %edx,_EDX(%eax)
//         movl    %esi,_ESI(%eax)
//         movl    %edi,_EDI(%eax)
//         movl    %ebp,_EBP(%eax)
//         movl    %esp,_ESP(%eax)         # save stack pointer
//         movl    _eax_save,%ebx          # get the saved value of eax
//         movl    %ebx,_EAX(%eax)         # store it

//         movl    8(%esp),%eax            # move pointer to t2 into eax

//         movl    _EAX(%eax),%ebx         # get new value for eax into ebx
//         movl    %ebx,_eax_save          # save it
//         movl    _EBX(%eax),%ebx         # retore old registers
//         movl    _ECX(%eax),%ecx
//         movl    _EDX(%eax),%edx
//         movl    _ESI(%eax),%esi
//         movl    _EDI(%eax),%edi
//         movl    _EBP(%eax),%ebp
//         movl    _ESP(%eax),%esp         # restore stack pointer
	
//         movl    _eax_save,%eax

//         ret

    //In this case the above statement 
    //    *(--stackTop) = (int)ThreadRoot;
    // is necesssary. But, the following statement
    //    machineState[PCState] = (_int) ThreadRoot;
    // becomes redundant.

    // Peiyi Tang, ptang@titus.compsci.ualr.edu
    // Department of Computer Science
    // University of Arkansas at Little Rock
    // Sep 1, 2003



#endif
#endif  // HOST_SPARC
    *stack = STACK_FENCEPOST;
#endif  // HOST_SNAKE
    
    machineState[PCState] = (_int) ThreadRoot;
    machineState[StartupPCState] = (_int) InterruptEnable;
    machineState[InitialPCState] = (_int) func;
    machineState[InitialArgState] = arg;
    machineState[WhenDonePCState] = (_int) ThreadFinish;
}

#ifdef USER_PROGRAM
#include "machine.h"

//----------------------------------------------------------------------
// PCB::SaveUserState
//	Save the CPU state of a user program on a context switch.
//
//	Note that a user program thread has *two* sets of CPU registers -- 
//	one for its state while executing user code, one for its state 
//	while executing kernel code.  This routine saves the former.
//----------------------------------------------------------------------

PCB::PCB() {
    for (int i = 0; i < NumTotalRegs; i++)
        userRegisters[i] = 0;
    parentPid = 0;
    waitProcessExitCode = 0;
    waitProcessPid = 0;
    exitCode = 0;
    space = NULL;
    // 初始化文件表
#ifdef FILESYS
    for (int i = 3; i < MaxFileId; i++)
        files[i] = NULL;
    OpenFile *stdin = new OpenFile("stdin");
    files[0] = stdin;
    OpenFile *stdout = new OpenFile("stdout");
    files[1] = stdout;
    OpenFile *stderr = new OpenFile("stderr");
    files[2] = stderr;
#endif
}

PCB::~PCB() {
    if (space)
        delete space;
    space = NULL;

#ifdef FILESYS
    for (int i = 0; i < 3; i++) {
        delete files[i];
        files[i] = NULL;
    }   
    
    for (int i = 3; i < MaxFileId; i++)
        if (files[i])
            files[i] = NULL;
#endif
}

#ifdef FILESYS

int 
PCB::getFileDescriptor(OpenFile *openfile) {
    for (int i = 3; i < MaxFileId; i++) {
        if (!files[i]) {
            files[i] = openfile;
            return i;
        }
    }
    ASSERT(FALSE);
}

OpenFile *
PCB::getOpenFile(int fd) {
    ASSERT(fd < MaxFileId);
    return files[fd];
}

void
PCB::releaseFileDescriptor(int fd) {
    ASSERT(fd < MaxFileId);
    files[fd] = NULL;
}
#endif

void
PCB::SaveUserState() {
    for (int i = 0; i < NumTotalRegs; i++)
	    userRegisters[i] = machine->ReadRegister(i);
}

//----------------------------------------------------------------------
// PCB::RestoreUserState
//	Restore the CPU state of a user program on a context switch.
//
//	Note that a user program thread has *two* sets of CPU registers -- 
//	one for its state while executing user code, one for its state 
//	while executing kernel code.  This routine restores the former.
//----------------------------------------------------------------------

void
PCB::RestoreUserState() {
    for (int i = 0; i < NumTotalRegs; i++)
	    machine->WriteRegister(i, userRegisters[i]);
}

void
Thread::Join(int pid) {
    DEBUG('t', "Thread::Join: Now in thread \"%s\"\n", currentThread->getName());
    IntStatus oldLevel = interrupt->SetLevel(IntOff);       // 关中断
    // step 1: 记录Joinee的pid
    currentThread->pcb->waitProcessPid = pid;
    List *terminatedList = scheduler->getTerminatedList();
    List *waitingList = scheduler->getWaitingList();
    // step 2: 在terminatedList中查找Joinee, 看是否运行结束
    Thread *thread = FindThread(terminatedList, pid);

    if (thread == NULL) {
        // step 3: 
        // Joinee不位于terminatedList, 说明Joinee正处于Ready/Blocked状态
        // 将Joiner加入Waiting队列, 睡眠阻塞Joiner, 引发调度, 调度Joinee运行
        waitingList->Append((void *)this);
        currentThread->Sleep();
        thread = FindThread(terminatedList, pid);       // 此处thread还是NULL呢!!!!!!
    }
    // step 4: Joinee执行结束, 获取Joinee的退出码, 在terminatedList中回收Joinee, 继续运行Joiner
    currentThread->pcb->waitProcessExitCode = thread->pcb->exitCode;
    scheduler->removeFromTerminatedList(pid);
    interrupt->SetLevel(IntOn);         // 开中断
}

void
Thread::Terminated() {
    // step 1: 检查是否有错误
    ASSERT(this == currentThread);
    ASSERT(interrupt->GetLevel() == IntOff);
    DEBUG('t', "Terminated: Now in thread \"%s\"\n", currentThread->getName());
    // step 2: 当前线程状态为TERMINATED, 并将其放入terminatedList
    this->status = TERMINATED;
    List *terminatedList = scheduler->getTerminatedList();
    terminatedList->Append((void *)this);
    // step 3: 从就绪队列寻找下一个线程执行
    // 此处的nextThread一定为currentThread->parentThread, 因为此时关中断, 不会有其他线程调度的影响
    // 换句话说Join过程是原子操作
    Thread *nextThread;
    while ((nextThread = scheduler->FindNextToRun()) == NULL)
        interrupt->Idle();

    scheduler->Run(nextThread);
    DEBUG('t', "Terminated complete.\n");       // 位于Run函数后面的指令都不会运行, 因为切换上下文了
}

int 
Thread::getPid() const {
    return pcb->space->getPid();
}
 
Thread *
Thread::FindThread(List *list, int pid) {
    ListElement *element = list->getFirst();
    Thread *thread = NULL;
    while (element != NULL) {
        thread = (Thread *) element->item;
        if (thread == NULL || thread->getPid() == pid)
            break;
        element = element->next;
    }
    return thread;
}

#endif
