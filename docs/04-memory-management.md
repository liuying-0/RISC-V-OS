# 阶段 4：内存管理

## 目标

完成本阶段后，你将：
- 理解 RISC-V Sv39 虚拟内存架构
- 实现物理页帧分配器
- 实现三级页表的创建、映射和查找
- 建立内核页表并启用分页
- 理解内核地址空间布局

## 本阶段范围说明

**本阶段仅实现内核分页**，不涉及用户空间：
- 只建立**内核页表**（`kernel_pagetable`），映射内核代码、数据、MMIO 等
- **不支持**为每个进程创建独立页表
- **不支持**用户空间虚拟地址映射（如 `0x0` ~ `0x3F_FFFF_FFFF`）

用户空间支持将在**阶段 6（系统调用与用户态）**中实现，届时会：
- 为每个进程创建独立的用户页表
- 映射 trampoline、trapframe、用户程序代码/数据
- 在 U/S 切换时切换 `satp` 指向的页表

`memlayout.h` 中的 `USER_BASE`、`TRAMPOLINE`、`TRAPFRAME` 等常量是为阶段 6 预留的，本阶段可先定义，暂不使用。

---

## 1. 为什么需要虚拟内存

- **隔离性**：每个进程拥有独立的地址空间，互不干扰
- **安全性**：阻止用户程序访问内核内存
- **灵活性**：物理内存不连续也能映射为连续的虚拟地址
- **按需分配**：利用缺页异常实现 lazy allocation

---

## 2. RISC-V Sv39 分页机制

### 2.1 概述

Sv39 表示 39 位虚拟地址空间，使用**三级页表**。

```
虚拟地址（64位，有效部分39位）：
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ 63:39    │ 38:30    │ 29:21    │ 20:12    │ 11:0     │
│ 符号扩展  │ VPN[2]   │ VPN[1]   │ VPN[0]   │ offset   │
│ (25 bit) │ (9 bit)  │ (9 bit)  │ (9 bit)  │ (12 bit) │
└──────────┴──────────┴──────────┴──────────┴──────────┘

VPN = Virtual Page Number（虚拟页号）
offset = 页内偏移（4KB 页，12 bit）
```

**重要**：虚拟地址的高 25 位必须与第 38 位相同（符号扩展），否则是无效地址。这意味着有效虚拟地址有两个区域：
- `0x0000_0000_0000_0000` ~ `0x0000_003F_FFFF_FFFF`（低地址，用户空间）
- `0xFFFF_FFC0_0000_0000` ~ `0xFFFF_FFFF_FFFF_FFFF`（高地址，内核空间）

### 2.2 页表结构

每级页表有 512 个条目（PTE），每个 PTE 8 字节，一个页表恰好占一页（4KB）。

```
页表项（PTE，64位）：
┌──────────────────────────────┬─────────┬──────────┐
│ 63:54    │ 53:10            │ 9:8     │ 7:0      │
│ Reserved │ PPN              │ RSW     │ Flags    │
│          │ (物理页号，44 bit) │         │          │
└──────────┴──────────────────┴─────────┴──────────┘

Flags（低 8 位）：
  bit 0: V (Valid)      - 有效位
  bit 1: R (Read)       - 可读
  bit 2: W (Write)      - 可写
  bit 3: X (Execute)    - 可执行
  bit 4: U (User)       - 用户态可访问
  bit 5: G (Global)     - 全局映射（TLB 不随 ASID 刷新）
  bit 6: A (Accessed)   - 已被访问
  bit 7: D (Dirty)      - 已被写入
```

**关键规则**：
- 如果 R=0, W=0, X=0：这是一个**目录项**（指向下一级页表）
- 如果 R, W, X 任一不为 0：这是一个**叶子项**（最终映射）
- W=1 时必须 R=1（不允许只写）

### 2.3 地址翻译过程

```
satp 寄存器保存根页表的物理页号
  ▼
第 1 级：root_table[VPN[2]] → PTE → 如果是目录项，取出 PPN 得到第 2 级页表地址
  ▼
第 2 级：mid_table[VPN[1]]  → PTE → 如果是目录项，取出 PPN 得到第 3 级页表地址
  ▼
第 3 级：leaf_table[VPN[0]] → PTE → 叶子项，取出 PPN 拼接 offset 得到物理地址

物理地址 = PTE.PPN << 12 | offset
```

### 2.4 satp 寄存器

```
satp（Supervisor Address Translation and Protection）：
┌──────────┬──────────┬──────────────────────┐
│ 63:60    │ 59:44    │ 43:0                 │
│ MODE     │ ASID     │ PPN                  │
│ (4 bit)  │ (16 bit) │ (44 bit, 根页表物理页号) │
└──────────┴──────────┴──────────────────────┘

MODE:
  0  = Bare（无分页）
  8  = Sv39（39位虚拟地址，3级页表）
  9  = Sv48（48位虚拟地址，4级页表）
```

