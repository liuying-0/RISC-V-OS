# 阶段 2：M-mode Trap 处理与 SBI 实现

## 目标

完成本阶段后，你将：
- 理解 RISC-V 的 trap（中断/异常）机制
- 实现 M-mode 的 trap 处理框架
- 实现简化版 SBI（Supervisor Binary Interface），为 S-mode 内核提供服务
- 准备好从 M-mode 切换到 S-mode 的基础设施

---

## 1. 理解 RISC-V Trap 机制

### 1.1 什么是 Trap

Trap 是一个统称，包括：
- **异常（Exception）**：同步事件，由当前执行的指令引发
  - 例如：非法指令、访问错误、ecall（系统调用）、缺页等
- **中断（Interrupt）**：异步事件，由外部源引发
  - 例如：定时器中断、外部中断（UART 等）、软件中断

### 1.2 Trap 处理流程（硬件自动完成）

当 trap 发生时，RISC-V 硬件**自动**执行以下操作（以 M-mode 为例）：

```
1. 将当前 PC 保存到 mepc（Machine Exception PC）
2. 将 trap 原因写入 mcause
3. 将触发地址/值写入 mtval（某些 trap 类型）
4. 将当前特权级保存到 mstatus.MPP
5. 将当前 mstatus.MIE 保存到 mstatus.MPIE，然后清零 MIE（关中断）
6. 跳转到 mtvec 指定的地址开始执行 trap 处理代码
```

`mret` 指令执行相反的操作：
```
1. 将 mepc 的值恢复到 PC（跳回原来的位置）
2. 将 mstatus.MPP 恢复到当前特权级
3. 将 mstatus.MPIE 恢复到 mstatus.MIE
```

### 1.3 关键的 M-mode CSR 寄存器

| CSR | 名称 | 功能 |
|-----|------|------|
| `mstatus` | Machine Status | 全局中断使能(MIE)、之前特权级(MPP)等 |
| `mtvec` | Machine Trap-Vector | trap 处理入口地址 |
| `mepc` | Machine Exception PC | trap 时保存的 PC |
| `mcause` | Machine Cause | trap 原因（最高位区分中断/异常，低位是具体编号） |
| `mtval` | Machine Trap Value | trap 附加信息（如缺页地址） |
| `mie` | Machine Interrupt Enable | 各类中断的单独使能位 |
| `mip` | Machine Interrupt Pending | 各类中断的挂起状态 |
| `medeleg` | Machine Exception Delegation | 异常委托（哪些异常直接交给 S-mode 处理） |
| `mideleg` | Machine Interrupt Delegation | 中断委托 |

### 1.4 mcause 编码

`mcause` 是 64 位寄存器，最高位（bit 63）区分中断和异常，低 63 位是具体编号：

```
bit 63 = 1 → 中断（Interrupt）
bit 63 = 0 → 异常（Exception）
```

**异常编号**：

| 编号 | 名称 | 说明 |
|------|------|------|
| 0 | Instruction address misaligned | 指令地址未对齐 |
| 1 | Instruction access fault | 指令访问错误 |
| 2 | Illegal instruction | 非法指令 |
| 3 | Breakpoint | 断点 |
| 4 | Load address misaligned | 加载地址未对齐 |
| 5 | Load access fault | 加载访问错误 |
| 6 | Store address misaligned | 存储地址未对齐 |
| 7 | Store access fault | 存储访问错误 |
| 8 | Environment call from U-mode | U-mode ecall（系统调用） |
| 9 | Environment call from S-mode | S-mode ecall（SBI 调用） |
| 11 | Environment call from M-mode | M-mode ecall |
| 12 | Instruction page fault | 指令缺页 |
| 13 | Load page fault | 加载缺页 |
| 15 | Store page fault | 存储缺页 |

**中断编号**：

| 编号 | 名称 | 说明 |
|------|------|------|
| 1 | Supervisor software interrupt | S-mode 软件中断 |
| 3 | Machine software interrupt | M-mode 软件中断 |
| 5 | Supervisor timer interrupt | S-mode 定时器中断 |
| 7 | Machine timer interrupt | M-mode 定时器中断 |
| 9 | Supervisor external interrupt | S-mode 外部中断 |
| 11 | Machine external interrupt | M-mode 外部中断 |

---

## 2. 实现 M-mode Trap 处理

本阶段涉及 4 个文件，调用关系如下：

