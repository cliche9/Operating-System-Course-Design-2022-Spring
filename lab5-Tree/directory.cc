// directory.cc 
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//	Fixing this is one of the parts to the assignment.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory()
{
    /**
    table = new DirectoryEntry[size];
    tableSize = size;
    for (int i = 0; i < tableSize; i++)
	table[i].inUse = FALSE;
    */
    root = new DirectoryEntry();
    root->sector = 4;
    tableSize = 1;
    root->inUse = TRUE;
    root->isDir = TRUE;
    root->child = NULL;
    root->slibing = NULL;
    strncpy(root->name, "/", FileNameMaxLen);
    parent = root;
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{   
    delete root;
} 

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void
Directory::FetchFrom(OpenFile *file)
{   
    char size[4];
    (void) file->ReadAt(size, sizeof(int), 0);
    size[4] = '\0';
    tableSize = atoi(size);
    DirectoryEntry tree[tableSize];
    (void) file->ReadAt((char *)tree, tableSize * sizeof(DirectoryEntry), sizeof(int));
    delete root;
    root = NULL;
    LoadNode(root, -1, tree);
    parent = root;
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void
Directory::WriteBack(OpenFile *file)
{
    char size[4];
    for (int i = 0; i < 3; i++)
        size[i] = char(tableSize >> (8 * i));
    size[4] = '\0';
    
    (void) file->WriteAt(size, sizeof(int), 0);
    DirectoryEntry tree[tableSize];
    SaveNode(root, tree);
    (void) file->WriteAt((char *)tree, tableSize * sizeof(DirectoryEntry), sizeof(int));
}

//----------------------------------------------------------------------
// Directory::Parse
// 	将输入路径按照"/"分割开
//	path: 输入路径
//----------------------------------------------------------------------
vector<string>
Directory::Parse(const string &path) const {
    vector<string> res;
    if (path[0] == '/')
        res.push_back("/");
    
    int left = 0;
    for (int right = 0; right < path.length(); right++) {
        if (path[right] == '/') {
            res.push_back(path.substr(left, right - left));
            left = right + 1;
        }
    }
    res.push_back(path.substr(left));

    return res;
}
//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return pointer to DirectoryEntry
//	Return NULL if the name isn't in the directory.
//
//	"name" -- the file name to look up: 绝对路径
//----------------------------------------------------------------------

DirectoryEntry *
Directory::FindNode(char *name)
{
    vector<string> path = Parse(name);
    int n = path.size();
    if (path.size() == 1)
        return NULL;
    DirectoryEntry *cur = root;
    // 遍历绝对路径的每个目录是否存在
    for (int i = 0; i < path.size() - 1; i++) {
        while (cur != NULL) {
            if (cur->isDir && cur->name == path[i])
                break;
            cur = cur->slibing;
        }
        parent = cur;
        cur = cur->child;
    }
    // 最后一个目录下文件/目录是否存在
    while (cur != NULL) {
        if (cur->name == path.back())
            break;
    }

    return cur;
}

//----------------------------------------------------------------------
// Directory::LoadNode
// 	将目录树从文件中读入内存, 此函数为递归处理函数, 递归读入每个节点
//
//	root：当前子树根节点, childSize: root的孩子个数, tree: 存放目录树数据的临时数组
//----------------------------------------------------------------------
void
Directory::LoadNode(DirectoryEntry *root, int childSize, DirectoryEntry *tree) {
    if (childSize == -1) {
        // 目录树的根节点
        root = new DirectoryEntry(*tree);       // 取数组头位置内容
        ++tree;
        LoadNode(root, root->childSize, tree);
    } else {
        // 先处理孩子节点
        if (childSize) {
            root->child = new DirectoryEntry(*tree);
            ++tree;
            LoadNode(root->child, root->child->childSize, tree);
        }
        DirectoryEntry *cur = root->child;
        // 然后处理兄弟节点
        for (int i = 1; i < root->childSize; i++) {
            cur->slibing = new DirectoryEntry(*tree);
            ++tree;
            LoadNode(cur->slibing, cur->slibing->childSize, tree);
            cur = cur->slibing;
        }
    }
}

//----------------------------------------------------------------------
// Directory::SaveNode
// 	将目录树从内存中存入文件, 此函数为递归处理函数, 递归保存每个节点
//
//	root：当前子树根节点, tree: 存放目录树数据的临时数组
//----------------------------------------------------------------------
void
Directory::SaveNode(DirectoryEntry *root, DirectoryEntry *tree) {
    if (root == NULL)
        return;
    // 保存一个根节点
    *tree = *root;
    ++tree;
    DirectoryEntry *cur = root->child;
    while (root != NULL) {
        SaveNode(root, tree);
        root = root->slibing;
    }
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't 
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int
Directory::Find(char *name)
{
    DirectoryEntry *node = FindNode(name);
    // 查找文件名为name的节点, 返回对应文件头扇区号
    if (node)
	    return node->sector;
    return -1;
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool
Directory::Add(char *name, int newSector)
{   
    DirectoryEntry *node = FindNode(name);
    if (node) {
        // 存在且正在使用
        if (node->inUse)
            return FALSE;
        else {
        // 存在但已经空闲
            node->inUse = TRUE;
            strncpy(node->name, name, FileNameMaxLen);
            node->sector = newSector;
            return TRUE;
        }
    }
    // 不存在, 需要新建
    if (parent->child == NULL) {
        // 无子结点
        parent->child = new DirectoryEntry();
        parent->child->inUse = TRUE;
        strncpy(parent->child->name, name, FileNameMaxLen);
        parent->child->sector = newSector;
    } else {
        // 有子节点
        DirectoryEntry *cur = parent->child;
        while (cur->slibing != NULL)
            cur = cur->slibing;
        cur->slibing = new DirectoryEntry();
        cur->inUse = TRUE;
        strncpy(cur->name, name, FileNameMaxLen);
        cur->sector = newSector;
    }
    ++tableSize;
    return TRUE;
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory. 
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool
Directory::Remove(char *name)
{ 
    DirectoryEntry *node = FindNode(name);

    if (node == NULL)
	    return FALSE; 		// name not in directory
    node->inUse = FALSE;
    return TRUE;	
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory. 
//----------------------------------------------------------------------

void
Directory::List()
{
    SubList(root);
}

void
Directory::SubList(DirectoryEntry *root)
{
    DirectoryEntry *cur = root;
    // 前序遍历输出
    while (cur != NULL) {
        printf("%s\n", cur->name);
        SubList(cur->child);
        cur = cur->slibing;
    }
}

//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void
Directory::Print()
{ 
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    SubPrint(root, hdr);
    printf("\n");
    delete hdr;
}

void
Directory::SubPrint(DirectoryEntry *root, FileHeader *hdr)
{ 
    DirectoryEntry *node = root;
    while (node != NULL) {
        printf("Name: %s, Sector: %d\n", node->name, node->sector);
        hdr->FetchFrom(node->sector);
        hdr->Print();
        SubPrint(node->child, hdr);
        node = node->slibing;
    }
}
