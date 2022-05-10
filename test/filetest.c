#include "syscall.h"

int main() {
    OpenFileId fp;
    char buffer[64];
    int size;
    Create("FTest");
    fp = Open("FTest");
    Write("hello nachos!", 10, fp);
    size = Read(buffer, 6, fp);
    Close(fp);
    Exit(0);
}