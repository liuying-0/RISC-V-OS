#ifndef __VMM_H__
#define __VMM_H__

#include "page_table.h"

extern pagetable_t kernel_pagetable;

void vmm_init(void);
void enable_paging(pagetable_t pt);

pagetable_t user_pagetable_create(void);

int copyin(pagetable_t pt, void *dst, unsigned long src, unsigned long len);

int copyout(pagetable_t pt, unsigned long dstva, const void *src, unsigned long len);

#endif