---

## 3. 物理页帧分配器

### 3.1 内存布局

```
物理内存布局（QEMU virt, 128MB RAM）：

0x8000_0000  ┌─────────────────┐
             │ 内核代码 (.text)  │
             │ 内核数据 (.data)  │
             │ 内核 BSS (.bss)  │
_kernel_end  ├─────────────────┤
             │                 │
             │   可用物理内存    │  ← 页帧分配器管理这部分
             │                 │
0x8800_0000  └─────────────────┘  （128MB RAM 结束地址）
```

`_kernel_end` 由链接脚本提供，从这里开始的内存可以自由分配。

### 3.2 空闲链表法

最简单的页帧分配器是**空闲链表**：每个空闲页帧的头部存放一个指针，指向下一个空闲页帧。

```c
// kernel/mm/pmm.h

#define PAGE_SIZE  4096

void pmm_init(void);
void *pmm_alloc(void);       // 分配一个物理页，返回物理地址
void pmm_free(void *pa);     // 释放一个物理页
```

```c
// kernel/mm/pmm.c

#include "pmm.h"
#include "../lib/string.h"

struct free_page {
    struct free_page *next;
};

static struct free_page *free_list = NULL;

extern char _kernel_end[];  // 来自链接脚本

#define PHYS_MEM_END  0x88000000UL   // 128MB RAM 结束

void pmm_init(void) {
    // 将 _kernel_end 到 PHYS_MEM_END 之间的内存，以页为单位加入空闲链表
    // 注意：起始地址需要向上对齐到 PAGE_SIZE
    unsigned long start = ((unsigned long)_kernel_end + PAGE_SIZE - 1)
                          & ~(PAGE_SIZE - 1);

    for (unsigned long addr = start; addr + PAGE_SIZE <= PHYS_MEM_END;
         addr += PAGE_SIZE) {
        pmm_free((void *)addr);
    }
}

void pmm_free(void *pa) {
    // 将页帧加入空闲链表头部
    struct free_page *page = (struct free_page *)pa;
    page->next = free_list;
    free_list = page;
}

void *pmm_alloc(void) {
    // 从空闲链表头部取出一个页帧
    struct free_page *page = free_list;
    if (page) {
        free_list = page->next;
        // 清零页面（页表等需要零初始化）
        memset(page, 0, PAGE_SIZE);
    }
    return (void *)page;
}
```

### 3.3 伙伴系统（Buddy System）

伙伴系统以 2 的幂次为单位分配连续物理页，支持一次分配多页，且能高效合并相邻空闲块。Linux 内核的物理页分配器即基于此。

**核心思想**：
- 块大小只能是 2^order 页（order=0 为 1 页，order=1 为 2 页，以此类推）
- 分配时若当前 order 无空闲块，则从更大 order 分裂为两个“伙伴”块
- 释放时若伙伴块空闲，则合并为更大 order 的块

**实现要点**：
- 每个 order 维护一条空闲链表
- 块头部存放链表指针（空闲时）或 order 信息（分配后用于释放时合并）
- 伙伴地址计算：`buddy = pa ^ (1 << (order + PAGE_SHIFT))`

```c
// kernel/mm/buddy.h（可选，替换 pmm 时使用）

#define PAGE_SIZE      4096
#define PAGE_SHIFT     12
#define MAX_ORDER      10   // 最大 2^10 = 1024 页 = 4MB

void buddy_init(void);
void *buddy_alloc(int order);      // 分配 2^order 页，返回物理地址
void buddy_free(void *pa, int order);
```

