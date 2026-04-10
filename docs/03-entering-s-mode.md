# 阶段 3：进入 S-mode 与内核初始化

## 目标

完成本阶段后，你将：

- 理解 RISC-V 的特权级切换机制
- 实现从 M-mode 到 S-mode 的切换
- 配置中断/异常委托
- 在 S-mode 下实现 trap 处理框架
- 通过 SBI 调用完成 S-mode 下的串口输出和定时器

---

## 实现位置总览（本阶段要改/要建的文件）

按「先 M-mode 再 S-mode、先初始化再 trap」的顺序实现时，可以按下面表格对照，避免漏写或写错文件。


| 功能                      | 文件路径                               | 操作               | 说明                                                                                                                                                                              |
| ----------------------- | ---------------------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| M-mode：委托 trap 给 S-mode | `bootloader/main.c`                | **修改**           | 新增函数 `delegate_traps()`，在 `main()` 里调用                                                                                                                                          |
| M-mode：配置 PMP           | `bootloader/main.c`                | **修改**           | 新增函数 `configure_pmp()`，在 `main()` 里调用                                                                                                                                           |
| M-mode：切换到 S-mode       | `bootloader/main.c`                | **修改**           | 新增函数 `switch_to_s_mode()`，在 `main()` 最后调用；`main()` 里先 `uart_init`、`delegate_traps()`、`configure_pmp()`，再 `switch_to_s_mode()`                                                   |
| S-mode：trap 上下文结构体      | `kernel/trap/trap.h`               | **新建**           | 定义 `struct trap_context`（含 `sepc`），声明 `s_trap_handler`                                                                                                                          |
| S-mode：trap 汇编入口        | `kernel/trap/trap_entry.S`         | **新建**           | 实现 `s_trap_entry`：保存寄存器、读 `sepc`、调 `s_trap_handler`、恢复、`sret`（命名用 trap_entry.S 避免与 trap.c 同目录生成同名 trap.o 冲突）                                                                    |
| S-mode：trap 分发          | `kernel/trap/trap.c`               | **新建**           | 实现 `s_trap_handler(scause, ctx)`，按中断/异常分支，调用下面各 handler                                                                                                                         |
| S-mode：外部中断处理           | `kernel/trap/irq.c`                | **新建**           | 实现 `external_irq_handler()`，内部调 PLIC claim/complete 和 UART 中断处理                                                                                                                 |
| S-mode：缺页等异常            | `kernel/trap/exception.c`          | **新建**           | 实现 `page_fault_handler()` 等，当前可只打印并 `panic`                                                                                                                                     |
| S-mode：系统调用桩            | `kernel/trap/syscall.c`            | **新建**           | 实现 `syscall_handler(ctx)` 桩，例如直接 `panic`                                                                                                                                        |
| S-mode：定时器处理            | `kernel/trap/timer.c`              | **新建**           | 实现 `timer_handler()`，本阶段先打印一行；阶段 5（进程管理）时补充设置下次定时器 + 调度逻辑                                                                                                                       |
| M-mode：定时器中断里通知 S-mode  | `bootloader/trap.c`                | **修改**           | 在 `m_trap_handler` 的 **中断** 分支、`case 7` 里：清 mtimecmp + 置 `**mip.STIP`**（推荐），S-mode 在 case 5 调 `timer_handler()`                                                                 |
| M-mode：SBI 定时器接口        | `bootloader/sbi.c`                 | **修改**           | 在 `sbi_handler` 里增加 EID `0x00`（set_timer），写 CLINT `mtimecmp`                                                                                                                    |
| CSR 工具宏                 | `kernel/lib/riscv.h`               | **新建**           | `csrr`/`csrw`/`csrs`/`csrc` 及 MSTATUS/SSTATUS/SIE 等常量                                                                                                                           |
| 内核类型定义                  | `kernel/lib/types.h`               | **新建**（若无）       | `uint64_t`、`size_t`、`NULL` 等                                                                                                                                                    |
| 内核字符串/内存                | `kernel/lib/string.c` + `string.h` | **新建**           | `memset`、`memcpy`、`strlen` 等                                                                                                                                                    |
| 内核 printf               | `kernel/lib/printf.c` + `printf.h` | **已有**           | 底层通过 SBI (`sbi_console_putchar`) 输出，函数名保持 `printf`                                                                                                                              |
| 内核 panic                | `kernel/lib/printf.c`              | **已有**           | `panic()` 直接集成在 `printf.c` 里（已实现），不再单独建 `panic.c`                                                                                                                               |
| 内存布局常量                | `include/memlayout.h`              | **新建**           | UART0_BASE、PLIC_BASE、CLINT_BASE 等 MMIO 地址；阶段 4 扩展 PHYS_MEM_*、PAGE_* 等                                                                                                       |
| PLIC 驱动                 | `kernel/driver/plic.c` + `plic.h`  | **新建**           | `plic_init()`、`plic_init_hart()`、`plic_claim()`、`plic_complete()`，使用 memlayout.h 的 PLIC_BASE                                                                                     |
| UART 中断处理               | `kernel/driver/uart.c` + `uart.h`  | **修改**           | 增加 `uart_intr()`；在 `uart_init()` 里使能接收中断                                                                                                                                        |
| 内核入口                    | `kernel/main.c`                    | **新建**           | `kernel_main()`：设 `stvec`、初始化 UART/PLIC、开 S-mode 中断、`printf`、可设第一次定时器                                                                                                           |
| SBI 调用封装                | `include/sbi_call.h`               | **新建**（若阶段 2 未建） | `sbi_call()`、`sbi_console_putchar`、`sbi_set_timer`、`sbi_shutdown` 等宏/内联                                                                                                         |
| 链接与编译                   | `Makefile`、`linker.ld`             | **修改**           | 把 `kernel/main.c`、`kernel/trap/*.c`、`kernel/driver/plic.c`、`kernel/lib/*.c`、`kernel/trap/trap_entry.S` 加入编译；必要时加 `-I include`。同目录下 .c 与 .S 会生成同名 .o，故 trap 汇编文件命名为 trap_entry.S |


**命名约定（避免 .o 冲突）**：同一目录下若同时有 `xxx.c` 和 `xxx.S`，编译后都会生成 `xxx.o`，链接时会产生重复定义或覆盖。因此 **trap 的汇编入口文件统一命名为 `trap_entry.S`**（生成 `trap_entry.o`），与 `trap.c` 的 `trap.o` 区分。bootloader 与 kernel/trap 均按此规则。

**建议实现顺序（减少一次通过难度）：**

1. **bootloader 侧**：`delegate_traps()`、`configure_pmp()`、`switch_to_s_mode()` 全放在 `bootloader/main.c`，并在 `main()` 里按顺序调用；确认能跳到 `kernel_main` 并打印一行。
2. **内核侧基础**：建 `kernel/main.c`（只做 `stvec` + 一句 `printf`）、`kernel/lib/riscv.h`、`include/sbi_call.h`、`include/memlayout.h`（初版：UART0_BASE、PLIC_BASE、CLINT_BASE）。
3. **S-mode trap 框架**：建 `kernel/trap/trap.h`、`trap_entry.S`（汇编入口，避免与 trap.c 的 trap.o 冲突）、`trap.c`，在 `kernel_main` 里设置 `stvec` 并开 SIE。
4. **PLIC + UART 中断**：建 `kernel/driver/plic.c/.h`（使用 memlayout.h），在 UART 里加 `uart_intr()`，在 `kernel/trap/irq.c` 里实现 `external_irq_handler()`。
5. **定时器**：在 M-mode 的 `trap.c` 的 case 7 里清 mtimecmp 并置位 `mip.STIP`，在 `sbi.c` 里实现 EID 0x00 写 mtimecmp；S-mode trap 里在 **case 5** 调 `timer_handler()`。

---

## 1. RISC-V 特权级架构

