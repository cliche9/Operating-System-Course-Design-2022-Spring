// addrspace.h 
//	Data structures to keep track of executing user programs 
//	(address spaces).
//
//	For now, we don't keep any information about address spaces.
//	The user level CPU state is saved and restored in the thread
//	executing the user program (see thread.h).
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#ifndef ADDRSPACE_H
#define ADDRSPACE_H

#include "copyright.h"
#include "filesys.h"
#include "bitmap.h"

#define UserStackSize		1024 	// increase this as necessary!
#define MaxUserProcess  128

class AddrSpace {
  public:
    AddrSpace(OpenFile *executable);	// 初始化executable中程序的地址空间
    ~AddrSpace();			                // 析构函数

    void InitRegisters();		          // 初始化用户线程寄存器
    void SaveState();			            // 保存地址空间状态
    void RestoreState();		          // 用户页表映射为系统页表
    void Print();                     // 输出页表相关信息：虚实页的映射等关系
    int getPid() const;               // 获取进程号

  private:
    int pid;                          // 线程号
    TranslationEntry *pageTable;	    // 用户页表
    unsigned int numPages;		        // 页表表项个数
    static BitMap *freePageMap;       // 管理物理内存的空闲帧
    static BitMap *freeUserProcessMap;// 管理空闲用户线程
};

#endif // ADDRSPACE_H