```c
// kernel/mm/buddy.c

#include "buddy.h"
#include "../lib/string.h"

#define PHYS_MEM_END   0x88000000UL

struct free_block {
    struct free_block *next;
};

static struct free_block *free_list[MAX_ORDER + 1];
extern char _kernel_end[];

// 获取 order 对应的块大小（字节）
static inline unsigned long block_size(int order) {
    return 1UL << (order + PAGE_SHIFT);
}

// 计算伙伴地址：同一 order 下，pa 与 buddy 相邻且大小相同
static void *get_buddy(void *pa, int order) {
    unsigned long addr = (unsigned long)pa;
    unsigned long size = block_size(order);
    return (void *)(addr ^ size);
}

void buddy_init(void) {
    for (int i = 0; i <= MAX_ORDER; i++)
        free_list[i] = NULL;

    unsigned long start = ((unsigned long)_kernel_end + PAGE_SIZE - 1)
                          & ~(PAGE_SIZE - 1);

    // 按页加入，buddy_free 会自动合并相邻伙伴
    for (unsigned long addr = start; addr + PAGE_SIZE <= PHYS_MEM_END;
         addr += PAGE_SIZE) {
        buddy_free((void *)addr, 0);
    }
}

void *buddy_alloc(int order) {
    if (order < 0 || order > MAX_ORDER) return NULL;

    int curr = order;
    while (curr <= MAX_ORDER && !free_list[curr])
        curr++;

    if (curr > MAX_ORDER) return NULL;

    // 若当前 order 无空闲，从更大 order 分裂
    while (curr > order) {
        struct free_block *blk = free_list[curr];
        free_list[curr] = blk->next;

        curr--;
        unsigned long size = block_size(curr);
        struct free_block *buddy = (struct free_block *)
            ((unsigned long)blk + size);

        buddy->next = free_list[curr];
        free_list[curr] = buddy;

        blk->next = free_list[curr];
        free_list[curr] = blk;
    }

    struct free_block *blk = free_list[order];
    free_list[order] = blk->next;
    memset(blk, 0, block_size(order));
    return (void *)blk;
}

void buddy_free(void *pa, int order) {
    if (order < 0 || order > MAX_ORDER) return;

    while (order < MAX_ORDER) {
        void *buddy = get_buddy(pa, order);
        struct free_block **prev = &free_list[order];
        struct free_block *p = *prev;

        // 在 free_list[order] 中查找并移除 buddy
        while (p) {
            if (p == buddy) {
                *prev = p->next;
                break;
            }
            prev = &p->next;
            p = p->next;
        }

        if (!p) break;  // buddy 不在空闲链表中，无法合并

        // 合并：取低地址块作为新块，继续尝试与上层合并
        if ((unsigned long)pa < (unsigned long)buddy)
            pa = pa;
        else
            pa = buddy;
        order++;
    }

    struct free_block *blk = (struct free_block *)pa;
    blk->next = free_list[order];
    free_list[order] = blk;
}
```

**与 pmm 的兼容**：若需保持 `pmm_alloc()` / `pmm_free()` 接口，可在其内部调用 `buddy_alloc(0)` 和 `buddy_free(pa, 0)`，即单页分配/释放。

**Bitmap 分配器**（简要）：用 bit 数组标记每页是否空闲，适合需要快速判断某页是否已分配的场景，实现较简单，此处不展开。

---

## 4. 页表操作

### 4.1 核心函数

你需要实现以下页表操作函数：

```c
// kernel/mm/page_table.h

typedef unsigned long pte_t;       // 页表项类型
typedef unsigned long *pagetable_t; // 页表类型（指向 512 个 PTE 的数组）

// PTE 标志位
#define PTE_V   (1 << 0)  // Valid
#define PTE_R   (1 << 1)  // Read
#define PTE_W   (1 << 2)  // Write
#define PTE_X   (1 << 3)  // Execute
#define PTE_U   (1 << 4)  // User
#define PTE_G   (1 << 5)  // Global
#define PTE_A   (1 << 6)  // Accessed
#define PTE_D   (1 << 7)  // Dirty

// 从 PTE 中提取物理页号
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 从物理地址生成 PTE（不含标志位）
#define PA2PTE(pa) ((((unsigned long)(pa)) >> 12) << 10)

// 从虚拟地址中提取 VPN[level]
// level: 0, 1, 2
#define VA_VPN(va, level) (((va) >> (12 + 9 * (level))) & 0x1FF)

// 创建新页表（返回物理地址）
pagetable_t pagetable_create(void);

// 映射：将虚拟地址 va 映射到物理地址 pa，权限为 flags
// size 可以是 PAGE_SIZE 的整数倍（映射多个页）
int pagetable_map(pagetable_t pt, unsigned long va, unsigned long pa,
                  unsigned long size, int flags);

// 取消映射：do_free=1 时同时释放叶子 PTE 指向的物理页
void pagetable_unmap(pagetable_t pt, unsigned long va,
                     unsigned long size, int do_free);

// 递归释放整棵页表树（调用前须先 unmap 所有叶子映射）
void pagetable_free(pagetable_t pt);

// 查找虚拟地址对应的 PTE（如果不存在，alloc=1 则创建中间页表）
pte_t *pagetable_walk(pagetable_t pt, unsigned long va, int alloc);
```

### 4.2 walk 函数实现（核心）

`walk` 是页表操作的核心，它遍历三级页表，找到虚拟地址对应的最终 PTE。

> **恒等映射下的地址**：`PTE2PA(*pte)` 得到的是**物理地址**。在恒等映射方案中，物理地址 = 虚拟地址，故可直接当作指针使用（`pt = (pagetable_t)PTE2PA(*pte)`）。若采用高地址映射，需将物理地址转换为内核虚拟地址后再访问。

