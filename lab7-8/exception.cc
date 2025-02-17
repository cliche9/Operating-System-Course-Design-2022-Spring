// exception.cc 
//	Entry point into the Nachos kernel from user programs.
//	There are two kinds of things that can cause control to
//	transfer back to here from user code:
//
//	syscall -- The user code explicitly requests to call a procedure
//	in the Nachos kernel.  Right now, the only function we support is
//	"Halt".
//
//	exceptions -- The user code does something that the CPU can't handle.
//	For instance, accessing memory that doesn't exist, arithmetic errors,
//	etc.  
//
//	Interrupts (which can also cause control to transfer from user
//	code into the Nachos kernel) are handled elsewhere.
//
// For now, this only handles the Halt() system call.
// Everything else core dumps.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "syscall.h"

void StartProcess(int pid);
void IncrementPC();
void ReadMem(int addr, char *fileName, int size);
//----------------------------------------------------------------------
// ExceptionHandler
// 	Entry point into the Nachos kernel.  Called when a user program
//	is executing, and either does a syscall, or generates an addressing
//	or arithmetic exception.
//
// 	For system calls, the following is the calling convention:
//
// 	system call code -- r2
//		arg1 -- r4
//		arg2 -- r5
//		arg3 -- r6
//		arg4 -- r7
//
//	The result of the system call, if any, must be put back into r2. 
//
// And don't forget to increment the pc before returning. (Or else you'll
// loop making the same system call forever!
//
//	"which" is the kind of exception.  The list of possible exceptions 
//	are in machine.h.
//----------------------------------------------------------------------

