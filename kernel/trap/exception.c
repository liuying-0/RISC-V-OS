#include "../lib/printf.h"
#include "../lib/riscv.h"

void page_fault_handler(unsigned long scause, unsigned long sepc, void *ctx) {
    unsigned long stval = csrr(stval);

    printf("Page fault! cause=%d, sepc=%p, addr=%p\n", scause, sepc, stval);

    panic("page fault");
}