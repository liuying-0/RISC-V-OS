#include "page_table.h"
#include "pmm.h"
#include "../lib/printf.h"
#include "../lib/string.h"
#include "memlayout.h"

pte_t *pagetable_walk(pagetable_t pt, unsigned long va, int alloc){
    for(int level = 2; level > 0; level--){
        int idx = VA_VPN(va, level);
        pte_t *pte = &pt[idx];
        if (*pte & PTE_V) {
            pt = (pagetable_t)PTE2PA(*pte);
        } else{
            if(!alloc) return NULL;

            pagetable_t new_pt = (pagetable_t)pmm_alloc();
            if(!new_pt) return NULL;

            *pte = PA2PTE(new_pt) | PTE_V;
            pt = new_pt;
        }
    }

    return &pt[VA_VPN(va,0)];
}

int pagetable_map(pagetable_t pt, unsigned long va, unsigned long pa, unsigned long size, int flags){
    unsigned long end = va + size;

    va = va & ~(PAGE_SIZE - 1);
    pa = pa & ~(PAGE_SIZE - 1);

    while(va < end){
        pte_t *pte = pagetable_walk(pt, va, 1);
        if(!pte) return -1;

        if(*pte & PTE_V) {
            panic("remap");
        }
        *pte = PA2PTE(pa) | flags | PTE_V;

        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
    return 0;
}

pagetable_t pagetable_create(void){
    return (pagetable_t)pmm_alloc();
}

void pagetable_unmap(pagetable_t pt, unsigned long va, unsigned long size, int do_free){
    va = va & ~(PAGE_SIZE - 1);
    unsigned long end = va + size;

    while (va < end){
        pte_t *pte = pagetable_walk(pt, va, 0);
        
        if(pte == NULL || !(*pte & PTE_V)) {
            panic("pagetable_unmap: page not mapped");
        }

        if ((*pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            panic("pagetable_unmap: not a leaf PTE");
        }
        if(do_free) {
            unsigned long pa = PTE2PA(*pte);
            pmm_free((void *)pa);
        }

        *pte = 0;
        asm volatile("sfence.vma %0, zero" : : "r"(va));

        va += PAGE_SIZE;

    }
}

void pagetable_free(pagetable_t pt){
    for(int i = 0; i < 512; i++){
        pte_t pte = pt[i];
        if(!(pte & PTE_V)) continue;

        if((pte & (PTE_R | PTE_W | PTE_X)) == 0){
            pagetable_t child = ((pagetable_t)PTE2PA(pte));
            pagetable_free(child);
            pt[i] = 0;
        } else {
            panic("pagetable_free: leaf PTE still mapped");
        }
    }
    pmm_free((void *)pt);
}