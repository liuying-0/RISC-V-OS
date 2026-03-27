#include "vmm.h"
#include "pmm.h"
#include "../lib/string.h"
#include "../lib/types.h"
#include "memlayout.h"

pagetable_t kernel_pagetable = NULL;

void vmm_init(void){
    kernel_pagetable = pagetable_create();
    if (!kernel_pagetable) { while(1); }

    extern char _text_start[], _text_end[];
    extern char _rodata_start[], _rodata_end[];
    extern char _data_start[], _kernel_end[];

    pagetable_map(kernel_pagetable,
        KERN_BASE + (unsigned long)_text_start,
        (unsigned long)_text_start,
        _text_end - _text_start,
        PTE_R | PTE_X);

    pagetable_map(kernel_pagetable,
        KERN_BASE + (unsigned long)_rodata_start,
        (unsigned long)_rodata_start,
        _rodata_end - _rodata_start,
        PTE_R);

    pagetable_map(kernel_pagetable,
        KERN_BASE + (unsigned long)_data_start,
        (unsigned long)_data_start,
        _kernel_end - _data_start,
        PTE_R | PTE_W);

    pagetable_map(kernel_pagetable,
        KERN_BASE + (unsigned long)_kernel_end,
        (unsigned long)_kernel_end,
        PHYS_MEM_END - (unsigned long)_kernel_end,
        PTE_R | PTE_W);

    pagetable_map(kernel_pagetable,
        PHYS_MEM_START,
        PHYS_MEM_START,
        PHYS_MEM_END - PHYS_MEM_START,
        PTE_R | PTE_W);

    pagetable_map(kernel_pagetable, UART0_BASE, UART0_BASE, PAGE_SIZE, PTE_R | PTE_W);

    pagetable_map(kernel_pagetable, CLINT_BASE, CLINT_BASE, 0x10000, PTE_R | PTE_W);

    pagetable_map(kernel_pagetable, PLIC_BASE, PLIC_BASE, PLIC_SIZE, PTE_R | PTE_W);

    extern char trampoline_start[];
    pagetable_map(kernel_pagetable, TRAMPOLINE,
        (unsigned long)trampoline_start, PAGE_SIZE, PTE_R | PTE_X);

    unsigned long satp_val = (8UL << 60) | ((unsigned long)kernel_pagetable >> 12);
    asm volatile("csrw satp, %0" :: "r"(satp_val));
}

pagetable_t user_pagetable_create(void){
    pagetable_t pt = pagetable_create();
    if (!pt) return NULL;

    extern char trampoline_start[];
    extern char trampoline_end[];
    pagetable_map(pt, TRAMPOLINE,
        (unsigned long)trampoline_start,
        trampoline_end - trampoline_start,
        PTE_R | PTE_X);

    return pt;
}

int copyin(pagetable_t pt, void *dst, unsigned long srcva, unsigned long len){
    while (len > 0) {
        pte_t *pte = pagetable_walk(pt, srcva, 0);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return -1;
        unsigned long pa = PTE2PA(*pte) | (srcva & (PAGE_SIZE - 1));
        unsigned long n = PAGE_SIZE - (srcva & (PAGE_SIZE - 1));
        if (n > len) n = len;
        memcpy(dst, (void *)pa, n);
        dst = (char *)dst + n;
        srcva += n;
        len -= n;
    }
    return 0;
}

int copyout(pagetable_t pt, unsigned long dstva, const void *src, unsigned long len){
    while (len > 0) {
        pte_t *pte = pagetable_walk(pt, dstva, 0);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) return -1;
        unsigned long pa = PTE2PA(*pte) | (dstva & (PAGE_SIZE - 1));
        unsigned long n = PAGE_SIZE - (dstva & (PAGE_SIZE - 1));
        if (n > len) n = len;
        memcpy((void *)pa, src, n);
        src = (const char *)src + n;
        dstva += n;
        len -= n;
    }
    return 0;
}