```
特权级     编码     用途
──────────────────────────────
 M-mode    11      固件/SBI（我们的 bootloader）
 S-mode    01      操作系统内核
 U-mode    00      用户程序
```

特权级从高到低：M > S > U。高特权级可以访问低特权级的资源，反之不行。

**你的 OS 架构**：

```
┌─────────────────────────┐
│ U-mode: 用户程序         │  ← ecall → 系统调用
├─────────────────────────┤
│ S-mode: 内核             │  ← ecall → SBI 调用
├─────────────────────────┤
│ M-mode: bootloader/SBI  │  ← 直接操作硬件
└─────────────────────────┘
```

---

## 2. 从 M-mode 切换到 S-mode

### 2.1 切换原理

RISC-V 没有"直接降级到 S-mode"的指令。切换方式是**利用 `mret` 指令**：

```
mret 的行为：
1. PC ← mepc          （跳转到 mepc 保存的地址）
2. 特权级 ← mstatus.MPP （恢复到 MPP 保存的特权级）
3. mstatus.MIE ← mstatus.MPIE
```

所以，要切换到 S-mode，你需要在执行 `mret` 之前：

1. 将 `mstatus.MPP` 设置为 `01`（S-mode）
2. 将 `mepc` 设置为 S-mode 的入口地址
3. 执行 `mret`

### 2.2 mstatus 寄存器详解

```
mstatus 寄存器的关键位域（RV64）：

位       名称      说明
─────────────────────────────────
[1]      SIE       S-mode 中断全局使能
[3]      MIE       M-mode 中断全局使能
[5]      SPIE      trap 前的 SIE 值
[7]      MPIE      trap 前的 MIE 值
[8]      SPP       trap 前的特权级（1位：0=U, 1=S）
[12:11]  MPP       trap 前的特权级（2位：00=U, 01=S, 11=M）
```

### 2.3 实现步骤

**📍 实现位置**：全部在 `**bootloader/main.c`** 里实现。

- `**switch_to_s_mode()`**：新建一个函数，函数内部按下面代码实现；不要在其他文件实现。
- **调用时机**：在 `main()` 里，在 `uart_init()`、`printf` 之后，先调用 `delegate_traps()` 和 `configure_pmp()`（见第 3 节），最后调用 `switch_to_s_mode()`，即：  
`main()` 流程 = `uart_init()` → 打印信息 → `delegate_traps()` → `configure_pmp()` → `switch_to_s_mode()`。

在 bootloader 的 `main.c` 中，完成所有 M-mode 初始化后，执行以下切换：

```c
void switch_to_s_mode(void) {
    // 1. 设置 mstatus.MPP = 01 (S-mode)
    unsigned long mstatus;
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus &= ~(3UL << 11);    // 清除 MPP 位
    mstatus |=  (1UL << 11);    // MPP = 01 (S-mode)
    asm volatile("csrw mstatus, %0" :: "r"(mstatus));

    // 2. 设置 mepc = S-mode 入口地址
    extern void kernel_main(void);  // 内核的入口函数
    asm volatile("csrw mepc, %0" :: "r"((unsigned long)kernel_main));

    // 3. 设置 S-mode 页表（暂时关闭分页）
    asm volatile("csrw satp, zero");

    // 4. 执行 mret，切换到 S-mode
    asm volatile("mret");
}
```

> **重要**：切换前还需要做委托设置和 PMP 配置（见下文）。

### 2.4 PMP（Physical Memory Protection）

**📍 实现位置**：在 `**bootloader/main.c`** 里新增函数 `**configure_pmp()`**，在 `main()` 里在调用 `switch_to_s_mode()` 之前调用一次（例如紧挨在 `delegate_traps()` 之后）。

在从 M 切换到 S 之前，需要配置 PMP 以允许 S-mode 访问内存。否则 S-mode 无法访问任何物理地址。

最简单的做法是开放所有地址：

```c
// 配置 PMP：允许 S-mode 访问所有地址空间
// pmpcfg0: 设置 entry 0 为 NAPOT 模式，R/W/X 全部允许
// pmpaddr0: 设置匹配所有地址（全 1）
asm volatile("csrw pmpaddr0, %0" :: "r"(0x3FFFFFFFFFFFFFULL));
asm volatile("csrw pmpcfg0, %0"  :: "r"(0x0F));  // NAPOT + RWX
```

> PMP 的 NAPOT（Naturally Aligned Power-Of-Two）模式：
> `pmpaddr` 全为 1 表示匹配所有地址。`pmpcfg` 低 3 位 = RWX，bit 3-4 = 模式（11=NAPOT）。
> 所以 `pmpcfg0 = 0x0F` 表示 NAPOT 模式 + 读/写/执行全部允许。
> 更准确地说是 `0x1F`（bit[4:3] = 11 = NAPOT），请查阅 RISC-V 特权规范确认你的理解。

---

## 3. 中断/异常委托

### 3.1 为什么需要委托

默认情况下，所有 trap 都由 M-mode 处理。但我们的内核运行在 S-mode，它需要自己处理大部分 trap（如系统调用、缺页等）。

通过 `medeleg`（异常委托）和 `mideleg`（中断委托），M-mode 可以将特定的 trap 直接交给 S-mode 处理，不需要先经过 M-mode。

### 3.2 委托配置

**📍 实现位置**：在 `**bootloader/main.c`** 里新增函数 `**delegate_traps(void)`**，函数体内写下面代码。在 `main()` 里在 `switch_to_s_mode()` 之前调用（例如在 `configure_pmp()` 之前或之后均可，只要在 `switch_to_s_mode()` 前即可）。

```c
void delegate_traps(void) {
    // 委托以下异常给 S-mode：
    unsigned long medeleg = 0;
    medeleg |= (1 << 0);   // Instruction address misaligned
    medeleg |= (1 << 1);   // Instruction access fault
    medeleg |= (1 << 2);   // Illegal instruction
    medeleg |= (1 << 3);   // Breakpoint
    medeleg |= (1 << 8);   // Environment call from U-mode
    medeleg |= (1 << 12);  // Instruction page fault
    medeleg |= (1 << 13);  // Load page fault
    medeleg |= (1 << 15);  // Store page fault
    asm volatile("csrw medeleg, %0" :: "r"(medeleg));

    // 委托以下中断给 S-mode：
    unsigned long mideleg = 0;
    mideleg |= (1 << 1);   // Supervisor software interrupt
    mideleg |= (1 << 5);   // Supervisor timer interrupt
    mideleg |= (1 << 9);   // Supervisor external interrupt
    asm volatile("csrw mideleg, %0" :: "r"(mideleg));
}
```

**关键理解**：

- `ecall from U-mode`（编号 8）委托给 S-mode → S-mode 内核处理系统调用
- `ecall from S-mode`（编号 9）**不要委托** → M-mode 处理 SBI 调用
- 定时器中断比较特殊：M-mode 定时器中断（编号 7）不能委托，S-mode 定时器中断（编号 5）可以委托
- 缺页异常委托给 S-mode → S-mode 实现虚拟内存

---

## 4. 定时器中断的完整流程

定时器中断是最棘手的部分，因为 CLINT 的 `mtimecmp` 只能在 M-mode 操作。

### 4.1 流程

**推荐做法**：M-mode 在 case 7 里置位 `**mip.STIP`**，让 S-mode 直接收到**定时器中断**（scause=5）。这样 S-mode 的 `s_trap_handler` 在 **case 5** 调 `timer_handler()` 即可，语义清晰，且不必在 S-mode 里手动清除挂起位。

```
1. S-mode 调用 sbi_set_timer(time) → ecall 到 M-mode
2. M-mode 的 SBI handler 写入 mtimecmp = time
3. 时间到达 → 触发 M-mode 定时器中断（mcause = 7）
4. M-mode trap handler:
   a. 清除 M-mode 定时器中断（写很大的值到 mtimecmp）
   b. 置位 mip.STIP（推荐），让 S-mode 收到“定时器中断”
5. S-mode 收到定时器中断（scause 对应 cause=5，Supervisor timer interrupt）
6. S-mode 在 case 5 中调用 timer_handler()
7. S-mode 调用 sbi_set_timer() 设置下次中断（回到步骤 1）
```