```c
pte_t *pagetable_walk(pagetable_t pt, unsigned long va, int alloc) {
    for (int level = 2; level > 0; level--) {
        // 取出当前级别的 VPN 索引
        int idx = VA_VPN(va, level);
        pte_t *pte = &pt[idx];

        if (*pte & PTE_V) {
            // PTE 有效，取出下一级页表的物理地址
            pt = (pagetable_t)PTE2PA(*pte);
        } else {
            // PTE 无效
            if (!alloc) return NULL;

            // 分配新页表
            pagetable_t new_pt = (pagetable_t)pmm_alloc();
            if (!new_pt) return NULL;

            // 填入目录项（只设置 V 位，不设置 R/W/X → 这是目录项）
            *pte = PA2PTE(new_pt) | PTE_V;
            pt = new_pt;
        }
    }

    // 返回最后一级的 PTE 指针
    return &pt[VA_VPN(va, 0)];
}
```

### 4.3 map 函数

```c
int pagetable_map(pagetable_t pt, unsigned long va, unsigned long pa,
                  unsigned long size, int flags) {
    unsigned long end = va + size;

    // 页对齐
    va = va & ~(PAGE_SIZE - 1);
    pa = pa & ~(PAGE_SIZE - 1);

    while (va < end) {
        pte_t *pte = pagetable_walk(pt, va, 1);
        if (!pte) return -1;

        if (*pte & PTE_V) {
            // 已经映射了，这是个错误（或者你可以选择覆盖）
            panic("remap");
        }

        *pte = PA2PTE(pa) | flags | PTE_V;

        va += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
    return 0;
}
```

### 4.4 pagetable_create、pagetable_unmap 与 pagetable_free

#### 4.4.1 pagetable_create — 创建一张空页表

创建页表 = 分配一个物理页 + 清零。

**为什么必须清零？** 页表中有 512 个 PTE，每个 PTE 的 V 位（bit 0）= 0 表示"无效"。清零后所有 PTE 都是无效状态，不会产生错误的映射。如果不清零，残留的垃圾数据可能让某些 PTE 的 V 位恰好为 1，导致硬件访问到随机的物理地址。

```c
pagetable_t pagetable_create(void) {
    // pmm_alloc 内部已经 memset 清零
    return (pagetable_t)pmm_alloc();
}
```

> 如果你的 `pmm_alloc` 没有清零，需要在这里手动 `memset(pt, 0, PAGE_SIZE)`。

---

#### 4.4.2 pagetable_unmap — 取消虚拟地址映射

##### 它做什么？

给定一段虚拟地址范围 `[va, va+size)`，找到每一页对应的 Level 0 叶子 PTE，将其清零（使映射失效），并可选地释放该 PTE 指向的物理页。

##### 需要考虑的三个问题

**问题 1：是否释放物理页？**

叶子 PTE 指向一个物理页。取消映射时，这个物理页是否应该释放？

- 如果这块物理内存只被这一个映射使用 → 应该释放，否则就泄漏了
- 如果这块物理内存还被别处使用（如共享内存、MMIO 设备地址） → 不能释放

因此增加一个 `do_free` 参数，由调用者决定：

| do_free | 行为 | 使用场景 |
|---------|------|----------|
| 1 | 释放叶子 PTE 指向的物理页 | 进程退出，回收其所有内存 |
| 0 | 只清 PTE，不释放物理页 | 取消 MMIO 设备映射、共享内存映射 |

**问题 2：TLB 刷新**

CPU 内部有 TLB（Translation Lookaside Buffer），缓存了最近的虚拟→物理地址翻译。修改 PTE 后如果不刷新 TLB，CPU 可能仍使用旧的翻译结果去访问已释放的物理页，造成数据损坏或崩溃。

```
修改 PTE 前：TLB 缓存 va → pa_old
修改 PTE 后：PTE 已清零，但 TLB 仍记着 va → pa_old
                              ↓
              CPU 访问 va → 使用旧缓存 → 访问 pa_old → 出错！

解决：修改 PTE 后执行 sfence.vma，强制 TLB 丢弃该条目
```

**问题 3：中间级页表是否回收？**

`unmap` 只负责清除 Level 0 的叶子 PTE。Level 1 和 Level 2 的中间页表不在这里回收。原因是：判断一个中间页表是否"全空"代价较高（需遍历 512 个 PTE），而且在大部分场景下（如进程退出）我们会直接调用 `pagetable_free` 递归释放整棵页表树，没必要在 `unmap` 中逐个检查。

##### 完整执行流程

以 `pagetable_unmap(pt, 0x80010000, 0x3000, 1)` 为例（取消映射 3 页，释放物理页）：

