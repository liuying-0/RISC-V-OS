#include "elf.h"
#include "../mm/pmm.h"
#include "../mm/page_table.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../../include/memlayout.h"

unsigned long elf_load(pagetable_t pt,
                      const unsigned char *elf_data,
                      unsigned long elf_size,
                      unsigned long *out_sz) {
    struct elf64_hdr *eh = (struct elf64_hdr *)elf_data;

    // 验证 ELF 魔数
    if (eh->e_ident[0] != ELFMAG0 ||
        eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 ||
        eh->e_ident[3] != ELFMAG3) {
        printf("elf_load: bad magic\n");
        return 0;
    }

    // 验证 64 位 ELF
    if (eh->e_ident[4] != 2) {
        printf("elf_load: not 64-bit ELF\n");
        return 0;
    }

    // 验证 RISC-V 架构
    if (eh->e_machine != EM_RISCV) {
        printf("elf_load: not RISC-V (machine=%d)\n", eh->e_machine);
        return 0;
    }

    unsigned long max_addr = 0;

    // 遍历每个 Program Header
    for (int i = 0; i < eh->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)
            (elf_data + eh->e_phoff + i * eh->e_phentsize);

        // 只处理 PT_LOAD 类型的段
        if (ph->p_type != PT_LOAD)
            continue;

        if (ph->p_memsz == 0)
            continue;

        // 检查地址合法性（不能超过用户空间顶）
        if (ph->p_vaddr + ph->p_memsz > USER_TOP) {
            printf("elf_load: segment exceeds user space\n");
            return 0;
        }

        // 将 ELF 权限标志转换为页表权限（必须加 PTE_U）
        int pte_flags = PTE_U;
        if (ph->p_flags & PF_R) pte_flags |= PTE_R;
        if (ph->p_flags & PF_W) pte_flags |= PTE_W;
        if (ph->p_flags & PF_X) pte_flags |= PTE_X;

        // 计算需要映射的虚拟地址范围（页对齐）
        unsigned long va_start = ph->p_vaddr & ~(PAGE_SIZE - 1);
        unsigned long va_end = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        // 分配物理页并建立映射
        for (unsigned long va = va_start; va < va_end; va += PAGE_SIZE) {
            // 检查是否已经映射过（多个 segment 可能共享同一页）
            pte_t *pte = pagetable_walk(pt, va, 0);
            if (pte && (*pte & PTE_V))
                continue;

            void *pa = pmm_alloc();
            if (!pa) {
                printf("elf_load: out of memory\n");
                return 0;
            }
            pagetable_map(pt, va, (unsigned long)pa, PAGE_SIZE, pte_flags);
        }

        // 拷贝文件数据到映射的物理页
        unsigned long copied = 0;
        while (copied < ph->p_memsz) {
            unsigned long dest_va = ph->p_vaddr + copied;
            unsigned long page_offset = dest_va & (PAGE_SIZE - 1);

            pte_t *pte = pagetable_walk(pt, dest_va, 0);
            unsigned long dest_pa = PTE2PA(*pte) | page_offset;

            unsigned long n = PAGE_SIZE - page_offset;
            if (copied + n > ph->p_memsz) n = ph->p_memsz - copied;

            if (copied < ph->p_filesz) {
                unsigned long copy_n = n;
                if (copied + copy_n > ph->p_filesz)
                    copy_n = ph->p_filesz - copied;

                // 从 ELF 数据拷贝到物理页（物理页在用户页表中映射）
                // 内核页表通过恒等映射可以访问全部物理内存，直接用物理地址拷贝
                memcpy((void *)dest_pa, elf_data + ph->p_offset + copied, copy_n);

                // p_filesz 到 p_memsz 之间的部分填零（BSS 区域）
                if (copy_n < n)
                    memset((void *)(dest_pa + copy_n), 0, n - copy_n);
            } else {
                memset((void *)dest_pa, 0, n);
            }

            copied += n;
        }

        if (ph->p_vaddr + ph->p_memsz > max_addr)
            max_addr = ph->p_vaddr + ph->p_memsz;
    }

    *out_sz = max_addr;
    return eh->e_entry;
}