> **替代方案**：也可置位 `mip.SSIP`，S-mode 会收到软件中断（scause=1），在 **case 1** 里调 `timer_handler()` 并手动 `csrc sip, (1<<1)` 清除 SSIP。若采用 SSIP，后续若用软件中断做 IPI 等需注意与“定时器转发”区分。

### 4.2 M-mode 中触发 S-mode 定时器中断

**📍 实现位置**：在 `**bootloader/trap.c`** 的 `**m_trap_handler()`** 里，在 **中断** 分支（`is_interrupt == 1`）的 `**case 7:`**（M-mode 定时器中断）里添加下面两件事：

1. **清除 M-mode 定时器中断**：给 `mtimecmp` 写一个很大的值（例如全 1，或 `mtime + 一个很大的增量`），使 `mtime >= mtimecmp` 暂时不再成立，避免刚 `mret` 回 S-mode 又立刻触发 M-mode 定时器中断。
2. **通知 S-mode**：**推荐置位 `mip.STIP`（bit 5）**，让 S-mode 收到**定时器中断**（scause=5），在 S-mode 的 **case 5** 中调用 `timer_handler()` 即可，语义明确且无需在 S-mode 里清挂起位。若改为置位 `mip.SSIP`（bit 1），则 S-mode 收到软件中断（scause=1），需在 case 1 中处理并手动清除 SSIP。

**CLINT 地址**（与 02 文档一致）：`mtimecmp` 在 QEMU virt 上为 `0x0200_4000`，`mtime` 为 `0x0200_BFF8`。可在 `trap.c` 文件顶部用宏定义，或放在公共头文件里。

S-mode 在 **case 5** 调 `timer_handler()`，与 RISC-V 的“S-mode 定时器中断”语义一致，后续做调度、时钟等逻辑时代码更易读。

**完整示例（写入 `bootloader/trap.c` 的 case 7 中，推荐用 STIP）**：

```c
// bootloader/trap.c 顶部可增加（若尚未定义）：
#define CLINT_BASE       0x02000000UL
#define CLINT_MTIME      (*(volatile unsigned long *)(CLINT_BASE + 0xBFF8))
#define CLINT_MTIMECMP   (*(volatile unsigned long *)(CLINT_BASE + 0x4000))

// 在 m_trap_handler() 的 if (is_interrupt) 分支内，case 7: 中写入：

case 7:  // M-mode 定时器中断
{
    // 1. 清除定时器：写一个很大的值到 mtimecmp，避免立刻再次触发
    CLINT_MTIMECMP = 0xFFFFFFFFFFFFFFFFULL;
    // 或者延后很久再触发：CLINT_MTIMECMP = CLINT_MTIME + 0x100000000ULL;

    // 2. 通知 S-mode：推荐置位 STIP，S-mode 在 case 5 收到定时器中断并调用 timer_handler()
    {
        unsigned long mip;
        asm volatile("csrr %0, mip" : "=r"(mip));
        mip |= (1UL << 5);   // STIP，S-mode 将收到 scause 对应 cause=5
        asm volatile("csrw mip, %0" :: "r"(mip));
    }
    // 若改用 SSIP：mip |= (1UL << 1); 则 S-mode 在 case 1 中处理，并需手动 csrc sip, (1<<1)
    break;
}
```

**注意**：

- **STIP**：S-mode 的 `s_trap_handler` 在**中断**分支的 **case 5** 中调用 `timer_handler()` 即可，无需在 S-mode 里清除挂起位，且与 RISC-V 的“Supervisor timer interrupt”一致，后续调度、时钟逻辑更清晰。
- `mip` 在 M-mode 可写，用于“注入”给 S-mode 的中断；S-mode 通过 `sip` 看到被委托的位。

---

## 5. S-mode Trap 处理

### 5.1 S-mode 关键 CSR


| CSR        | 功能                   | 对应的 M-mode CSR |
| ---------- | -------------------- | -------------- |
| `sstatus`  | S-mode 状态（SIE、SPP 等） | `mstatus` 的子集  |
| `stvec`    | S-mode trap 入口地址     | `mtvec`        |
| `sepc`     | trap 时保存的 PC         | `mepc`         |
| `scause`   | trap 原因              | `mcause`       |
| `stval`    | trap 附加值             | `mtval`        |
| `sie`      | S-mode 中断使能          | `mie`          |
| `sip`      | S-mode 中断挂起          | `mip`          |
| `satp`     | 页表基地址 + 分页模式         | 无对应            |
| `sscratch` | 临时暂存寄存器              | `mscratch`     |


### 5.2 S-mode trap_context（`kernel/trap/trap.h`）

**📍 实现位置**：**新建文件 `kernel/trap/trap.h`**。该文件只放 S-mode 用的 `struct trap_context`（最后一格为 `sepc`）和 `void s_trap_handler(unsigned long scause, struct trap_context *ctx);` 声明。不要和 `bootloader/trap.h` 混用（两处 trap_context 名字可同，但一个用 mepc、一个用 sepc，且在不同目录）。

> **注意**：M-mode 有自己的 `bootloader/trap.h`，里面保存的是 `mepc`。
> S-mode 需要单独的头文件，字段用 `sepc`，同时增加一些内核需要的额外字段。
> 两个结构体名字相同但在不同的头文件中，不会在同一个编译单元里同时被 include。

```c
// kernel/trap/trap.h

#ifndef __KERNEL_TRAP_H__
#define __KERNEL_TRAP_H__

struct trap_context {
    unsigned long ra;    // x1  offset 0
    unsigned long sp;    // x2  offset 8
    unsigned long gp;    // x3  offset 16
    unsigned long tp;    // x4  offset 24
    unsigned long t0;    // x5  offset 32
    unsigned long t1;    // x6  offset 40
    unsigned long t2;    // x7  offset 48
    unsigned long s0;    // x8  offset 56
    unsigned long s1;    // x9  offset 64
    unsigned long a0;    // x10 offset 72
    unsigned long a1;    // x11 offset 80
    unsigned long a2;    // x12 offset 88
    unsigned long a3;    // x13 offset 96
    unsigned long a4;    // x14 offset 104
    unsigned long a5;    // x15 offset 112
    unsigned long a6;    // x16 offset 120
    unsigned long a7;    // x17 offset 128
    unsigned long s2;    // x18 offset 136
    unsigned long s3;    // x19 offset 144
    unsigned long s4;    // x20 offset 152
    unsigned long s5;    // x21 offset 160
    unsigned long s6;    // x22 offset 168
    unsigned long s7;    // x23 offset 176
    unsigned long s8;    // x24 offset 184
    unsigned long s9;    // x25 offset 192
    unsigned long s10;   // x26 offset 200
    unsigned long s11;   // x27 offset 208
    unsigned long t3;    // x28 offset 216
    unsigned long t4;    // x29 offset 224
    unsigned long t5;    // x30 offset 232
    unsigned long t6;    // x31 offset 240
    unsigned long sepc;  //      offset 248（S-mode 用 sepc，不是 mepc）
};

void s_trap_handler(unsigned long scause, struct trap_context *ctx);

#endif /* __KERNEL_TRAP_H__ */
```

### 5.3 S-mode trap 入口（`kernel/trap/trap_entry.S`）

**📍 实现位置**：**新建文件 `kernel/trap/trap_entry.S`**。该文件实现全局符号 `**s_trap_entry**`（`.global s_trap_entry`），内部：保存 31 个通用寄存器 + sepc 到栈、用 `scause` 和 `sp` 调 `s_trap_handler`、恢复寄存器、`sret`。寄存器布局和偏移必须和 `kernel/trap/trap.h` 里的 `struct trap_context` 一致。Makefile 需把 `kernel/trap/trap_entry.S` 加入 `SRCS_ASM` 参与编译链接。**命名用 trap_entry.S 避免与同目录 trap.c 编译后都生成 trap.o 产生冲突。**