```
va 页对齐 → 0x80010000（已对齐）
end = 0x80010000 + 0x3000 = 0x80013000

循环每次处理一页（4KB = 0x1000）：

  第 1 轮：va = 0x80010000
  ├── walk(pt, va, 0) → 遍历三级页表，找到 Level 0 叶子 PTE
  ├── 检查 PTE 有效（V=1）且是叶子项（R/W/X 至少一个≠0） ✓
  ├── pa = PTE2PA(*pte)     ← 取出物理地址
  ├── pmm_free(pa)          ← do_free=1，释放物理页
  ├── *pte = 0              ← 清零 PTE（V=0，映射失效）
  ├── sfence.vma va         ← 刷新该地址的 TLB 缓存
  └── va += 0x1000

  第 2 轮：va = 0x80011000
  └── ...同上...

  第 3 轮：va = 0x80012000
  └── ...同上...

  va = 0x80013000 >= end → 结束
```

##### 实现

```c
void pagetable_unmap(pagetable_t pt, unsigned long va,
                     unsigned long size, int do_free) {
    va = va & ~(PAGE_SIZE - 1);
    unsigned long end = va + size;

    while (va < end) {
        pte_t *pte = pagetable_walk(pt, va, 0);
        if (pte == NULL || !(*pte & PTE_V)) {
            panic("pagetable_unmap: page not mapped");
        }

        if ((*pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            panic("pagetable_unmap: not a leaf PTE");
        }

        if (do_free) {
            unsigned long pa = PTE2PA(*pte);
            pmm_free((void *)pa);
        }

        *pte = 0;
        asm volatile("sfence.vma %0, zero" :: "r"(va));

        va += PAGE_SIZE;
    }
}
```

**逐步解释**：

| 步骤 | 代码 | 为什么 |
|------|------|--------|
| 页对齐 | `va & ~(PAGE_SIZE - 1)` | 清除低 12 位，保证 va 是 4KB 的整数倍，因为页表以页为粒度工作 |
| 查找 PTE | `pagetable_walk(pt, va, 0)` | alloc=0：只查找，不创建。如果中间级页表不存在则返回 NULL |
| 有效性检查 | `*pte & PTE_V` | 如果该页从未被映射（V=0），取消映射就是逻辑错误 |
| 叶子检查 | `*pte & (PTE_R\|PTE_W\|PTE_X)` | 如果 R=W=X=0，这是目录项（指向下一级页表），不是最终映射，不应该在这里操作 |
| 释放物理页 | `pmm_free(PTE2PA(*pte))` | do_free=1 时，回收叶子 PTE 指向的物理内存 |
| 清除 PTE | `*pte = 0` | 将 64 位全部置 0，V=0 → 硬件认为该映射无效 |
| 刷新 TLB | `sfence.vma %0, zero` | 清除 CPU 对该虚拟地址的翻译缓存。`zero` 表示所有地址空间（ASID） |

---

#### 4.4.3 pagetable_free — 递归释放整棵页表树

##### 它做什么？

当一个进程退出时，需要回收它的整棵页表占用的所有物理页。页表是一棵三层树：

```
Level 2（根页表）       ←  1 页
   ├── Level 1 页表     ← 最多 512 页
   │    ├── Level 0 页表 ← 最多 512×512 页
   │    └── ...
   └── ...
```

`pagetable_free` 递归遍历这棵树，从底向上释放每一个页表页。

##### 前置条件

调用 `pagetable_free` 之前，必须先用 `pagetable_unmap` 清除所有叶子映射。因为 `pagetable_free` 只负责释放页表本身的页，不释放叶子 PTE 指向的用户数据页。这样职责分离：

```
进程退出的释放流程：

  1. pagetable_unmap(pt, user_start, user_size, 1)
     → 清除所有叶子 PTE，释放用户数据占用的物理页

  2. pagetable_free(pt)
     → 递归释放所有中间级页表和根页表本身
```

如果不先 unmap 就直接 free，叶子 PTE 指向的物理页就泄漏了（没人释放它们）。

##### 递归过程图示

```
pagetable_free(root_pt)                      ← Level 2 根页表
│
├── 遍历 root_pt[0..511]：
│   │
│   ├── pt[i] 的 V=0  → 无效，跳过
│   │
│   ├── pt[i] 的 V=1, R=W=X=0  → 这是目录项，指向一个 Level 1 页表
│   │   │
│   │   └── 递归调用 pagetable_free(level1_pt)
│   │       │
│   │       ├── 遍历 level1_pt[0..511]：
│   │       │   ├── V=0 → 跳过
│   │       │   ├── V=1, R=W=X=0 → 目录项，指向 Level 0 页表
│   │       │   │   └── pmm_free(level0_pt)  ← Level 0 页表无子节点，直接释放
│   │       │   └── V=1, R/W/X≠0 → 叶子还在！panic（应先 unmap）
│   │       │
│   │       └── pmm_free(level1_pt)  ← Level 1 页表本身释放
│   │
│   └── pt[i] 的 V=1, R/W/X≠0  → 叶子还在！panic（应先 unmap）
│
└── pmm_free(root_pt)  ← 最后释放根页表
```