void
ExceptionHandler(ExceptionType which)
{
    int type = machine->ReadRegister(2);

    if (which == SyscallException) {
        // 处理system call exception
        switch (type) {
            case SC_Halt: {
                DEBUG('x', "Shutdown, initiated by user program.\n");
   	            interrupt->Halt();
                break;
            }
            case SC_Exit: {
                DEBUG('x', "Exit, initiated by user program.\n");
                printf("SC_Exit: system call\n");
                // scheduler->Print();
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
                // scheduler->Print();
                IncrementPC();
                break;
            }
            case SC_Exec: {
                DEBUG('x', "Exec, initiated by user program.\n");
                printf("SC_Exec: system call\n");
                // scheduler->Print();
                // 获得exec程序中Exec系统调用函数的参数
   	            int addr = machine->ReadRegister(4);        
                // 由于此处参数是字符串, r4寄存器存储该字符串在内存中的地址, 因此需要访存读出
                // char fileName[FileNameMaxLen + 1];   Nachos中的文件长度应该限制在FileNameMaxLen, 此处为了适应unix文件系统, 直接使用50作为长度
                char fileName[64];
                ReadMem(addr, fileName, 64);
                // 从内存读取待执行的程序
                if (strcmp(fileName, "ls") == 0) {
                    DEBUG('x', "thread:%s\tFile(s) on Nachos DISK:\n", currentThread->getName());
                    fileSystem->List();
                    machine->WriteRegister(2, 127);
                    IncrementPC();
                    break;
                }
                OpenFile *executable = fileSystem->Open(fileName);
                if (executable == NULL) {
                    printf("Unable to open file %s\n", fileName);
                    ASSERT(FALSE);
                }
                // 建立程序的地址空间 ----- 即建立用户线程
                AddrSpace *space = new AddrSpace(executable);
                delete executable;
                // 新建Thread类即线程管理类, Fork运行子线程
                printf("SC_Exec: Forked thread name is %s.\n", fileName);
                Thread *thread = new Thread(fileName);
                // 将用户线程映射为核心线程
                thread->pcb->space = space;
                printf("SC_Exec: parentPid = %d\n", currentThread->getPid());
                // 设置新建线程的parentPid = 当前线程的pid
                thread->pcb->parentPid = currentThread->getPid();
                // 输出该进程的页表信息, for debugging
                space->Print();
                // 此处Fork的参数要求为int, 如果要传char *, 要么重载Fork, 要么重载StartProcess, 我们选择简单的重载StartProcess
                // 还有一种解决思路, 将char *转换成int传递给Fork, 两者均为4字节;
                thread->Fork(StartProcess, space->getPid());
                // currentThread->Yield();      // 去掉这个就会报段错误, 为啥呢？
                // Exec有返回值, 返回线程号, 返回值存在r2寄存器中
                machine->WriteRegister(2, space->getPid());
                // 系统调用返回后直接return, 因此需要在ExceptionHandler中增加PC
                // scheduler->Print();
                IncrementPC();
                break;
            }
            case SC_Join: {
                DEBUG('x', "Join, initiated by user program.\n");
                printf("SC_Join: system call\n");
                // scheduler->Print();
                int pid = machine->ReadRegister(4);     // 读取pid
                currentThread->Join(pid);               // 执行join
                // 返回pid线程的返回码waitProcessExitCode
                printf("SC_Join: Exit Status = %d\n", currentThread->pcb->waitProcessExitCode);
                // scheduler->Print();
                machine->WriteRegister(2, currentThread->pcb->waitProcessExitCode);
                IncrementPC();
                break;
            }
            case SC_Yield: {
                DEBUG('x', "Yield, initiated by user program.\n");
                printf("SC_Yield: system call\n");
                currentThread->Yield();
                IncrementPC();
                break;
            }
            case SC_Create: {
                DEBUG('x', "Create, initiated by user program.\n");
                printf("SC_Create: system call\n");
                // 获取文件基址
                int addr = machine->ReadRegister(4);
                printf("Addr: %d\n", addr);
                // 读取文件
                char fileName[64];
                ReadMem(addr, fileName, 64);
#ifdef FILESYS_STUB
                // 打开文件, 不存在则新建, 存在则清空覆盖
                int fd = OpenForWrite(fileName);
                if (fd == -1)
                    printf("Create file %s failed.\n", fileName);
                else
                    printf("Create file %s succeed, the fd is %d.\n", fileName, fd);
                Close(fd);
#else
                if (!fileSystem->Create(fileName, 0))
                    printf("Create file %s failed.\n", fileName);
                else
                    printf("Create file %s succeed.\n", fileName);
#endif
                // machine->WriteRegister(2, fileDescriptor);
                IncrementPC();
                break;
            }
            case SC_Open: {
                DEBUG('x', "Open, initiated by user program.\n");
                printf("SC_Open: system call\n");
                int addr = machine->ReadRegister(4);
                printf("Addr: %d\n", addr);
                char fileName[64];
                ReadMem(addr, fileName, 64);
#ifdef FILESYS_STUB
                int fd = OpenForReadWrite(fileName, true);
                if (fd == -1)
                    printf("Open file %s failed.\n", fileName);
                else
                    printf("Opne file %s, the fd is %d.\n", fileName, fd);
#else
                OpenFile *openfile = fileSystem->Open(fileName);
                ASSERT(openfile != NULL);
                int fd = currentThread->pcb->getFileDescriptor(openfile);
                printf("File: %s open secceed! the file id is %d\n", fileName, fd);
#endif
                machine->WriteRegister(2, fd);
                IncrementPC();
                break;
            }
            case SC_Write: {
                DEBUG('x', "Write, initiated by user program.\n");
                printf("SC_Write: system call\n");
                int addr = machine->ReadRegister(4);
                int size = machine->ReadRegister(5);
                int fd = machine->ReadRegister(6);
                printf("Addr: %d, length: %d, fd: %d\n", addr, size, fd);
                // 从内存读取buffer的内容
                char buffer[128];
                ReadMem(addr, buffer, size);
#ifdef FILESYS_STUB
                // 打开fd对应文件
                OpenFile *openfile = new OpenFile(fd);
                ASSERT(openfile != NULL);
                int writePosition = (fd == 1) ? 0 : openfile->Length();
                int writtenBytes = openfile->WriteAt(buffer, size, writePosition);
                if (writtenBytes == 0)
                    printf("Write to file failed.\n");
                else
                    printf("\"%s\" has written to file %d.\n", buffer, fd);
#else
                OpenFile *openfile = currentThread->pcb->getOpenFile(fd);
                ASSERT(openfile != NULL)
                if (fd == 1 || fd == 2)
                    openfile->WriteStdout(buffer, size); 
                else {
                    int writePosition = openfile->Length();
                    openfile->Seek(writePosition);
                    int writtenBytes = openfile->Write(buffer, size);
                    if(writtenBytes == 0)
                        printf("Write file failed!\n"); 
                    else
                        printf("\"%s\" has written in file %d succeed!\n", buffer, fd);
                }
#endif
                machine->WriteRegister(2, size);
                IncrementPC();
                break;
            }
            case SC_Read: {
                DEBUG('x', "Read, initiated by user program.\n");
                printf("SC_Read: system call\n");
                int addr = machine->ReadRegister(4);
                int size = machine->ReadRegister(5);
                int fd = machine->ReadRegister(6);
                printf("Addr: %d, length: %d, fd: %d\n", addr, size, fd);
#ifdef FILESYS_STUB
                // 打开fd对应文件
                OpenFile *openfile = new OpenFile(fd);
                ASSERT(openfile != NULL);
                // 从内存读取buffer的内容
                char buffer[size];
                int readBytes = openfile->Read(buffer, size);
                for (int i = 0; i < size; i++)
                    if (!machine->WriteMem(addr, 1, buffer[i]))
                        printf("Writing Memory ErrorOccurred.\n");
                buffer[readBytes] = '\0';
                printf("Read succeed, contents: %s, length = %d.\n", buffer, readBytes);
#else
                OpenFile *openfile = currentThread->pcb->getOpenFile(fd);
                ASSERT(openfile != NULL);
                char buffer[size];
                int readBytes = 0;
                if (fd == 0) //stdin
                    readBytes = openfile->ReadStdin(buffer, size);
                else
                    readBytes = openfile->ReadFromStart(buffer, size);
                for (int i = 0; i < readBytes; i++)
                    machine->WriteMem(addr, 1, buffer[i]);
                buffer[readBytes]='\0';
                
                for(int i = 0; i < readBytes; i++)
                    if (buffer[i] >=0 && buffer[i] <= 9)
                        buffer[i] += 0x30;
                char *buf = buffer;
                if (readBytes > 0)
                    printf("Read file (%d) succeed! the content is \"%s\", the length is %d\n", fd, buf, readBytes);
                else
                    printf("Read file failed!\n");
#endif
                machine->WriteRegister(2, readBytes);
                IncrementPC();
                break;
            }
            case SC_Close: {
                DEBUG('x', "Close, initiated by user program.\n");
                printf("SC_Close: system call\n");
                int fd = machine->ReadRegister(4);
                printf("fd: %d\n", fd);
#ifdef FILESYS_STUB
                Close(fd);
                printf("Close succeed, fd = %d.\n", fd);
#else
                OpenFile* openfile = currentThread->pcb->getOpenFile(fd);
                ASSERT(openfile != NULL);
                openfile->WriteBack();  // write file header back to DISK
                delete openfile;        // close file 
                currentThread->pcb->releaseFileDescriptor(fd);
                printf("File %d closed succeed.\n", fd);
#endif
                IncrementPC();
                break;
            }
            default: {
                printf("Unexpected system call %d %d\n", which, type);
	            ASSERT(FALSE);
            }
        }
    } else {
        // 处理其他 exception: addressing exception 或者 arithmetic exception等
        printf("Unexpected user mode exception %d %d\n", which, type);
        ASSERT(FALSE);
    }
}