结构与 M-mode 类似，但使用 S-mode CSR：

```asm
# kernel/trap/trap_entry.S

.global s_trap_entry
.align 4

s_trap_entry:
    # 在栈上分配 256 字节，布局和 kernel/trap/trap.h 的 trap_context 一致
    addi sp, sp, -256
    sd ra,    0(sp)
    sd sp,    8(sp)
    sd gp,   16(sp)
    sd tp,   24(sp)
    sd t0,   32(sp)
    sd t1,   40(sp)
    sd t2,   48(sp)
    sd s0,   56(sp)
    sd s1,   64(sp)
    sd a0,   72(sp)
    sd a1,   80(sp)
    sd a2,   88(sp)
    sd a3,   96(sp)
    sd a4,  104(sp)
    sd a5,  112(sp)
    sd a6,  120(sp)
    sd a7,  128(sp)
    sd s2,  136(sp)
    sd s3,  144(sp)
    sd s4,  152(sp)
    sd s5,  160(sp)
    sd s6,  168(sp)
    sd s7,  176(sp)
    sd s8,  184(sp)
    sd s9,  192(sp)
    sd s10, 200(sp)
    sd s11, 208(sp)
    sd t3,  216(sp)
    sd t4,  224(sp)
    sd t5,  232(sp)
    sd t6,  240(sp)

    # 保存 sepc
    csrr t0, sepc
    sd   t0, 248(sp)

    csrr a0, scause    # 第一个参数：scause
    mv   a1, sp        # 第二个参数：trap_context 指针
    call s_trap_handler

    # 从 ctx 恢复 sepc（C 代码可能修改了它，如 ecall 后 +4）
    ld   t0, 248(sp)
    csrw sepc, t0

    ld ra,    0(sp)
    ld gp,   16(sp)
    ld tp,   24(sp)
    ld t0,   32(sp)
    ld t1,   40(sp)
    ld t2,   48(sp)
    ld s0,   56(sp)
    ld s1,   64(sp)
    ld a0,   72(sp)
    ld a1,   80(sp)
    ld a2,   88(sp)
    ld a3,   96(sp)
    ld a4,  104(sp)
    ld a5,  112(sp)
    ld a6,  120(sp)
    ld a7,  128(sp)
    ld s2,  136(sp)
    ld s3,  144(sp)
    ld s4,  152(sp)
    ld s5,  160(sp)
    ld s6,  168(sp)
    ld s7,  176(sp)
    ld s8,  184(sp)
    ld s9,  192(sp)
    ld s10, 200(sp)
    ld s11, 208(sp)
    ld t3,  216(sp)
    ld t4,  224(sp)
    ld t5,  232(sp)
    ld t6,  240(sp)
    addi sp, sp, 256

    sret
```

### 5.4 sscratch 的用途

`sscratch` 是一个非常重要的寄存器。当从 U-mode 陷入 S-mode 时，内核需要切换到内核栈，但此时所有通用寄存器都保存着用户程序的值，没有空闲寄存器可用。

**解决方案**：

1. `sscratch` 在 U-mode 运行时保存内核栈指针
2. trap 发生时，使用 `csrrw sp, sscratch, sp` 交换 `sp` 和 `sscratch`
3. 现在 `sp` = 内核栈，`sscratch` = 用户栈

```asm
s_trap_entry:
    csrrw sp, sscratch, sp
    # 现在 sp = 内核栈指针（之前存在 sscratch 中）
    # sscratch = 用户栈指针

    # 但如果从 S-mode 自身 trap 呢？此时 sscratch = 0
    # 需要判断：如果 sscratch == 0，说明从 S-mode 来，恢复原来的 sp
    # 如果 sscratch != 0，说明从 U-mode 来，使用新的 sp
```

> 这个技巧在后续实现用户态切换时非常关键，现在先有概念即可。

### 5.5 S-mode trap 处理（`kernel/trap/trap.c`）

**📍 实现位置**：**新建文件 `kernel/trap/trap.c`**。该文件实现 `**s_trap_handler(unsigned long scause, struct trap_context *ctx)**`，根据 `scause` 最高位区分中断/异常，再按 cause 编号 switch：中断里处理 1（软件）、5（定时器）、9（外部），异常里处理 8（ecall）、12/13/15（缺页），其余可打印并 `panic`。其中 `timer_handler`（`kernel/trap/timer.c`）、`external_irq_handler`（`kernel/trap/irq.c`）、`page_fault_handler`（`kernel/trap/exception.c`）、`syscall_handler`（`kernel/trap/syscall.c`）在各自文件实现（见第 10 节），这里只需通过 `trap.h` 声明后调用。

```c
// kernel/trap/trap.c

#include "trap.h"
#include "../lib/printf.h"

void s_trap_handler(unsigned long scause, struct trap_context *ctx) {
    unsigned long sepc = ctx->sepc;
    int is_interrupt = (scause >> 63) & 1;
    unsigned long cause = scause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (cause) {
        case 1:   // Supervisor software interrupt（IPI 等，暂不处理）
            asm volatile("csrc sip, %0" :: "r"(1 << 1));
            break;
        case 5:   // Supervisor timer interrupt（M-mode 置位 STIP 后触发）
            timer_handler();
            break;
        case 9:   // Supervisor external interrupt
            external_irq_handler();
            break;
        default:
            printf("Unknown S-mode interrupt: %d\n", cause);
            break;
        }
    } else {
        switch (cause) {
        case 8:   // Environment call from U-mode
            // 系统调用处理（后续阶段实现）
            ctx->sepc += 4;  // 跳过 ecall 指令
            syscall_handler(ctx);
            break;
        case 12:  // Instruction page fault
        case 13:  // Load page fault
        case 15:  // Store page fault
            page_fault_handler(scause, sepc, ctx);
            break;
        default:
            printf("S-mode exception: cause=%d, sepc=0x%x\n", cause, sepc);
            panic("unhandled exception");
        }
    }
}
```

---

## 6. 内核入口

**📍 实现位置**：**新建文件 `kernel/main.c`**。该文件提供 `**kernel_main(void)**`，作为从 M-mode `mret` 后进入 S-mode 的入口（bootloader 里 `mepc` 设为 `kernel_main`）。在此函数内：先设置 `stvec = s_trap_entry`，再初始化 UART、PLIC，打开 S-mode 中断（`sie`、`sstatus.SIE`），然后 `printf` 打印，可选设置第一次定时器。Makefile 需把 `kernel/main.c` 加入编译，且链接时保证 `kernel_main` 被正确链接进最终内核镜像。

```c
// kernel/main.c

#include "../include/sbi_call.h"  // SBI 调用封装（或 "sbi_call.h" 配合 -I include）

// 基于 SBI 的 putchar（S-mode 不直接操作 UART）
void kputc(char c) {
    sbi_console_putchar(c);
}

void kputs(const char *s) {
    while (*s) kputc(*s++);
}

void kernel_main(void) {
    // 此时已经在 S-mode 运行

    // 1. 设置 S-mode trap
    extern void s_trap_entry(void);
    asm volatile("csrw stvec, %0" :: "r"((unsigned long)s_trap_entry));

    // 2. 使能中断
    // sie: 使能 S-mode 的软件中断、定时器中断、外部中断
    asm volatile("csrw sie, %0" :: "r"((1 << 1) | (1 << 5) | (1 << 9)));
    // sstatus.SIE = 1: 全局使能 S-mode 中断
    asm volatile("csrs sstatus, %0" :: "r"(1 << 1));

    kputs("Hello from S-mode kernel!\n");

    // 3. 设置第一次定时器中断
    // sbi_set_timer(current_time + interval);

    while (1) {}
}
```

---

## 7. riscv.h 工具宏

