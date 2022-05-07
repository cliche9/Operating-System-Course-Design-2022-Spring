// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -N -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you haven't implemented the file system yet, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "system.h"
#include "addrspace.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
	noffH->noffMagic = WordToHost(noffH->noffMagic);
	noffH->code.size = WordToHost(noffH->code.size);
	noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
	noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
	noffH->initData.size = WordToHost(noffH->initData.size);
	noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
	noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
	noffH->uninitData.size = WordToHost(noffH->uninitData.size);
	noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
	noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Load the program from a file "executable", and set everything
//	up so that we can start executing user instructions.
//
//	Assumes that the object code file is in NOFF format.
//
//	First, set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//
//	"executable" is the file containing the object code to load into memory
//----------------------------------------------------------------------

BitMap *AddrSpace::freePageMap = new BitMap(NumPhysPages);
BitMap *AddrSpace::freeUserProcessMap = new BitMap(MaxUserProcess);

AddrSpace::AddrSpace(OpenFile *executable) {
    // 分配线程号
    ASSERT(freeUserProcessMap->NumClear() >= 1);
    pid = freeUserProcessMap->Find() + 100;                 // 0~99保留给核心进程
    
    NoffHeader noffH;
    unsigned int i, size;

    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    // Nachos是大端机器, 防止该noff程序是小端机器实现, 内存排列方式相反, 对其进行一步转换;
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

    // 确定该程序地址空间大小, 包括用户栈
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    ASSERT(numPages <= NumPhysPages);		// check we're not trying
						// to run anything too big --
						// at least until we have
						// virtual memory

    DEBUG('a', "Initializing address space, num pages %d, size %d\n", 
					numPages, size);
    // step1：创建用户页表, 建立虚拟页-实际帧映射
    pageTable = new TranslationEntry[numPages];
    ASSERT(freePageMap->NumClear() >= numPages);            // 确定内存空闲帧足以分配给该程序
    for (i = 0; i < numPages; i++) {
        pageTable[i].virtualPage = i;
        pageTable[i].physicalPage = freePageMap->Find();    // 修改virt - phys的映射, 寻找空闲物理帧作为映射
        pageTable[i].valid = TRUE;
        pageTable[i].use = FALSE;
        pageTable[i].dirty = FALSE;
        pageTable[i].readOnly = FALSE;                      // 只读选项, 当代码段完全占据一整个物理帧时设为只读
    }

    // 多线程情况下不清除machine的主存内容
    // bzero(machine->mainMemory, size);

    // step2：将noff代码段、数据段复制到物理内存
    if (noffH.code.size > 0) {
        // 根据noff程序的虚拟页号转换得到其物理地址
        int physicalPageNumber = pageTable[noffH.code.virtualAddr / PageSize].physicalPage * PageSize;
        int offset = noffH.code.virtualAddr % PageSize;
        DEBUG('a', "Initializing code segment, at 0x%x, size %d\n", 
			physicalPageNumber + offset, noffH.code.size);
        executable->ReadAt(&(machine->mainMemory[physicalPageNumber + offset]),
			noffH.code.size, noffH.code.inFileAddr);
    }
    if (noffH.initData.size > 0) {
        int physicalPageNumber = pageTable[noffH.initData.virtualAddr / PageSize].physicalPage * PageSize;
        int offset = noffH.initData.virtualAddr % PageSize;
        DEBUG('a', "Initializing data segment, at 0x%x, size %d\n", 
			physicalPageNumber + offset, noffH.initData.size);
        executable->ReadAt(&(machine->mainMemory[physicalPageNumber + offset]),
			noffH.initData.size, noffH.initData.inFileAddr);
    }
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.  Nothing for now!
//----------------------------------------------------------------------

AddrSpace::~AddrSpace() {
    freeUserProcessMap->Clear(pid);
    for (int i = 0; i < numPages; i++)
        freePageMap->Clear(pageTable[i].physicalPage);
    delete [] pageTable;
}

//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start"
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG('a', "Initializing stack register to %d\n", numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, nothing!
//----------------------------------------------------------------------

void AddrSpace::SaveState() {
    // 保存系统页表信息, 以便于线程上下文切换
    pageTable = machine->pageTable;
    numPages = machine->pageTableSize;
}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() {
    // 该函数是多线程的关键函数, 其将用户页表映射为系统页表, 切换了进程上下文
    machine->pageTable = pageTable;
    machine->pageTableSize = numPages;
}


void AddrSpace::Print() {
    // 输出该用户线程页表信息
    printf("Page table dump: %d pages in total\n", this->numPages);
    printf("===================================================\n");
    printf("\tVirtPage,\tPhysPage\n");
    for (int i = 0; i < this->numPages; i++) {
        printf("\t%d,\t\t%d\n", pageTable[i].virtualPage, pageTable[i].physicalPage);
    }
    printf("===================================================\n");
}

int AddrSpace::getPid() const {
    return this->pid;
}