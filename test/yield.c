#include "syscall.h"

int main() {
    Exec("exit.noff");
    Yield();
    Exit(0);
}