**📍 实现位置**：**新建文件 `kernel/lib/riscv.h`**。该文件只放 CSR 读写宏和特权级/中断相关常量，供 `kernel/trap/trap.c`、`kernel/main.c`、`kernel/lib/printf.c` 等 include。在 Makefile 中若有 `-I kernel` 或 `-I kernel/lib`，其他内核文件可用 `#include "lib/riscv.h"` 或 `#include "riscv.h"` 引用。

为了减少到处写内联汇编，建议创建一个 `riscv.h`，封装常用的 CSR 操作：

```c
// kernel/lib/riscv.h

#ifndef __RISCV_H__
#define __RISCV_H__

// 读取 CSR
#define csrr(csr) ({ \
    unsigned long __val; \
    asm volatile("csrr %0, " #csr : "=r"(__val)); \
    __val; \
})

// 写入 CSR
#define csrw(csr, val) \
    asm volatile("csrw " #csr ", %0" :: "r"((unsigned long)(val)))

// CSR 置位（set bits）
#define csrs(csr, val) \
    asm volatile("csrs " #csr ", %0" :: "r"((unsigned long)(val)))

// CSR 清位（clear bits）
#define csrc(csr, val) \
    asm volatile("csrc " #csr ", %0" :: "r"((unsigned long)(val)))

// 特权级相关常量
#define MSTATUS_MPP_MASK   (3UL << 11)
#define MSTATUS_MPP_M      (3UL << 11)
#define MSTATUS_MPP_S      (1UL << 11)
#define MSTATUS_MPP_U      (0UL << 11)
#define MSTATUS_MIE        (1UL << 3)
#define MSTATUS_MPIE       (1UL << 7)

#define SSTATUS_SPP        (1UL << 8)
#define SSTATUS_SIE        (1UL << 1)
#define SSTATUS_SPIE       (1UL << 5)

// 中断使能位
#define SIE_SSIE   (1UL << 1)   // S-mode software interrupt
#define SIE_STIE   (1UL << 5)   // S-mode timer interrupt
#define SIE_SEIE   (1UL << 9)   // S-mode external interrupt

#endif
```

---

## 8. 内核基础库（kernel/lib/）

**📍 实现位置**：以下文件均在 `**kernel/lib/`** 目录下新建或修改，并在 Makefile 的 `SRCS_C` 中加入对应 `.c` 文件，保证链接进内核。

进入 S-mode 后，内核需要一套自己的基础库。这些文件是后续所有模块的基础。

### 8.1 types.h — 基本类型定义

**文件**：`kernel/lib/types.h`（新建）。其他内核代码通过 `#include "lib/types.h"` 或 `#include "types.h"` 使用。

```c
// kernel/lib/types.h

#ifndef __TYPES_H__
#define __TYPES_H__

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long       uint64_t;

typedef char                int8_t;
typedef short               int16_t;
typedef int                 int32_t;
typedef long                int64_t;

typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef int64_t             ptrdiff_t;

typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;

#define NULL ((void *)0)
#define true  1
#define false 0
typedef int bool;

#endif
```

> 也可以直接用编译器内置的 `<stdint.h>`（裸机环境可用），再加上自己的补充定义。

### 8.2 string.c / string.h — 内存和字符串操作

**文件**：`kernel/lib/string.h`（新建，声明）、`kernel/lib/string.c`（新建，实现）。Makefile 中把 `kernel/lib/string.c` 加入 `SRCS_C`。

后续的 pmm（页帧清零）、页表操作、进程创建等都依赖这些函数。

```c
// kernel/lib/string.h

#ifndef __STRING_H__
#define __STRING_H__

#include "types.h"

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
int   memcmp(const void *s1, const void *s2, size_t n);
int   strcmp(const char *s1, const char *s2);
int   strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
size_t strlen(const char *s);

#endif
```

```c
// kernel/lib/string.c

#include "string.h"

void *memset(void *dst, int c, size_t n) {
    char *d = (char *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

// strlen, strcmp, strncpy 等请自行实现，逻辑类似
```

### 8.3 printf.c / printf.h — 内核格式化输出

**文件**：`kernel/lib/printf.c`、`kernel/lib/printf.h`（阶段 1 已有）。底层通过 SBI 调用 (`sbi_console_putchar`) 输出字符，S-mode 下通过 `ecall` 陷入 M-mode 完成实际 UART 操作。函数名保持 `**printf`**，不改名为 `kprintf`。所有内核代码统一用 `printf` 和 `panic`。

阶段 1 已在 `kernel/lib/printf.c` 中实现了完整的 `printf`（支持 `%d`、`%x`、`%p`、`%s`、`%c` 等），底层通过 SBI 调用 (`sbi_console_putchar`) 输出字符。这是标准做法：S-mode 内核通过 `ecall` 请求 M-mode 的 SBI 服务来完成 UART 输出，**本阶段无需修改 `printf.c` 的输出方式**。

```c
// kernel/lib/printf.h（已有）

extern int printf(const char *s, ...);
extern void panic(const char *s, ...);
```

> `printf` 和 `panic` 均在 `printf.c` 中实现，所有内核代码统一通过 `#include "printf.h"` 使用。

### 8.4 panic — 内核崩溃处理

**文件**：直接集成在 `**kernel/lib/printf.c`** 中，不再单独建 `panic.c`。你的 `printf.c` 末尾已有 `panic()` 实现，在 `printf.h` 中声明即可。`kernel/trap/trap.c`、`exception.c` 等通过 `#include "printf.h"` 调用 `panic(...)`。

**推荐实现**（在 `kernel/lib/printf.c` 末尾，需在文件顶部 `#include "riscv.h"`）：

```c
// kernel/lib/printf.c 末尾

static int panicked = 0;

void panic(const char *s, ...) {
    // 关闭 S-mode 全局中断，防止 panic 过程中被打断
    csrc(sstatus, SSTATUS_SIE);

    // 防止多核或中断重入导致 panic 嵌套
    if (panicked) {
        while (1) {}
    }
    panicked = 1;

    printf("\n!!! KERNEL PANIC !!!\n");

    // 支持格式化参数，例如 panic("page fault at %p", addr)
    va_list vl;
    va_start(vl, s);
    _vprintf(s, vl);
    va_end(vl);
    printf("\n");

    // 打印关键 CSR，帮助定位问题
    printf("  sepc   = 0x%lx\n", csrr(sepc));
    printf("  scause = 0x%lx\n", csrr(scause));
    printf("  stval  = 0x%lx\n", csrr(stval));

    while (1) {}
}
```

```c
// kernel/lib/printf.h 中声明：

extern int printf(const char *s, ...);
extern void panic(const char *s, ...);
```

**相比简单版本的改进**：


| 改进                           | 说明                                                                                        |
| ---------------------------- | ----------------------------------------------------------------------------------------- |
| `csrc(sstatus, SSTATUS_SIE)` | 关中断，避免 panic 过程中被定时器/外部中断打断导致输出混乱                                                         |
| `panicked` 标志                | 防止 panic 嵌套（例如 panic 里的 printf 又触发异常再次 panic）                                             |
| `_vprintf(s, vl)`            | 直接用内部的 `_vprintf` 支持格式化参数，`panic("fault at %p", addr)` 这种写法才有效。你当前版本用 `printf(s)` 会忽略可变参数 |
| 打印 `sepc`/`scause`/`stval`   | 出问题时能直接看到：在哪条指令（sepc）、什么原因（scause）、相关地址（stval）                                            |


---

## 9. S-mode 设备驱动

**📍 实现位置**：本节所有代码在 `**kernel/driver/`** 目录下。PLIC 为新建文件；UART 在阶段 1 已有，本阶段在其上**修改**。

### 9.1 PLIC 驱动（kernel/driver/plic.c, plic.h）

