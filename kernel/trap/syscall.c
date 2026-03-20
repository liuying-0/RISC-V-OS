#include "../lib/printf.h"
#include "trap.h"

void syscall_handler(struct trap_context *ctx) {
    panic("syscall not implemented");
}