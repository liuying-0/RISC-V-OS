#include "vmm.h"
#include "pmm.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "memlayout.h"

pagetable_t kernel_pagetable = NULL;

void vmm_init(void){
    kernel_pagetable = pagetable_create();
    pagetable_t kernel_pt = kernel_pagetable;

    extern char _text_start[], _text_end[];
    pagetable_map(kernel_pt, (unsigned long)_text_start, (unsigned long)_text_start, _text_end - _text_start, PTE_R | PTE_X);

    extern char _rodata_start[], _rodata_end[];
    pagetable_map(kernel_pt, (unsigned long)_rodata_start, (unsigned long)_rodata_start, _rodata_end - _rodata_start, PTE_R);

    extern char _data_start[], _kernel_end[];
    pagetable_map(kernel_pt, (unsigned long)_data_start, (unsigned long)_data_start, _kernel_end - _data_start, PTE_R | PTE_W);

    pagetable_map(kernel_pt, (unsigned long)_kernel_end, (unsigned long)_kernel_end, PHYS_MEM_END - (unsigned long)_kernel_end, PTE_R | PTE_W);

    pagetable_map(kernel_pt, UART0_BASE, UART0_BASE, PAGE_SIZE, PTE_R | PTE_W);

    pagetable_map(kernel_pt, PLIC_BASE, PLIC_BASE, UART0_BASE - PLIC_BASE, PTE_R | PTE_W);
    
    enable_paging(kernel_pt);
}

void enable_paging(pagetable_t kernel_pt){
    unsigned long satp_val = (8UL << 60) | (unsigned long) kernel_pt >> 12;

    asm volatile("csrw satp, %0" : : "r"(satp_val));

    asm volatile("sfence.vma zero, zero");
}