**文件**：**新建 `kernel/driver/plic.h`**（声明）、**新建 `kernel/driver/plic.c`**（实现）。在 `kernel/main.c` 里先调 `plic_init()`，再调 `plic_init_hart()`；在 S-mode 外部中断处理（见 10.1）里用 `plic_claim()` / `plic_complete()`。Makefile 将 `kernel/driver/plic.c` 加入 `SRCS_C`。

PLIC（Platform-Level Interrupt Controller）管理所有外部中断源。S-mode 需要配置 PLIC 才能接收外部设备（如 UART）的中断。

```c
// kernel/driver/plic.h

#ifndef __PLIC_H__
#define __PLIC_H__

void plic_init(void);
void plic_init_hart(void);
int  plic_claim(void);       // 获取当前待处理的中断号
void plic_complete(int irq);  // 通知 PLIC 中断处理完成

#endif
```

**PLIC 寄存器布局**（QEMU virt 平台，基址 `0x0C000000`）：


| 地址                              | 功能                    |
| ------------------------------- | --------------------- |
| `base + irq*4`                  | 中断源优先级（每个源一个 32 位寄存器） |
| `base + 0x2000 + hart*0x80`     | 中断使能位图（每个 hart 独立）    |
| `base + 0x200000 + hart*0x1000` | 优先级阈值                 |
| `base + 0x200004 + hart*0x1000` | claim/complete 寄存器    |


> **注意**：S-mode 的 hart context 编号 = hart_id * 2 + 1（context 0 是 M-mode）。

**前置**：在实现 PLIC 之前，先**新建 `include/memlayout.h`**（阶段 3 初版），放入 MMIO 地址常量，供 `plic.c`、`uart.c` 等使用。阶段 4 会扩展该文件。

```c
// include/memlayout.h（阶段 3 初版，新建）

#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

#define UART0_BASE  0x10000000UL
#define PLIC_BASE   0x0C000000UL
#define CLINT_BASE  0x02000000UL

#endif
```

```c
// kernel/driver/plic.c

#include <stdint.h>
#include "plic.h"
#include "memlayout.h"   // 需 CFLAGS += -I include

#define PLIC_PRIORITY(irq)       (PLIC_BASE + (irq) * 4)
#define PLIC_SENABLE(hart)       (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart)     (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)        (PLIC_BASE + 0x201004 + (hart) * 0x2000)

#define UART0_IRQ  10

void plic_init(void) {
    // 设置 UART0 中断的优先级（非零即可）
    *(volatile uint32_t *)PLIC_PRIORITY(UART0_IRQ) = 1;
}

void plic_init_hart(void) {
    int hart = 0;  // 单核简化
    // 为当前 hart 使能 UART0 中断
    *(volatile uint32_t *)PLIC_SENABLE(hart) = (1 << UART0_IRQ);
    // 设置优先级阈值为 0（接受所有优先级 > 0 的中断）
    *(volatile uint32_t *)PLIC_SPRIORITY(hart) = 0;
}

int plic_claim(void) {
    int hart = 0;
    return *(volatile uint32_t *)PLIC_SCLAIM(hart);
}

void plic_complete(int irq) {
    int hart = 0;
    *(volatile uint32_t *)PLIC_SCLAIM(hart) = irq;
}
```

### 9.2 扩展 UART 驱动（kernel/driver/uart.c, uart.h）

**文件**：**修改** 阶段 1 已有的 `**kernel/driver/uart.c`** 和 `**kernel/driver/uart.h`**。在 `uart.h` 中声明 `**void uart_intr(void);**`；在 `uart.c` 中实现 `uart_intr()`（内部可循环读字符并处理或放入缓冲区），并在 `uart_init()` 里使能 UART 接收中断（如 IER 相应位）。`uart_intr()` 由 `kernel/trap/irq.c` 的 `external_irq_handler()` 在 UART 中断时调用。

你在阶段 1 已经在 `kernel/driver/` 中实现了基本的 UART 驱动（`uart_init`、`uart_putc`、`uart_getc`）。现在需要添加**中断驱动的输入**支持，以便 S-mode 能通过 PLIC 接收 UART 中断。

需要扩展的内容：

1. 在 `uart_init()` 中使能接收中断：`IER = 0x01`
2. 添加 `uart_intr()` 中断处理函数

```c
// kernel/driver/uart.h — 在已有声明基础上添加

void uart_intr(void);   // UART 中断处理（新增）
```

```c
// kernel/driver/uart.c — 在已有代码基础上添加

void uart_intr(void) {
    int c;
    while ((c = uart_getc()) != -1) {
        // 将字符放入输入缓冲区（可以是一个环形 buffer）
        // 后续可以唤醒等待输入的进程
        // 简单实现：直接回显
        uart_putc((char)c);
    }
}
```

---

## 10. S-mode Trap 子模块

**📍 实现位置**：以下四个模块均在 `**kernel/trap/`** 目录下，各自一个 C 文件，在 Makefile 的 `SRCS_C` 中加入对应 `.c`。第 5 节的 `s_trap_handler` 会调用这些函数，需在 `kernel/trap/trap.h` 里统一声明后调用。

在第 5 节中，`trap.c` 的 `s_trap_handler` 调用了 `timer_handler()`、`external_irq_handler()`、`page_fault_handler()` 和 `syscall_handler()`。现在为它们创建独立的文件。

### 10.1 timer.c — 定时器中断处理

**文件**：**新建 `kernel/trap/timer.c`**，实现 `**void timer_handler(void)**`。本阶段只需打印一行信息即可，具体的定时器调度逻辑（调用 `sbi_set_timer()` 设置下次中断 + `yield()` 触发进程切换）将在**阶段 5（进程管理）**中完善。

```c
// kernel/trap/timer.c（阶段 3 桩实现）

#include "../lib/printf.h"

void timer_handler(void) {
    printf("Timer interrupt!\n");
}
```

> 阶段 5 中，此函数将扩展为：维护全局 `ticks` 计数、调用 `sbi_set_timer()` 设置下次定时器中断、调用 `yield()` 触发调度。届时还会新建 `timer_init()` 来设置第一次定时器中断。

### 10.2 irq.c — 外部中断处理

**文件**：**新建 `kernel/trap/irq.c`**，实现 `**void external_irq_handler(void)**`：内部调 `plic_claim()` 得到中断号，根据中断号 switch（如 UART0_IRQ 调 `uart_intr()`），最后对非 0 的中断号调 `plic_complete(irq)`。需 include `plic.h`、`uart.h` 等。

```c
// kernel/trap/irq.c

#include "plic.h"
#include "uart.h"
#include "printf.h"

#define UART0_IRQ  10

void external_irq_handler(void) {
    int irq = plic_claim();

    switch (irq) {
    case UART0_IRQ:
        uart_intr();
        break;
    case 0:
        // irq=0 表示没有中断待处理（spurious）
        break;
    default:
        printf("Unexpected external interrupt: irq=%d\n", irq);
        break;
    }

    if (irq)
        plic_complete(irq);
}
```

### 10.3 exception.c — 异常处理

**文件**：**新建 `kernel/trap/exception.c`**，实现 `**void page_fault_handler(unsigned long scause, unsigned long sepc, void *ctx)**`（或参数类型与 `trap.c` 里调用处一致）。内部可读 `stval` 得到 fault 地址，打印信息后调用 `panic("page fault")`。其他异常若在 trap.c 里未单独处理，也会走到 default 分支，可同样用 `panic`。Makefile 将 `kernel/trap/exception.c` 加入 `SRCS_C`。

```c
// kernel/trap/exception.c

#include "printf.h"
#include "riscv.h"

void page_fault_handler(unsigned long scause, unsigned long sepc,
                        void *ctx) {
    unsigned long stval = csrr(stval);  // 触发缺页的虚拟地址
    printf("Page fault! cause=%d, sepc=0x%p, addr=0x%p\n",
            scause, sepc, stval);
    // 后续阶段实现：按需分配物理页、映射页表
    // 当前阶段：直接 panic
    panic("page fault");
}
```

### 10.4 syscall.c — 系统调用处理

