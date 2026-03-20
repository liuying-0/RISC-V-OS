#ifndef __VMM_H__
#define __VMM_H__

#include "page_table.h"

extern pagetable_t kernel_pagetable;

void vmm_init(void);
void enable_paging(pagetable_t pt);

#endif