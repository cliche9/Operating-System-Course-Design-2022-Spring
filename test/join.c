#include "syscall.h"

int main() {
    SpaceId pid = Exec("exit.noff");
    Join(pid);
    Exit(0);
}