**文件**：**新建 `kernel/trap/syscall.c`**，实现 `**void syscall_handler(struct trap_context *ctx)**` 桩函数，内部直接 `panic("syscall not implemented");` 即可。在 `kernel/trap/trap.c` 的 case 8（U-mode ecall）里调用 `syscall_handler(ctx)` 并 `ctx->sepc += 4`。Makefile 将 `kernel/trap/syscall.c` 加入 `SRCS_C`。

> `syscall_handler()` 的完整实现将在后续阶段（如阶段 6）完成。
> 本阶段可以先写一个桩函数：
>
> ```c
> // kernel/trap/syscall.c（桩）
> void syscall_handler(void *ctx) { panic("syscall not implemented"); }
> ```

---

## 11. 更新内核入口

**📍 实现位置**：在 `**kernel/main.c`** 的 `**kernel_main()**` 里，按顺序完成：设置 `stvec`、UART 初始化、`plic_init()` 与 `plic_init_hart()`、设置 `sie` 和 `sstatus.SIE`、`printf` 打印、可选设置第一次定时器（需先实现 SBI set_timer 和 `timer_handler`）。SBI 的 set_timer 在 `**bootloader/sbi.c**` 的 `**sbi_handler()**` 里增加 **EID 0x00** 分支，根据参数写 CLINT 的 `mtimecmp` 寄存器。

把上面新增的初始化整合到 `kernel_main` 中：

```c
// kernel/main.c

#include "lib/printf.h"
#include "lib/riscv.h"
#include "driver/uart.h"
#include "driver/plic.h"
#include "../include/sbi_call.h"

void kernel_main(void) {
    // 1. 设置 S-mode trap
    extern void s_trap_entry(void);
    csrw(stvec, (unsigned long)s_trap_entry);

    // 2. 初始化设备驱动
    uart_init();
    plic_init();
    plic_init_hart();

    // 3. 使能中断
    csrw(sie, SIE_SSIE | SIE_STIE | SIE_SEIE);
    csrs(sstatus, SSTATUS_SIE);

    printf("Hello from S-mode kernel!\n");

    // 4. 功能测试（见"功能测试"章节）
    // ...

    while (1) {}
}
```

---

## 12. 本阶段的完整切换流程总结

```
entry.S
  │
  ├─ 设置栈、清零 BSS
  ├─ 设置 mtvec
  ▼
bootloader/main.c
  │
  ├─ uart_init()
  ├─ uart_puts("M-mode init...\n")
  ├─ delegate_traps()         ← 设置 medeleg/mideleg
  ├─ configure_pmp()          ← 配置 PMP 允许 S-mode 访问内存
  ├─ set mstatus.MPP = S-mode
  ├─ set mepc = kernel_main
  ├─ satp = 0 (关闭分页)
  ├─ mret                     ← 切换到 S-mode！
  ▼
kernel/main.c :: kernel_main()
  │
  ├─ 设置 stvec
  ├─ uart_init()              ← S-mode UART 初始化
  ├─ plic_init() + plic_init_hart()
  ├─ 使能 S-mode 中断
  ├─ printf("Hello from S-mode!\n")
  └─ 主循环
```

---

## 本阶段 Makefile 与 SBI 修改位置

**Makefile**：在项目根目录的 `**Makefile`** 中：

- 在 `**SRCS_ASM`** 里加入 `**kernel/trap/trap_entry.S**`（与 trap.c 同目录，故用 trap_entry.S 避免都生成 trap.o）。M-mode 的 trap 汇编若在阶段 2 命名为 `bootloader/trap_entry.S`，则保持不改。
- 在 `**SRCS_C**` 里加入：`kernel/main.c`、`kernel/trap/trap.c`、`kernel/trap/irq.c`、`kernel/trap/exception.c`、`kernel/trap/syscall.c`、`kernel/driver/plic.c`、`kernel/lib/string.c`。`panic()` 已集成在 `kernel/lib/printf.c` 中，不需要单独的 `panic.c`。若内核与 bootloader 共用或分开编译，需保证 kernel 的 .c 只编译一次且链接进同一 elf。
- 若内核代码需要 `#include "sbi_call.h"` 等，在 `**CFLAGS**` 中加 `**-I include**`（或你的 include 目录）。

**SBI 定时器（M-mode）**：在 `**bootloader/sbi.c`** 的 `**sbi_handler(struct trap_context *ctx)`** 里，在现有 `switch(eid)` 中增加 `**case 0x00:**`（EID 0x00 为 set_timer）：从 `ctx->a0` 取出目标时间，写入 CLINT 的 `mtimecmp` 寄存器（例如 `CLINT_BASE + 0x4000`）。这样 S-mode 调用 `sbi_set_timer(time)` 时才会生效。

**timer_handler（S-mode）**：若采用推荐的 STIP 方案，在 `**kernel/trap/trap.c`** 的 `s_trap_handler` 里，**中断**分支的 **case 5** 调用 `**timer_handler()`**。该函数实现在 `**kernel/trap/timer.c**` 中，本阶段只需打印一行即可；具体逻辑（设置下次定时器中断 `sbi_set_timer()` + 进程调度 `yield()`）将在**阶段 5（进程管理）**中完善。Makefile 需将 `kernel/trap/timer.c` 加入 `SRCS_C`。

---

## 本阶段新增文件清单

```
kernel/
├── main.c                     # 内核入口（S-mode 入口函数 kernel_main）
├── trap/
│   ├── trap.h                 # S-mode trap_context 结构体（sepc，区别于 bootloader/trap.h）
│   ├── trap_entry.S           # S-mode trap 汇编入口（避免与 trap.c 的 trap.o 冲突）
│   ├── trap.c                 # trap 分发
│   ├── irq.c                  # 外部中断处理
│   ├── exception.c            # 异常处理
│   ├── syscall.c              # 系统调用（桩函数）
│   └── timer.c                # 定时器中断处理（桩，阶段 5 扩展）
├── driver/
│   ├── uart.c                 # UART 驱动（阶段 1 已创建，本阶段扩展 uart_intr）
│   ├── uart.h
│   ├── plic.c                 # PLIC 驱动（新增）
│   └── plic.h
└── lib/
    ├── types.h                # 基本类型定义
    ├── string.c               # 内存/字符串操作
    ├── string.h
    ├── printf.c               # 内核 printf + panic（panic 集成在此文件中）
    ├── printf.h
    └── riscv.h                # CSR 操作宏

include/
├── sbi_call.h                 # SBI 调用封装（阶段 2 已定义，本阶段开始使用）
└── memlayout.h                # 内存布局常量（本阶段新建初版：UART0_BASE、PLIC_BASE、CLINT_BASE）
```

---

## 本阶段 Makefile 更新

```makefile
# 在 CFLAGS 中添加头文件搜索路径
CFLAGS += -I include         # 让 #include "sbi_call.h" 等全局头文件能被找到

# 新增源文件
SRCS_ASM = bootloader/entry.S \
           bootloader/trap_entry.S \   # 与 trap.c 不同名，避免 trap.o 冲突
           kernel/trap/trap_entry.S    # ← 新增，同上

SRCS_C = bootloader/main.c \
         bootloader/trap.c \
         bootloader/sbi.c \
         kernel/main.c \         # ← 新增：S-mode 内核入口
         kernel/trap/trap.c \    # ← 新增
         kernel/trap/irq.c \     # ← 新增
         kernel/trap/exception.c \ # ← 新增
         kernel/trap/syscall.c \ # ← 新增（桩函数）
         kernel/driver/uart.c \
         kernel/driver/plic.c \  # ← 新增
         kernel/lib/printf.c \   # panic() 已集成在此文件中
         kernel/lib/string.c    # ← 新增
```

> **提示**：`bootloader/main.c` 在本阶段也需要更新，添加 `delegate_traps()`、`configure_pmp()` 和切换到 S-mode 的代码。

---

## 功能测试

