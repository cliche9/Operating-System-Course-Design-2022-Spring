Exit:
    DEBUG('x', "Exit, initiated by user program.\n");
    printf("SC_Exit: system call\n");
    scheduler->Print();
    // 读取Exit的退出码
    int exitCode = machine->ReadRegister(4);    
    printf("SC_Exit: Exit Status = %d\n", exitCode);
    // 将退出码作为返回值保存在r2, 以备Join使用
    machine->WriteRegister(2, exitCode);
    DEBUG('x', "Write exitCode back to r2\n");
    // 设置thread的退出码
    currentThread->pcb->exitCode = exitCode;
    // 处理非Fork线程
    if (currentThread->pcb->parentPid < 100) {
        scheduler->emptyList(scheduler->getTerminatedList());
        DEBUG('x', "Non-Forked Thread, empty terminated list.\n");
    }
    // 释放该线程的地址空间和pid
    currentThread->Finish();
    printf("SC_Exit complete.\n");
    scheduler->Print();
    IncrementPC();
    break;

/**
 * 问题出在哪里？
 * fork，将joinee放在readylist
 * 把Joiner睡眠，放在waitingList
 * joinee结束，调用exit，删除其对应的信息，将其放入terminatedlist
 * joiner从waitinglist移除，放入readylist
 * joiner将joinee从terminatedlist移除