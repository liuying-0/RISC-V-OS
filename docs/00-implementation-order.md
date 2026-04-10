# 实现顺序与文件规划

本文档规划各阶段文件的**创建时机**和**内容演进**，避免实现顺序混乱（如 `memlayout.h` 在阶段 4 才创建，但阶段 3 的 `plic.c` 已需要地址常量）。

---

## 1. 文件创建与内容演进总览

|| 文件路径 | 首次创建 | 内容演进 |
||----------|----------|----------|
|| **include/** | | |
|| `include/sbi_call.h` | 阶段 2 | 阶段 2：基础 SBI 宏；阶段 3 开始使用 |
|| `include/memlayout.h` | 阶段 3 | 阶段 3：UART0_BASE, PLIC_BASE, CLINT_BASE 等 MMIO 地址；阶段 4：PHYS_MEM_*, PAGE_*；阶段 6：KERN_BASE, TRAMPOLINE, TRAPFRAME；阶段 9：VIRTIO_BASE, SWAP_* |
|| `include/syscall.h` | 阶段 6 | 系统调用号，内核与用户共享 |
|| **bootloader/** | | |
|| `bootloader/entry.S` | 阶段 1 | 阶段 1 完成；阶段 2 可能加 mtvec 设置 |
|| `bootloader/main.c` | 阶段 1 | 阶段 1 基础；阶段 3 加 switch_to_s_mode 等 |
|| `bootloader/sbi.c`, `sbi.h` | 阶段 2 | SBI 实现；阶段 3 加 set_timer |
|| **kernel/lib/** | | |
|| `kernel/lib/types.h` | 阶段 3 | 基本类型 |
|| `kernel/lib/string.c`, `string.h` | 阶段 3 | 内存/字符串函数 |
|| `kernel/lib/printf.c`, `printf.h` | 阶段 1 | 阶段 1 基础；阶段 3 集成 panic；阶段 8 加 console_lock |
|| `kernel/lib/riscv.h` | 阶段 3 | CSR 宏；阶段 8 加 intr_disable 等 |
|| **kernel/driver/** | | |
|| `kernel/driver/uart.c`, `uart.h` | 阶段 1 | 阶段 1 基础 |
|| `kernel/driver/plic.c`, `plic.h` | 阶段 3 | 使用 memlayout.h 的 PLIC_BASE（阶段 3 创建） |
|| `kernel/driver/virtio.h`, `virtio.c` | 阶段 9 | VirtIO 块设备驱动 |
|| **kernel/trap/** | | |
|| `kernel/trap/trap_entry.S` | 阶段 3 | 阶段 3 单路径 S-mode trap；阶段 6 重写为双路径 + trampoline |
|| `kernel/trap/trap.c` | 阶段 3 | trap 分发；阶段 6 增加 usertrap/usertrapret |
|| `kernel/trap/trap.h` | 阶段 3 | trap_context、handler 声明；阶段 6 扩展字段 |
|| `kernel/trap/irq.c` | 阶段 3 | 外部中断处理 |
|| `kernel/trap/exception.c` | 阶段 3 | 缺页等异常处理；阶段 7 实现 lazy allocation；阶段 9 支持 swap |
|| `kernel/trap/syscall.c` | 阶段 6 | 阶段 6 实现 sys_write/exit/sbrk |
|| `kernel/trap/timer.c` | 阶段 3 | 阶段 3 桩（打印）；阶段 5 加 timer_init、sbi_set_timer、yield |
|| **kernel/mm/** | | |
|| `kernel/mm/pmm.c`, `pmm.h` | 阶段 4 | 物理页帧分配器；阶段 8 加锁；阶段 9 加 pmm_alloc_with_swap |
|| `kernel/mm/page_table.c`, `page_table.h` | 阶段 4 | 页表操作 |
|| `kernel/mm/vmm.c`, `vmm.h` | 阶段 4 | 内核页表、启用分页；阶段 6 加 user_pagetable_create、copyin/copyout；阶段 9 加 pagetable_unmap_nofree |
|| `kernel/mm/swap.h`, `swap.c` | 阶段 9 | Swap 管理器、Clock 算法 |
|| **kernel/proc/** | | |
|| `kernel/proc/proc.c`, `proc.h` | 阶段 5 | 进程管理；阶段 6 加 user_proc_create；阶段 8 加 chan 字段和锁 |
|| `kernel/proc/scheduler.c`, `scheduler.h` | 阶段 5 | 调度器；阶段 6 扩展支持用户进程 satp 切换 |
|| `kernel/proc/switch.S` | 阶段 5 | 上下文切换 |
|| `kernel/proc/elf.c`, `elf.h` | 阶段 7 | ELF 加载器 |
|| **kernel/sync/** | | |
|| `kernel/sync/spinlock.c`, `spinlock.h` | 阶段 8 | 自旋锁 |
|| `kernel/sync/mutex.c`, `mutex.h` | 阶段 8 | 互斥锁 |
|| `kernel/sync/semaphore.c`, `semaphore.h` | 阶段 8 | 信号量 |
|| **kernel/main.c** | 阶段 3 | 阶段 3 入口；阶段 4 加 pmm_init、vmm_init；阶段 5 加进程创建；阶段 6 加 sscratch=0；阶段 7 加载用户 ELF；阶段 9 加 virtio/swap 初始化 |

---

## 2. 关键依赖与顺序修正

### 2.1 memlayout.h 提前创建

**问题**：阶段 3 的 `plic.c` 需要 `PLIC_BASE`，但 `memlayout.h` 在阶段 4 才创建，导致阶段 3 只能在 `plic.c` 里局部 `#define PLIC_BASE`。

**修正**：在**阶段 3** 就创建 `include/memlayout.h`，先放入 MMIO 相关常量：

```c
// include/memlayout.h（阶段 3 初版）

#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// QEMU virt 平台 MMIO 地址（阶段 3 需要）
#define UART0_BASE      0x10000000UL
#define PLIC_BASE       0x0C000000UL
#define CLINT_BASE      0x02000000UL

#endif
```

阶段 4 再追加：`PAGE_SIZE`、`PHYS_MEM_*`、`KSTACK_SIZE`、`USER_*`、`TRAMPOLINE`、`TRAPFRAME`。

### 2.2 timer 文件位置

- **`kernel/trap/timer.c`**：阶段 3 新建，实现 `timer_handler()`；阶段 5 扩展 `timer_init()`、`sbi_set_timer`、`yield()`。
- 不单独建 `kernel/driver/timer.c`，定时器逻辑统一放在 `kernel/trap/timer.c`。

### 2.3 文档拆分说明

原来的阶段 6 包含系统调用 + 用户态 + ELF + 同步 + swap，内容过多。本项目将其拆分为：

| 原阶段 | 新文档 |
|--------|---------|
| 阶段 6 | `06-syscall-and-usermode.md`：双路径 trap 框架、syscall 实现 |
| ~~原阶段 6~~ | `07-elf-and-user-process.md`：ELF 加载器、用户进程创建、hello 程序 |
| 阶段 7（同步） | `08-synchronization.md`：spinlock、mutex、semaphore |
| 阶段 8（swap） | `09-virtual-memory-and-swap.md`：VirtIO 驱动、swap 管理、Clock 算法 |

---

## 3. 各阶段新增/修改文件清单

|| 阶段 | 新建文件 | 修改文件 |
||------|----------|----------|
|| 01 | entry.S, main.c, uart.c/h, printf.c/h, linker.ld | Makefile |
|| 02 | trap_entry.S, trap.c/h, sbi.c/h, sbi_call.h | entry.S, main.c, Makefile |
|| 03 | **memlayout.h**（初版）, main.c, trap/*, plic.c/h, types.h, string.c/h, riscv.h | uart.c/h, printf.c, Makefile |
|| 04 | pmm.c/h, page_table.c/h, vmm.c/h | **memlayout.h**（扩展）, main.c, linker.ld, Makefile |
|| 05 | proc.c/h, scheduler.c/h, switch.S | timer.c（扩展）, main.c, Makefile |
|| 06 | **syscall.h** | syscall.c, trap_entry.S, proc.c/h, vmm.c/h, main.c, Makefile |
|| 07 | **elf.c/h**, user/* | proc.c, vmm.c, exception.c, main.c, Makefile |
|| 08 | sync/spinlock.c/h, sync/mutex.c/h, sync/semaphore.c/h | riscv.h, pmm.c, proc.c, printf.c, Makefile |
|| 09 | driver/virtio.h/c, mm/swap.h/c | memlayout.h, pmm.c, vmm.c, exception.c, proc.c, main.c, Makefile |

---

## 4. Makefile 与 CFLAGS 建议

- **阶段 2**：`CFLAGS += -I include`（若使用 `sbi_call.h`）
- **阶段 3**：`CFLAGS += -I include`（`memlayout.h`、`sbi_call.h`）
- **阶段 4**：确保 `-I include` 已存在，`memlayout.h` 可被 `vmm.c` 等包含
- **阶段 7**：确保 `-march=rv64imac -mabi=lp64` 用于用户程序编译
- **阶段 9**：QFLAGS 添加 VirtIO 磁盘配置

---

## 5. 使用建议

1. 按阶段顺序实现，每完成一阶段再进入下一阶段。
2. 新建文件时参考本表，避免过早创建或遗漏。
3. `memlayout.h`、`syscall.c` 等会随阶段**逐步扩展**，不必一次写全。
4. 建议每完成一个阶段就 `make run` 验证，避免问题积累。