本阶段实现完成后，应当在 `kernel/main.c` 的 `kernel_main()` 中添加测试代码，逐项验证所有功能是否正常工作。测试分为多个阶段，建议逐个打开验证，避免一次性出错难以定位。

### 前置：补全 SBI set_timer

定时器测试需要 M-mode 的 `sbi_handler` 支持 EID 0x00（set_timer），先确认 `bootloader/sbi.c` 中已添加：

```c
// bootloader/sbi.c — 在 switch(eid) 中添加

case 0x00: { // sbi_set_timer
    unsigned long stime = ctx->a0;
    // 写入 mtimecmp，设置下一次定时器中断时间
    *(volatile unsigned long *)(0x2000000UL + 0x4000) = stime;
    // 清除 STIP，避免 S-mode 立刻再次触发旧的定时器中断
    asm volatile("csrc mip, %0" : : "r"(1UL << 5));
    break;
}
```

### 测试 1：S-mode 基本启动 + SBI 输出

**验证内容**：M→S 切换成功、SBI console_putchar 可用、printf 工作正常。

**预期输出**：

```
M-mode bootloader started.
Hello from S-mode kernel!
```

如果只看到第一行没看到第二行，说明 `mret` 切换或 S-mode 初始化有问题。如果两行都看不到，说明 M-mode 的 UART 初始化或 `entry.S` 有问题。

### 测试 2：定时器中断

**验证内容**：SBI set_timer → M-mode 定时器中断（case 7）→ 置位 STIP → S-mode 定时器中断（case 5）→ `timer_handler()` 被调用。

在 `kernel_main()` 里，`printf` 之后添加：

```c
// 测试定时器中断
static unsigned long rdtime(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

printf("Setting timer...\n");
sbi_set_timer(rdtime() + 10000000);  // 约 1 秒后触发
```

**预期输出**：

```
Hello from S-mode kernel!
Setting timer...
Timer interrupt!
```

如果 `Timer interrupt!` 没出现：

- 检查 `sbi.c` 是否实现了 `case 0x00`
- 检查 `bootloader/trap.c` 的 `case 7` 是否正确置位 `mip.STIP`
- 检查 `sie` 是否使能了 `SIE_STIE`（bit 5）
- 检查 `sstatus.SIE` 是否已开启

> 注意：定时器只会触发一次，因为 `timer_handler()` 当前只是打印，没有设置下一次定时器。这是正确的，阶段 5 才补充循环定时。

### 测试 3：UART 外部中断（键盘输入回显）

**验证内容**：PLIC 初始化正确、UART 接收中断使能、外部中断通路 → `s_trap_handler` case 9 → `external_irq_handler` → `uart_intr` → 回显字符。

**操作**：启动 QEMU 后，在终端里**按几个键**（如 `abc`）。

**预期输出**：

```
Hello from S-mode kernel!
abc
```

你按什么字符就回显什么字符（`uart_intr` 里 `uart_getc` + `uart_putc` 实现了回显）。如果按键没有回显：

- 检查 `uart_init()` 是否使能了接收中断（IER 的 bit 0）
- 检查 `plic_init()` 设置的 UART0_IRQ(10) 优先级是否 > 0
- 检查 `plic_init_hart()` 的 enable 位是否包含 UART0_IRQ
- 检查 `sie` 是否使能了 `SIE_SEIE`（bit 9）

### 测试 4：SBI shutdown（清洁退出）

**验证内容**：SBI EID 0x08（shutdown）能让 QEMU 正常退出。

可在 `kernel_main()` 的 `while(1)` 前添加延时后关机测试：

```c
// 测试 SBI shutdown
printf("Shutting down...\n");
sbi_shutdown();
```

**预期输出**：

```
Hello from S-mode kernel!
Shutting down...
```

然后 QEMU 自动退出，不需要 Ctrl-A X。

### 测试 5：异常处理（panic）

**验证内容**：S-mode 异常被正确捕获，`s_trap_handler` 异常分支 → `panic()` 输出诊断信息。

在 `kernel_main()` 里临时添加一个非法内存访问：

```c
// 测试异常处理（测试完后删掉！）
printf("Testing exception...\n");
*(volatile int *)0xdeadbeef = 42;  // 触发 Store/AMO page fault (cause=15)
```

**预期输出**（大致）：

```
Hello from S-mode kernel!
Testing exception...

!!! KERNEL PANIC !!!
page fault
  sepc   = 0x80xxxxxx
  scause = 0xf
  stval  = 0xdeadbeef
```

`scause = 0xf`（即 15）表示 Store page fault，`stval = 0xdeadbeef` 就是触发异常的地址。如果看到这些说明整个 S-mode trap → exception → panic 链路都正常。

> 测试完记得删掉这行，否则内核永远 panic。

### 完整测试代码参考

以下是包含所有测试的 `kernel_main()` 参考实现。建议逐步取消注释，逐项验证：

```c
// kernel/main.c

#include "lib/printf.h"
#include "lib/riscv.h"
#include "driver/uart.h"
#include "driver/plic.h"
#include "../include/sbi_call.h"

static unsigned long rdtime(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

void kernel_main(void) {
    // 1. 设置 S-mode trap
    extern void s_trap_entry(void);
    csrw(stvec, (unsigned long)s_trap_entry);

    // 2. 初始化设备驱动
    uart_init();
    plic_init();
    plic_init_hart();

    // 3. 使能中断
    csrw(sie, SIE_SSIE | SIE_STIE | SIE_SEIE);
    csrs(sstatus, SSTATUS_SIE);

    // === 测试 1: S-mode 基本启动 ===
    printf("Hello from S-mode kernel!\n");

    // === 测试 2: 定时器中断 ===
    printf("[test] Setting timer (1s)...\n");
    sbi_set_timer(rdtime() + 10000000);

    // 简单延时等待定时器触发
    for (volatile int i = 0; i < 100000000; i++) {}

    // === 测试 3: UART 外部中断 ===
    printf("[test] Type some characters to test UART interrupt echo:\n");

    // === 测试 4: SBI shutdown（取消注释后 QEMU 将自动退出） ===
    // printf("[test] Shutting down via SBI...\n");
    // sbi_shutdown();

    // === 测试 5: 异常处理（取消注释后会 panic，测试完务必注释回去） ===
    // printf("[test] Triggering page fault...\n");
    // *(volatile int *)0xdeadbeef = 42;

    while (1) {}
}
```

> **重要**：测试 4 和 5 是破坏性的（一个退出 QEMU，一个 panic），默认注释掉。逐个取消注释验证后再注释回去。

---

## 检查清单

- 理解了 `mret` 如何实现特权级切换
- 正确配置了 `mstatus.MPP` 和 `mepc`
- 配置了 PMP 允许 S-mode 访问全部内存
- 配置了 `medeleg` 和 `mideleg` 委托
- 成功切换到 S-mode 并打印 "Hello from S-mode!"
- 实现了 S-mode trap 入口（`trap_entry.S` + `trap.c`）
- 创建了 `kernel/lib/` 基础库（types.h, string, printf, panic, riscv.h）
- 实现了 PLIC 驱动（init, claim, complete）
- 扩展了 UART 驱动（添加 uart_intr 中断处理）
- 实现了 `irq.c` 外部中断分发
- 实现了 `timer.c` 定时器中断处理（桩函数）
- 实现了 `exception.c` 异常处理（目前 panic）
- 创建了 `syscall.c` 桩函数
- M-mode bootloader 输出使用 `uart_puts`（不使用 SBI）
- SBI set_timer（EID 0x00）已实现
- 定时器中断能在 S-mode 下正常触发
- UART 键盘输入能正确回显
- SBI shutdown 能让 QEMU 正常退出
- 异常触发后 panic 能输出诊断信息（sepc, scause, stval）

---

## 下一步

S-mode 内核基础设施就绪后，进入 [阶段 4：内存管理](04-memory-management.md)，实现 Sv39 分页。