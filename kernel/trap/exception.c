#include "../lib/printf.h"
#include "../lib/riscv.h"
#include "trap.h"
#include "../mm/page_table.h"
#include "../mm/pmm.h"
#include "memlayout.h"
#include "../proc/proc.h"

#define PGROUNDUP(addr) (((unsigned long)(addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))
#define PGROUNDDOWN(addr) (((unsigned long)(addr)) & ~(PAGE_SIZE - 1))

void page_fault_handler(unsigned long scause, unsigned long sepc,struct trap_context *ctx) {
    struct proc *p = current_proc;
    unsigned long stval = csrr(stval);

    // ============ 1. 堆的 lazy allocation ============
    // heap_end 是 brk() 设置的上限，[p->sz, heap_end) 区间是合法的但未分配物理页
    if (stval >= p->sz && stval < p->heap_end) {
        unsigned long fault_page = PGROUNDDOWN(stval);

        // 检查是否已经映射
        pte_t *pte = pagetable_walk(p->pagetable, fault_page, 0);
        if (pte && (*pte & PTE_V)) {
            printf("[page_fault] page already mapped: 0x%lx\n", fault_page);
            proc_exit(-1);
            return;
        }

        // 分配物理页并建立映射
        void *pa = pmm_alloc();
        if (!pa) {
            printf("[page_fault] out of memory (heap)\n");
            proc_exit(-1);
            return;
        }

        if (pagetable_map(p->pagetable, fault_page, (unsigned long)pa, PAGE_SIZE, PTE_R | PTE_W | PTE_U) < 0) {
            printf("[page_fault] pagetable_map failed\n");
            pmm_free(pa);
            proc_exit(-1);
            return;
        }
        return;
    }

    // ============ 2. 用户栈的 lazy allocation ============
    // 栈在用户地址空间的最高处，sz = stack_top，guard page 在下方
    unsigned long stack_guard = PGROUNDUP(p->sz) - 5 * PAGE_SIZE;
    if (stval >= stack_guard && stval < PGROUNDUP(p->sz)) {
        unsigned long fault_page = PGROUNDDOWN(stval);

        pte_t *pte = pagetable_walk(p->pagetable, fault_page, 0);
        if (pte && (*pte & PTE_V)) {
            proc_exit(-1);
            return;
        }

        void *pa = pmm_alloc();
        if (!pa) {
            printf("[page_fault] out of memory (stack)\n");
            proc_exit(-1);
            return;
        }

        if (pagetable_map(p->pagetable, fault_page, (unsigned long)pa, PAGE_SIZE, PTE_R | PTE_W | PTE_U) < 0) {
            pmm_free(pa);
            proc_exit(-1);
            return;
        }
        return;
    }

    // ============ 3. 其他地址 → 非法 ============
    printf("[page_fault] invalid address: 0x%lx\n", stval);
    printf("  p->sz=0x%lx, p->heap_end=0x%lx\n", p->sz, p->heap_end);
    proc_exit(-1);
}