```
entry.S           → 设置 mtvec 寄存器指向 m_trap_entry
trap_entry.S      → m_trap_entry：保存寄存器 → 调用 C 函数 → 恢复寄存器 → mret
trap.c        → m_trap_handler：根据 mcause 分发处理
sbi.c         → sbi_handler：处理 S-mode 的 SBI 调用
```

### 2.1 定义 trap_context 结构体（`bootloader/trap.h`）

trap 发生时，CPU 的所有寄存器需要保存到栈上，这块内存用 `trap_context` 结构体来描述，方便 C 代码访问。

**关键规则**：结构体的字段顺序必须和 `trap_entry.S` 中 `sd` 指令的顺序完全一致，否则 C 代码读到的值是错的。

> **命名说明**：同一目录下 `trap.c` 和 `trap.S` 编译后都会生成 `trap.o`，会产生命名冲突。因此汇编入口文件命名为 `trap_entry.S`（生成 `trap_entry.o`），与 `trap.c` 的 `trap.o` 区分开。

```c
// bootloader/trap.h

#ifndef __TRAP_H__
#define __TRAP_H__

struct trap_context {
    unsigned long ra;   // x1  offset 0
    unsigned long sp;   // x2  offset 8
    unsigned long gp;   // x3  offset 16
    unsigned long tp;   // x4  offset 24
    unsigned long t0;   // x5  offset 32
    unsigned long t1;   // x6  offset 40
    unsigned long t2;   // x7  offset 48
    unsigned long s0;   // x8  offset 56
    unsigned long s1;   // x9  offset 64
    unsigned long a0;   // x10 offset 72
    unsigned long a1;   // x11 offset 80
    unsigned long a2;   // x12 offset 88
    unsigned long a3;   // x13 offset 96
    unsigned long a4;   // x14 offset 104
    unsigned long a5;   // x15 offset 112
    unsigned long a6;   // x16 offset 120
    unsigned long a7;   // x17 offset 128
    unsigned long s2;   // x18 offset 136
    unsigned long s3;   // x19 offset 144
    unsigned long s4;   // x20 offset 152
    unsigned long s5;   // x21 offset 160
    unsigned long s6;   // x22 offset 168
    unsigned long s7;   // x23 offset 176
    unsigned long s8;   // x24 offset 184
    unsigned long s9;   // x25 offset 192
    unsigned long s10;  // x26 offset 200
    unsigned long s11;  // x27 offset 208
    unsigned long t3;   // x28 offset 216
    unsigned long t4;   // x29 offset 224
    unsigned long t5;   // x30 offset 232
    unsigned long t6;   // x31 offset 240
    unsigned long mepc; //      offset 248
};

#endif /* __TRAP_H__ */
```

> **为什么没有 x0（zero）？** x0 是硬连线到 0 的只读寄存器，读永远是 0，写无效，不需要保存。

### 2.2 在 `entry.S` 中设置 mtvec

在跳转到 `main` 之前，设置 `mtvec` 寄存器，告诉 CPU trap 入口在哪里：

```asm
# bootloader/entry.S（在 call main 之前）
la  t0, m_trap_entry   # 把 m_trap_entry 的地址加载到 t0
csrw mtvec, t0         # 写入 mtvec，从此以后发生 trap 就跳到 m_trap_entry
```

> **mtvec 的模式**：mtvec 最低两位是模式位。`00` 表示 Direct 模式（所有 trap 都跳同一个地址），`01` 表示 Vectored 模式（不同中断跳不同地址）。我们用 Direct 模式，只要地址 4 字节对齐即可（`.align 4` 保证了这一点）。

### 2.3 trap 入口汇编（`bootloader/trap_entry.S`）

trap 入口必须用汇编写，原因是：进入 trap 时 CPU 所有寄存器还是被打断程序的值，如果直接调用 C 函数，C 函数的执行会覆盖这些寄存器，导致被打断的程序无法正确恢复。

**完整实现**：