##### 实现

```c
void pagetable_free(pagetable_t pt) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V)) continue;

        if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // 目录项：指向下一级页表，递归释放
            pagetable_t child = (pagetable_t)PTE2PA(pte);
            pagetable_free(child);
            pt[i] = 0;
        } else {
            // 叶子项还在 → 调用者忘了先 unmap
            panic("pagetable_free: leaf PTE still mapped");
        }
    }

    pmm_free((void *)pt);
}
```

**逐步解释**：

| 步骤 | 代码 | 为什么 |
|------|------|--------|
| 遍历 512 项 | `for (int i = 0; i < 512; ...)` | 一个页表恰好 512 个 PTE（每个 8 字节，共 4KB = 一页） |
| 跳过无效项 | `!(pte & PTE_V)` | V=0 表示该槽位没有被使用 |
| 判断目录项 | `(pte & (PTE_R\|PTE_W\|PTE_X)) == 0` | V=1 但 R=W=X=0 → 这是一个目录项，指向下一级页表 |
| 递归释放子页表 | `pagetable_free(child)` | 先释放子树，再释放自己（后序遍历） |
| 清除目录项 | `pt[i] = 0` | 释放后清零，防止悬垂指针 |
| panic 叶子项 | `panic(...)` | 安全检查：叶子映射应在调用前通过 unmap 清除 |
| 释放当前页表 | `pmm_free((void *)pt)` | 子树全部释放后，释放当前页表页本身 |

> **递归深度**：最多 3 层（Level 2 → Level 1 → Level 0），不会栈溢出。

---

#### 4.4.4 完整文件参考

将以上三个函数与 4.2、4.3 节的 `walk`、`map` 放在同一个文件中：

```c
// kernel/mm/page_table.c

#include "page_table.h"
#include "pmm.h"
#include "../lib/string.h"
#include "../lib/printf.h"

// --- 4.4.1 ---
pagetable_t pagetable_create(void) {
    return (pagetable_t)pmm_alloc();
}

// --- 4.2 walk（见 4.2 节）---
pte_t *pagetable_walk(pagetable_t pt, unsigned long va, int alloc) {
    // ... 省略，见 4.2 节 ...
}

// --- 4.3 map（见 4.3 节）---
int pagetable_map(pagetable_t pt, unsigned long va, unsigned long pa,
                  unsigned long size, int flags) {
    // ... 省略，见 4.3 节 ...
}

// --- 4.4.2 ---
void pagetable_unmap(pagetable_t pt, unsigned long va,
                     unsigned long size, int do_free) {
    va = va & ~(PAGE_SIZE - 1);
    unsigned long end = va + size;

    while (va < end) {
        pte_t *pte = pagetable_walk(pt, va, 0);
        if (pte == NULL || !(*pte & PTE_V))
            panic("pagetable_unmap: page not mapped");
        if ((*pte & (PTE_R | PTE_W | PTE_X)) == 0)
            panic("pagetable_unmap: not a leaf PTE");

        if (do_free)
            pmm_free((void *)PTE2PA(*pte));

        *pte = 0;
        asm volatile("sfence.vma %0, zero" :: "r"(va));

        va += PAGE_SIZE;
    }
}

// --- 4.4.3 ---
void pagetable_free(pagetable_t pt) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V)) continue;

        if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            pagetable_free((pagetable_t)PTE2PA(pte));
            pt[i] = 0;
        } else {
            panic("pagetable_free: leaf PTE still mapped");
        }
    }
    pmm_free((void *)pt);
}
```

---

## 5. 建立内核页表

### 5.1 内核地址空间布局设计

有两种常见方案：

**方案 A：恒等映射（Identity Mapping）**
- 虚拟地址 = 物理地址
- 简单直接，但内核和用户空间不好隔离

**方案 B：高地址映射**（推荐）
- 内核映射到高地址（如 `0xFFFF_FFC0_0000_0000` + 物理偏移）
- 低地址留给用户空间
- Linux、xv6 等都采用这种方案

**建议先用方案 A（恒等映射）让系统跑起来**，后续再改为方案 B。

### 5.2 vmm.c / vmm.h — 虚拟内存管理

将内核页表的建立和分页启用放在 `vmm.c` 中，与底层的页表操作（`page_table.c`）分离：

