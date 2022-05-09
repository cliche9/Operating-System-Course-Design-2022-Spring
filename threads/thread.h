// thread.h 
//	Data structures for managing threads.  A thread represents
//	sequential execution of code within a program.
//	So the state of a thread includes the program counter,
//	the processor registers, and the execution stack.
//	
// 	Note that because we allocate a fixed size stack for each
//	thread, it is possible to overflow the stack -- for instance,
//	by recursing to too deep a level.  The most common reason
//	for this occuring is allocating large data structures
//	on the stack.  For instance, this will cause problems:
//
//		void foo() { int buf[1000]; ...}
//
//	Instead, you should allocate all data structures dynamically:
//
//		void foo() { int *buf = new int[1000]; ...}
//
//
// 	Bad things happen if you overflow the stack, and in the worst 
//	case, the problem may not be caught explicitly.  Instead,
//	the only symptom may be bizarre segmentation faults.  (Of course,
//	other problems can cause seg faults, so that isn't a sure sign
//	that your thread stacks are too small.)
//	
//	One thing to try if you find yourself with seg faults is to
//	increase the size of thread stack -- ThreadStackSize.
//
//  	In this interface, forking a thread takes two steps.
//	We must first allocate a data structure for it: "t = new Thread".
//	Only then can we do the fork: "t->fork(f, arg)".
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef THREAD_H
#define THREAD_H

#include "copyright.h"
#include "utility.h"

#ifdef USER_PROGRAM
#include "machine.h"
#include "addrspace.h"
#include "list.h"
#endif


// CPU register state to be saved on context switch.  
// The SPARC and MIPS only need 10 registers, but the Snake needs 18.
// For simplicity, this is just the max over all architectures.
#define MachineStateSize 18 


// Size of the thread's private execution stack.
// WATCH OUT IF THIS ISN'T BIG ENOUGH!!!!!
#define StackSize	(sizeof(_int) * 1024)	// in words


// Thread state, 增加TERMINATED状态, 用于多线程机制
enum ThreadStatus { JUST_CREATED, RUNNING, READY, BLOCKED, TERMINATED };

// external function, dummy routine whose sole job is to call Thread::Print
extern void ThreadPrint(_int arg);	 

// The following class defines a "thread control block" -- which
// represents a single thread of execution.
//
//  Every thread has:
//     an execution stack for activation records ("stackTop" and "stack")
//     space to save CPU registers while not running ("machineState")
//     a "status" (running/ready/blocked)
//    
//  Some threads also belong to a user address space; threads
//  that only run in the kernel have a NULL address space.

#ifdef USER_PROGRAM
#define MaxFileId 10;

struct PCB {
    int userRegisters[NumTotalRegs];	// 用户态CPU寄存器状态
    int parentPid;                    // 父线程的pid
    int waitProcessExitCode;          // Join(pid)线程pid的退出码
    int waitProcessPid;               // 当前thread等待线程的pid
    int exitCode;                     // 当前thread的exitCode
    AddrSpace *space;			            // 用户线程的地址空间
    OpenFile *files[MaxFileId];       // 打开文件
    
    PCB();                            // 构造函数
    ~PCB();                           // 析构函数
    void SaveUserState();		          // 保存用户寄存器内容
    void RestoreUserState();		      // 恢复用户寄存器
    int getFileDescriptor(OpenFile *openfile);    // 获取openfile的fd
    OpenFile *getOpenFile(int fd);    // 获取fd的OpenFile *
    void releaseFileDescriptor(int fd);           // 释放fd
};
#endif

class Thread {
  private:
    // NOTE: DO NOT CHANGE the order of these first two members.
    // THEY MUST be in this position for SWITCH to work.
    int* stackTop;			        // 当前栈指针
    _int machineState[MachineStateSize];  // 所有CPU寄存器状态

  public:
    Thread(char* debugName = "default");		// 初始化线程
    ~Thread(); 				          // 删除线程, 该线程必须是terminated状态

    // basic thread operations
    void Fork(VoidFunctionPtr func, _int arg); 	// 线程从函数(*func)(arg)开始运行
    void Yield();  				      // 其他线程运行, 让出CPU运行权
    void Sleep();  				      // 线程睡眠, 让出CPU运行权
    void Finish();  		        // 线程运行结束后调用Finish
    
    void CheckOverflow();       // 检查线程栈是否溢出
    void setStatus(ThreadStatus st) { status = st; }
    char* getName() { return (name); }
    void Print() { printf("%s, ", name); }

  private:
    // some of the private data for this class is listed above
    int* stack; 	 		          // 栈底指针, 主线程栈底指针为NULL 
    ThreadStatus status;		    // 线程状态：ready, running, blocked or terminated
    char* name;                 // 线程debug名称

    void StackAllocate(VoidFunctionPtr func, _int arg);   // Fork内部调用, 分配线程的栈空间

#ifdef USER_PROGRAM
// A thread running a user program actually has *two* sets of CPU registers -- 
// one for its state while executing user code, one for its state 
// while executing kernel code.

  public:
    void Join(int pid);               // 系统调用Join: 阻塞当前线程, 执行pid线程, pid执行完毕后回收其资源, 重新运行当前线程
    void Terminated();                // 线程终止收尾操作
    
    int getPid() const;               // 获取Pid
    PCB *pcb;                         // 用户进程的相关变量
  private:
    Thread *FindThread(List *list, int pid);   // 从list中寻找线程号为pid的线程
    friend class Scheduler;
#endif
};

// Magical machine-dependent routines, defined in switch.s

extern "C" {
// First frame on thread execution stack; 
// enable interrupts
// call "func"
// (when func returns, if ever) call ThreadFinish()
void ThreadRoot();

// Stop running oldThread and start running newThread
void SWITCH(Thread *oldThread, Thread *newThread);
}

#endif // THREAD_H