/**
 * 重载的StartProcess函数, 以适应Fork传参为整数
 */ 
void
StartProcess(int pid) {
    // 此时地址空间已经建立, 只需要初始化寄存器, 调度执行程序即可
    currentThread->pcb->space->InitRegisters();  // 初始化寄存器
    currentThread->pcb->space->RestoreState();   // 恢复页表信息
    machine->Run();     // 运行用户程序
    ASSERT(FALSE);
}

/**
 * IncrementPC, 在处理完系统调用后, 程序直接return, 缺少了更新PC的过程
 * 因此需要将PC增加, 将其封装成函数IncrementPC
 * 具体实现可以参考../machine/mipssim.cc中OneInstruction()前后更新PC的逻辑
 */ 
void 
IncrementPC() {
    printf("Increment PC --- initiated by user program.\n");
    // machine中的registers是私有的, 不可直接访问, 因此通过WriteRegister来间接更新PC值
    machine->WriteRegister(PrevPCReg, machine->ReadRegister(PCReg));    // 更新PrevPC = PC
    machine->WriteRegister(PCReg, machine->ReadRegister(NextPCReg));    // 更新PC = NextPC
    machine->WriteRegister(NextPCReg, machine->ReadRegister(NextPCReg) + 4);    // 更新NextPC = NextPC + 4
}

void
ReadMem(int addr, char *buffer, int size) {
    for (int i = 0; i < size; i++) {
        machine->ReadMem(addr + i, 1, (int *)&buffer[i]);
        if (buffer[i] == '\0')
            break;
    }
}