```c
// kernel/mm/vmm.h

#ifndef __VMM_H__
#define __VMM_H__

#include "page_table.h"

extern pagetable_t kernel_pagetable;   // 全局内核页表

void vmm_init(void);              // 建立内核页表并启用分页
void enable_paging(pagetable_t pt);

#endif
```

内核需要映射的区域：

```c
// kernel/mm/vmm.c

#include "vmm.h"
#include "pmm.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "memlayout.h"   // 需 CFLAGS += -I include

pagetable_t kernel_pagetable = NULL;

void vmm_init(void) {
    kernel_pagetable = pagetable_create();
    pagetable_t kernel_pt = kernel_pagetable;

    // 1. 映射内核代码段（可读 + 可执行）
    extern char _text_start[], _text_end[];
    pagetable_map(kernel_pt,
                  (unsigned long)_text_start,
                  (unsigned long)_text_start,
                  _text_end - _text_start,
                  PTE_R | PTE_X);

    // 2. 映射只读数据段（只读，不可执行）
    extern char _rodata_start[], _rodata_end[];
    pagetable_map(kernel_pt,
                  (unsigned long)_rodata_start,
                  (unsigned long)_rodata_start,
                  _rodata_end - _rodata_start,
                  PTE_R);

    // 3. 映射内核数据段 + BSS（可读写）
    extern char _data_start[], _kernel_end[];
    pagetable_map(kernel_pt,
                  (unsigned long)_data_start,
                  (unsigned long)_data_start,
                  _kernel_end - _data_start,
                  PTE_R | PTE_W);

    // 4. 映射可用物理内存（可读写）
    pagetable_map(kernel_pt,
                  (unsigned long)_kernel_end,
                  (unsigned long)_kernel_end,
                  PHYS_MEM_END - (unsigned long)_kernel_end,
                  PTE_R | PTE_W);

    // 5. 映射 UART MMIO（可读写）
    pagetable_map(kernel_pt, 0x10000000, 0x10000000, PAGE_SIZE,
                  PTE_R | PTE_W);

    // 6. 映射 PLIC
    pagetable_map(kernel_pt, 0x0C000000, 0x0C000000, 0x4000000,
                  PTE_R | PTE_W);

    // 7. 映射 CLINT
    // 注意：CLINT 在 M-mode 才能直接访问，S-mode 通过 SBI
    // 如果需要的话可以映射

    // 启用分页
    enable_paging(kernel_pt);
}
```

> **提示**：`linker.ld` 需导出 `_text_start`、`_text_end`、`_rodata_start`、`_rodata_end`、`_data_start`、`_kernel_end`。
>
> **关键**：`.rodata` 单独映射时，每个段之间必须用 `ALIGN(0x1000)` 页对齐，否则不同权限的段会落在同一个 4KB 页内，权限无法区分。链接脚本参考：
> ```ld
> .text : {
>     PROVIDE(_text_start = .);
>     *(.text .text.*)
>     PROVIDE(_text_end = .);
> }
>
> . = ALIGN(0x1000);
>
> .rodata : {
>     PROVIDE(_rodata_start = .);
>     *(.rodata .rodata.*)
>     PROVIDE(_rodata_end = .);
> }
>
> . = ALIGN(0x1000);
>
> .data : {
>     PROVIDE(_data_start = .);
>     *(.data .data.*)
>     PROVIDE(_data_end = .);
> }
>
> .bss : {
>     PROVIDE(_bss_start = .);
>     *(.bss .bss.*)
>     *(COMMON)
>     PROVIDE(_bss_end = .);
> }
>
> PROVIDE(_kernel_end = .);
> ```

### 5.3 启用分页（也在 vmm.c 中）

```c
// kernel/mm/vmm.c（续）

void enable_paging(pagetable_t kernel_pt) {
    // 构造 satp 值：MODE = 8 (Sv39), ASID = 0, PPN = 页表物理地址 >> 12
    unsigned long satp_val = (8UL << 60) | ((unsigned long)kernel_pt >> 12);

    asm volatile("csrw satp, %0" :: "r"(satp_val));

    // 刷新 TLB
    asm volatile("sfence.vma zero, zero");
}
```

### 5.4 启用分页的关键注意事项

**这是最容易出 bug 的地方！**

启用分页后，所有地址（包括下一条指令的 PC）都会通过页表翻译。如果页表没有正确映射当前正在执行的代码的地址，CPU 立即产生缺页异常 → 系统崩溃。

**恒等映射方案的优势**：因为虚拟地址 = 物理地址，所以启用分页前后代码的地址不变，不会出问题。

**高地址映射方案的挑战**：需要一个"过渡"：
1. 在页表中同时建立恒等映射和高地址映射
2. 启用分页（此时恒等映射保证代码继续执行）
3. 跳转到高地址（通过一个绝对跳转指令）
4. 移除恒等映射