```asm
# bootloader/trap_entry.S

.global m_trap_entry

.align 4    # mtvec 要求 4 字节对齐

m_trap_entry:
    # 步骤 1：在栈上分配 256 字节空间
    # 31 个通用寄存器 × 8 字节 = 248 字节，加上 mepc 的 8 字节 = 256 字节
    addi sp, sp, -256

    # 步骤 2：把所有通用寄存器保存到栈上（x0 不需要保存）
    sd ra,    0(sp)
    sd sp,    8(sp)
    sd gp,   16(sp)
    sd tp,   24(sp)
    sd t0,   32(sp)
    # ... 依此类推到 t6, 240(sp)

    # 步骤 3：保存 mepc（trap 发生时 CPU 正在执行哪条指令）
    csrr t0, mepc
    sd   t0, 248(sp)

    # 步骤 4：调用 C 处理函数
    # a0 = mcause（为什么发生 trap）
    # a1 = sp（trap_context 结构体的地址，让 C 函数能访问所有保存的寄存器）
    csrr a0, mcause
    mv   a1, sp
    call m_trap_handler

    # 步骤 5：从 trap_context 恢复 mepc（C 代码可能修改了它，例如 ecall +4）
    ld   t0, 248(sp)
    csrw mepc, t0

    # 步骤 6：恢复所有通用寄存器（注意 sp 不在这里恢复，最后通过 addi 恢复）
    ld ra,    0(sp)
    ld gp,   16(sp)
    # ... 依此类推
    ld t6,  240(sp)
    addi sp, sp, 256   # 恢复 sp

    # 步骤 7：mret 返回，CPU 从 mepc 处继续执行
    mret
```

> **为什么 sp 不用 ld 恢复？** 恢复其他寄存器时需要用 sp 来寻址（`ld ra, 0(sp)`），如果提前用 `ld sp, 8(sp)` 恢复了 sp，后续的寻址就乱了。所以最后用 `addi sp, sp, 256` 来恢复 sp（把分配的空间还回去）。

### 2.4 C 语言 trap 处理函数（`bootloader/trap.c`）

```c
// bootloader/trap.c

#include "../kernel/lib/printf.h"
#include "trap.h"
#include "sbi.h"

void m_trap_handler(unsigned long mcause, struct trap_context *ctx) {
    // 最高位为 1 = 中断，为 0 = 异常
    int is_interrupt = (mcause >> 63) & 1;
    // 清掉最高位，得到具体的原因编号
    unsigned long cause = mcause & 0x7FFFFFFFFFFFFFFFULL;

    if (is_interrupt) {
        switch (cause) {
            case 7:
                // M-mode 定时器中断（后续阶段实现）
                break;
            default:
                printf("Unknown M-mode interrupt: cause=%d\n", cause);
                break;
        }
    } else {
        switch (cause) {
            case 9:
                // S-mode ecall：S-mode 内核请求 SBI 服务
                sbi_handler(ctx);
                break;
            default:
                // 无法处理的异常，打印信息后停机
                printf("M-mode exception: cause=%d, mepc=0x%x\n", cause, ctx->mepc);
                while (1) {}
        }
    }
}
```

---

## 3. SBI（Supervisor Binary Interface）

### 3.1 什么是 SBI

SBI 是 M-mode 提供给 S-mode 的服务接口，作用类似于 BIOS 之于操作系统。S-mode 内核通过 `ecall` 指令发起 SBI 调用，M-mode 负责实现这些服务。

在真实系统中 SBI 由 OpenSBI 实现，这里我们自己实现一个简化版本。

### 3.2 SBI 调用约定

S-mode 通过寄存器传递参数，这些寄存器在 trap 发生时已经被 `trap_entry.S` 保存到 `trap_context` 里了：

| 寄存器 | 用途 |
|--------|------|
| `a7` | EID（Extension ID）：调用哪种服务 |
| `a6` | FID（Function ID）：服务内的子功能（简化实现可忽略） |
| `a0`~`a5` | 参数 |
| `a0` | 返回值（写回 `ctx->a0`，mret 后 S-mode 就能读到） |

### 3.3 我们需要实现的 SBI 调用

| EID | 名称 | 功能 |
|-----|------|------|
| `0x01` | console_putchar | 输出一个字符（参数在 a0） |
| `0x02` | console_getchar | 从串口读取一个字符（返回值写入 a0） |
| `0x08` | shutdown | 关机 |

### 3.4 SBI 实现（`bootloader/sbi.c`）

