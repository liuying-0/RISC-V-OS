#include "../lib/printf.h"
#include "../lib/string.h"
#include "../lib/riscv.h"
#include "../driver/uart.h"
#include "../proc/proc.h"
#include "../mm/vmm.h"
#include "trap.h"
#include "syscall.h"
#include "memlayout.h"

static int sys_write(struct trap_context *tf) {
    int fd = (int)tf->a0;
    unsigned long buf_va = tf->a1;
    int len = (int)tf->a2;

    if (fd != 1 && fd != 2) return -1;
    if (len <= 0) return 0;

    char kbuf[128];
    int written = 0;
    while (written < len) {
        int n = len - written;
        if (n > (int)sizeof(kbuf)) n = (int)sizeof(kbuf);

        if (copyin(current_proc->pagetable, kbuf, buf_va + written, n) < 0)
            return -1;

        for (int i = 0; i < n; i++)
            uart_putc(kbuf[i]);

        written += n;
    }
    return written;
}

static void sys_exit(struct trap_context *tf) {
    int status = (int)tf->a0;
    printf("[kernel] process %d (%s) exited with status %d\n",
           current_proc->pid, current_proc->name, status);
    proc_exit(status);
}

static int sys_sbrk(struct trap_context *tf) {
    unsigned long new_heap_end = (unsigned long)tf->a0;

    if (current_proc->pid == 0) return -1;

    if (new_heap_end == 0)
        return (int)current_proc->heap_end;

    if (new_heap_end < current_proc->heap_end)
        return -1;

    if (new_heap_end >= USER_TOP)
        return -1;

    current_proc->heap_end = new_heap_end;
    return 0;
}

void syscall_handler(struct trap_context *ctx) {
    int sysnum = (int)ctx->a7;
    long ret = 0;

    switch (sysnum) {
        case SYS_exit:
            sys_exit(ctx);
            break;
        case SYS_write:
            ret = sys_write(ctx);
            break;
        case SYS_brk:
            ret = sys_sbrk(ctx);
            break;
        default:
            printf("[kernel] unknown syscall %d from pid %d\n",
                   sysnum, current_proc->pid);
            ret = -1;
            break;
    }

    ctx->a0 = ret;
}