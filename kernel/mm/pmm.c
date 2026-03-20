#include "pmm.h"
#include "../lib/string.h"
#include "memlayout.h"


struct free_page {
    struct free_page *next;
};

static struct free_page *free_list = NULL;

extern char _kernel_end[];


void pmm_init(void){
    unsigned long start = ((unsigned long)_kernel_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for(unsigned long addr = start; addr + PAGE_SIZE <= PHYS_MEM_END; addr += PAGE_SIZE){
        pmm_free((void *)addr);
    }
}

void pmm_free(void *pa){
    struct free_page *page = ( struct free_page *)pa;
    page->next = free_list;
    free_list = page;
}

void *pmm_alloc(void) {
    struct free_page *page = free_list;
    if(page){
        free_list = page->next;
        memset(page, 0, PAGE_SIZE);
    }
    return (void *)page;
}
