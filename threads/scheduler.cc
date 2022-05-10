// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "scheduler.h"
#include "system.h"

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads to empty.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    readyList = new List("Ready"); 
#ifdef USER_PROGRAM
    terminatedList = new List("Terminated");
    waitingList = new List("Waiting");
#endif
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList;
    DEBUG('t', "deleted readyList\n");
#ifdef USER_PROGRAM
    if (terminatedList) {
        delete terminatedList;
        terminatedList = NULL;
    }
    DEBUG('t', "deleted terminatedList\n");
    if (waitingList) {
        delete waitingList;
        waitingList = NULL;
    }
    DEBUG('t', "deleted waitingList\n");
#endif
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    DEBUG('t', "Putting thread %s on ready list.\n", thread->getName());

    thread->setStatus(READY);
    readyList->Append((void *)thread);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    return (Thread *)readyList->Remove();
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread)
{
    Thread *oldThread = currentThread;
    
#ifdef USER_PROGRAM			// ignore until running user programs 
    if (currentThread->pcb->space != NULL) {	// if this thread is a user program,
        if (DebugIsEnabled('s'))
            machine->DumpState();
        currentThread->pcb->SaveUserState(); // save the user's CPU registers
	    currentThread->pcb->space->SaveState();
        DEBUG('t', "Save user program state.\n");
    }
#endif
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    currentThread = nextThread;		    // switch to the next thread
    currentThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG('t', "Switching from thread \"%s\" to thread \"%s\".\n",
	  oldThread->getName(), nextThread->getName());
#ifdef USER_PROGRAM
    if (oldThread->pcb->space && nextThread->pcb->space)
        DEBUG('t', "Switching from thread (pid = %d) to thread (pid = %d).\n", 
            oldThread->getPid(), nextThread->getPid());
#endif
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    SWITCH(oldThread, nextThread);
    
    DEBUG('t', "Now in thread \"%s\"\n", currentThread->getName());

    // If the old thread gave up the processor because it was finishing,
    // we need to delete its carcass.  Note we cannot delete the thread
    // before now (for example, in Thread::Finish()), because up to this
    // point, we were still running on the old thread's stack!
    if (threadToBeDestroyed != NULL) {
        DEBUG('t', "Delete previous thread.\n");
        delete threadToBeDestroyed;
	    threadToBeDestroyed = NULL;
    }
    
#ifdef USER_PROGRAM
    if (currentThread->pcb->space != NULL) {		// if there is an address space
        currentThread->pcb->RestoreUserState();     // to restore, do it.
	    currentThread->pcb->space->RestoreState();
        if (DebugIsEnabled('s')) {
            currentThread->pcb->space->Print();
            machine->DumpState();
        }
        DEBUG('t', "Restore user program state.\n");
    }
    Print();
#endif
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    printf("=======================Scheduler Queue=========================\n");
    printf("Ready list contents: ");
    readyList->Mapcar((VoidFunctionPtr) ThreadPrint);
    printf("\nWaiting list contents: ");
    waitingList->Mapcar((VoidFunctionPtr) ThreadPrint);
    printf("\nTerminated list contents: ");
    terminatedList->Mapcar((VoidFunctionPtr) ThreadPrint);
    printf("\n=============================================================\n\n\n");
}


#ifdef USER_PROGRAM
void
Scheduler::removeFromTerminatedList(int pid) {
    ListElement *cur = terminatedList->getFirst();
    while(cur != NULL){
        Thread *thread = (Thread *)cur->item;
        if(thread->getPid() == pid){
            terminatedList->RemoveByItem(cur->item);
            delete thread;
            break;
        }
        cur = cur->next;
    }
}

void
Scheduler::emptyList(List *list) {
    while (list->Remove() != NULL);	 // delete all the list elements
}

List *
Scheduler::getTerminatedList() const {
    return this->terminatedList;
}

List *
Scheduler::getWaitingList() const {
    return this->waitingList;
}
#endif