---

## 6. TLB（Translation Lookaside Buffer）

TLB 是页表的硬件缓存。修改页表后，必须刷新 TLB，否则处理器可能使用旧的翻译结果。

```c
// 刷新所有 TLB 条目
asm volatile("sfence.vma zero, zero");

// 刷新特定虚拟地址的 TLB 条目
asm volatile("sfence.vma %0, zero" :: "r"(va));
```

---

## 7. memlayout.h（阶段 4 扩展）

`include/memlayout.h` 在**阶段 3** 已创建初版（UART0_BASE、PLIC_BASE、CLINT_BASE）。本阶段**扩展**以下内容：

```c
// include/memlayout.h（在阶段 3 初版基础上追加）

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

// 物理内存（阶段 3 已有 UART0_BASE、PLIC_BASE、CLINT_BASE）
#define PHYS_MEM_START  0x80000000UL
#define PHYS_MEM_SIZE   (128 * 1024 * 1024)  // 128MB
#define PHYS_MEM_END    (PHYS_MEM_START + PHYS_MEM_SIZE)

// 内核栈大小
#define KSTACK_SIZE     (PAGE_SIZE * 2)  // 8KB

// 用户空间范围（Sv39 低地址，阶段 6 使用）
#define USER_BASE       0x0000000000000000UL
#define USER_TOP        0x0000004000000000UL  // 256GB

// Trampoline 页（阶段 6 使用）
#define TRAMPOLINE      (USER_TOP - PAGE_SIZE)
#define TRAPFRAME       (TRAMPOLINE - PAGE_SIZE)
```

---

## 8. 调试技巧

### 8.1 打印页表

实现一个函数打印页表内容，非常有用：

```c
void pagetable_dump(pagetable_t pt, int level) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        if (!(pte & PTE_V)) continue;

        // 打印缩进
        for (int j = 0; j < (2 - level); j++) printf("  ");

        printf("PTE[%d]: 0x%lx -> PA: 0x%lx, flags: ", i, pte, PTE2PA(pte));
        if (pte & PTE_R) printf("R");
        if (pte & PTE_W) printf("W");
        if (pte & PTE_X) printf("X");
        if (pte & PTE_U) printf("U");
        printf("\n");

        // 如果是目录项，递归打印
        if ((pte & (PTE_R | PTE_W | PTE_X)) == 0 && level > 0) {
            pagetable_dump((pagetable_t)PTE2PA(pte), level - 1);
        }
    }
}
```

### 8.2 QEMU 的 info mem 命令

在 QEMU monitor 中（按 Ctrl-A 然后 C 切换到 monitor），输入 `info mem` 可以查看当前页表映射。

---

## 本阶段新增文件清单

```
kernel/mm/
├── pmm.c              # 物理页帧分配器
├── pmm.h
├── vmm.c              # 虚拟内存管理（内核页表、启用分页）
├── vmm.h
├── page_table.c       # 页表底层操作（walk, map, unmap）
└── page_table.h

include/
└── memlayout.h        # 阶段 3 已创建，本阶段扩展
```

---

## 本阶段 Makefile 更新

```makefile
# 在上阶段基础上追加：
CFLAGS += -I include   # 让 kernel/mm/vmm.c 等能 #include "memlayout.h"

SRCS_C += kernel/mm/pmm.c \
          kernel/mm/page_table.c \
          kernel/mm/vmm.c
```

同时在 `kernel/main.c` 中的 `kernel_main` 里追加调用：

```c
pmm_init();
vmm_init();   // 建立内核页表并启用 Sv39 分页
```

---

## 检查清单

- [ ] 理解了 Sv39 三级页表的结构和地址翻译过程
- [ ] 理解了 PTE 各标志位的含义
- [ ] 实现了物理页帧分配器（`kernel/mm/pmm.c`）
- [ ] 实现了 `pagetable_walk` 函数（`kernel/mm/page_table.c`）
- [ ] 实现了 `pagetable_map` 和 `pagetable_unmap` 函数
- [ ] 创建了 `kernel/mm/vmm.c`，实现 `vmm_init()` 和 `enable_paging()`
- [ ] 建立了内核页表（恒等映射所有必要区域）
- [ ] 成功启用分页（写入 satp，刷新 TLB）
- [ ] 启用分页后系统仍然正常运行
- [ ] 更新了 `linker.ld` 导出必要的段地址符号
- [ ] 扩展了 `include/memlayout.h`（阶段 3 已创建初版，本阶段追加 PHYS_MEM_*、PAGE_*、USER_* 等）

---

## 下一步

有了内存管理，进入 [阶段 5：进程管理](05-process-management.md)，实现进程抽象和调度。