```c
// bootloader/sbi.c

#include "../kernel/driver/uart.h"
#include "trap.h"

void sbi_handler(struct trap_context *ctx) {
    unsigned long eid  = ctx->a7;   // 从保存的寄存器里读 EID
    unsigned long arg0 = ctx->a0;   // 读第一个参数

    switch (eid) {
        case 0x01:
            uart_putc((char)arg0);
            break;
        case 0x02:
            ctx->a0 = uart_getc();  // 返回值写回 a0，mret 后 S-mode 能读到
            break;
        case 0x08:
            *(volatile unsigned int *)0x100000 = 0x5555;  // QEMU 关机
            break;
        default:
            break;
    }

    // 重要：ecall 指令是 4 字节，处理完必须让 mepc +4
    // 否则 mret 会跳回 ecall 指令再次执行，形成死循环
    ctx->mepc += 4;
}
```

> **为什么 mepc 要 +4？** `ecall` 触发异常时，`mepc` 保存的是 `ecall` 这条指令自身的地址。如果不修改 `mepc`，`mret` 后 CPU 会回到 `ecall` 再次执行，再次触发 trap，无限循环。`+4` 是跳过这条指令，继续执行 `ecall` 后面的代码。

### 3.5 SBI 声明（`bootloader/sbi.h`）

```c
// bootloader/sbi.h

#ifndef __SBI_H__
#define __SBI_H__

#include "trap.h"

void sbi_handler(struct trap_context *ctx);

#endif /* __SBI_H__ */
```

### 3.6 S-mode 端调用 SBI（`include/sbi_call.h`，后续阶段使用）

S-mode 内核想使用 SBI 服务时，通过如下封装发起 ecall：

```c
// include/sbi_call.h

static inline long sbi_call(long eid, long fid, long arg0, long arg1, long arg2) {
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = eid;

    asm volatile (
        "ecall"
        : "+r"(a0)
        : "r"(a1), "r"(a2), "r"(a6), "r"(a7)
        : "memory"
    );
    return a0;
}

#define sbi_console_putchar(c) sbi_call(0x01, 0, (c), 0, 0)
#define sbi_console_getchar()  sbi_call(0x02, 0, 0, 0, 0)
#define sbi_set_timer(t)       sbi_call(0x00, 0, (t), 0, 0)
#define sbi_shutdown()         sbi_call(0x08, 0, 0, 0, 0)
```

> 这个文件在阶段 3 进入 S-mode 后才会用到，本阶段不需要实现。

---

## 4. 关键细节

### 4.1 整个 trap 调用链

```
trap 发生
  ↓ CPU 硬件自动：保存 PC 到 mepc，记录原因到 mcause，跳到 mtvec
trap_entry.S: m_trap_entry
  ↓ addi sp, sp, -256（分配栈空间）
  ↓ sd ra/sp/gp/...（保存 31 个寄存器 + mepc 到栈上）
  ↓ csrr a0, mcause（准备第一个参数）
  ↓ mv a1, sp（准备第二个参数：trap_context 指针）
  ↓ call m_trap_handler
trap.c: m_trap_handler(mcause, ctx)
  ↓ 判断是中断还是异常
  ↓ case 9（S-mode ecall）→ sbi_handler(ctx)
sbi.c: sbi_handler(ctx)
  ↓ 读 ctx->a7 得到 EID
  ↓ 执行对应服务（putchar/getchar/shutdown）
  ↓ ctx->mepc += 4（跳过 ecall 指令）
  ↓ 返回
trap_entry.S（继续）
  ↓ ld t0, 248(sp) + csrw mepc, t0（把修改后的 mepc 写回 CSR）
  ↓ ld ra/gp/tp/...（恢复所有寄存器）
  ↓ addi sp, sp, 256（恢复 sp）
  ↓ mret（CPU 从新的 mepc 继续执行）
```

### 4.2 各 ecall 由谁处理

| 谁调用 | ecall | cause | 由谁处理 |
|--------|-------|-------|----------|
| U-mode | ecall | 8 | S-mode trap handler（阶段 3 后实现） |
| S-mode | ecall | 9 | M-mode `sbi_handler`（本阶段实现） |
| M-mode | ecall | 11 | M-mode trap handler（一般不需要） |

### 4.3 trap_context 字段偏移必须和 trap_entry.S 对齐

`trap_context` 结构体的字段偏移是由 C 编译器按顺序排列的（每个 `unsigned long` 占 8 字节）。`trap_entry.S` 中 `sd` 指令的偏移必须和结构体一致：

```
ctx->ra  在偏移 0  ← 对应 sd ra,  0(sp)
ctx->sp  在偏移 8  ← 对应 sd sp,  8(sp)
ctx->a7  在偏移 128 ← 对应 sd a7, 128(sp)
ctx->mepc 在偏移 248 ← 对应 sd t0, 248(sp)（mepc 先读到 t0 再保存）
```

如果顺序错了，`ctx->a7` 读到的就不是 a7 的值，SBI 调用会完全混乱。

---

## 5. 定时器中断（预览）

RISC-V 的定时器由 CLINT（Core Local Interruptor）管理，地址在 `0x0200_0000`：

| 地址 | 寄存器 | 功能 |
|------|--------|------|
| `0x0200_0000` | msip | 软件中断挂起位 |
| `0x0200_4000` | mtimecmp | 定时器比较值（写入新值设置下次中断） |
| `0x0200_BFF8` | mtime | 当前时间计数器（只读，持续递增） |

当 `mtime >= mtimecmp` 时，触发 M-mode 定时器中断（mcause = 0x8000000000000007）。

```c
#define CLINT_BASE          0x02000000UL
#define CLINT_MTIME         (*(volatile unsigned long *)(CLINT_BASE + 0xBFF8))
#define CLINT_MTIMECMP      (*(volatile unsigned long *)(CLINT_BASE + 0x4000))

void timer_init(void) {
    // 设置 10000000 个 tick 后触发中断（约 1 秒）
    CLINT_MTIMECMP = CLINT_MTIME + 10000000;
}
```

> 定时器中断总是先触发 M-mode，即使委托给 S-mode 也一样。M-mode 处理完后通过软件中断通知 S-mode。这将在下一阶段详细讲解。

---

## 6. 本阶段的文件结构

```
bootloader/
├── entry.S        ← 添加 mtvec 设置（la t0, m_trap_entry; csrw mtvec, t0）
├── main.c         ← 测试 trap 处理
├── trap_entry.S   ← trap 汇编入口：保存/恢复寄存器（避免与 trap.c 同生 trap.o 冲突）
├── trap.c         ← trap C 处理函数：根据 mcause 分发
├── trap.h      ← trap_context 结构体定义
├── sbi.c       ← SBI 服务实现
└── sbi.h       ← sbi_handler 声明

kernel/driver/
├── uart.c      ← UART 驱动（阶段 1 已实现，M/S 共用）
└── uart.h
```

Makefile 中需要加入新文件：
```makefile
SRCS_ASM = bootloader/entry.S \
           bootloader/trap_entry.S
SRCS_C = bootloader/main.c \
         bootloader/trap.c \
         bootloader/sbi.c \
         kernel/driver/uart.c \
         kernel/lib/printf.c
```

---

## 7. 测试

在 `main.c` 中可以主动触发一个异常来验证 trap 处理是否工作：

```c
void main(void) {
    uart_init();
    // 设置 mtvec
    // ...（在 entry.S 里设置，这里不需要重复）

    printf("M-mode trap handler ready.\n");

    // 测试：主动触发非法指令异常（asm volatile 执行一条非法指令）
    // asm volatile (".word 0x00000000");

    // 测试 SBI putchar（等进入 S-mode 后再测，现在可以直接调 uart_putc）

    printf("Done.\n");
    while (1) {}
}
```

---

## 检查清单

- [ ] `trap.h` 定义了 `trap_context` 结构体，有 include 保护，有分号
- [ ] `trap_entry.S` 的 `sd` 偏移和 `trap_context` 字段偏移完全一致
- [ ] `trap_entry.S` 保存了 `mepc`（offset 248），并在 mret 前写回 CSR
- [ ] `trap_entry.S` 在 call 前执行了 `mv a1, sp`，将 ctx 指针传给 C 函数
- [ ] `entry.S` 在 call main 前设置了 `mtvec` 指向 `m_trap_entry`
- [ ] `trap.c` 的函数签名是 `(unsigned long mcause, struct trap_context *ctx)`
- [ ] `sbi.c` 在处理完后执行了 `ctx->mepc += 4`
- [ ] Makefile 包含了 `trap_entry.S`、`trap.c`、`sbi.c`
- [ ] `make` 能成功编译，`make run` 能正常启动

---

## 下一步

trap 框架和 SBI 准备就绪后，进入 [阶段 3：进入 S-mode 与内核初始化](03-entering-s-mode.md)。
