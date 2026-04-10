# 阶段 1：启动流程与串口输出

## 目标

完成本阶段后，你将：
- 理解 RISC-V 的 M-mode 启动过程
- 实现完整的 `entry.S`（栈设置、BSS 清零、跳转到 C）
- 实现 NS16550A UART 驱动
- 在 QEMU 上看到 "Hello, RISC-V OS!" 的输出

---

## 1. RISC-V 启动流程

RISC-V 处理器上电后从 M-mode（Machine mode，最高特权级）开始执行。在使用 `-bios none` 的 QEMU virt 平台上，执行流程为：

```
上电 → 所有 hart 从 0x80000000 开始执行
     → 我们的 entry.S 是第一段代码
     → 非主 hart 停放（wfi 循环）
     → 主 hart (hartid=0) 初始化硬件
     → 跳转到 C 语言 main()
```

> **Hart**：RISC-V 中的硬件线程（Hardware Thread），类似于 CPU 核心。QEMU virt 默认有 1 个 hart。

---

## 2. 完善 entry.S

你当前的 `entry.S` 已经有了 hart 识别和停放的基础。现在需要补充以下内容：

### 2.1 设置栈指针

C 语言需要栈才能工作。你需要在跳转到 C 之前设置好 `sp`（栈指针寄存器）。

**关键知识**：
- 栈从高地址向低地址增长
- 每个 hart 需要独立的栈空间
- 典型的内核栈大小：4KB ~ 16KB（我们先用 4KB = 4096 字节）

**实现思路**：
```
在 linker.ld 或 entry.S 中预留栈空间，例如：
    栈底（低地址）→ _stack_start
    栈顶（高地址）→ _stack_start + STACK_SIZE
    sp = 栈顶地址
```

可以在 `entry.S` 中用 `.space` 指令预留空间：

```asm
.section .bss
.global _stack_start
_stack_start:
    .space 4096       # 4KB 栈空间
.global _stack_end
_stack_end:
```

然后在 `_entry` 中设置栈指针：

```asm
    la sp, _stack_end     # sp 指向栈顶（高地址端）
```

### 2.2 清零 BSS 段

BSS 段存放未初始化的全局变量，C 语言规范要求它们初始值为 0。但加载器（这里是 QEMU）不保证这块内存为零，所以需要手动清零。

**实现思路**：
```
用循环将 _bss_start 到 _bss_end 之间的内存全部写零
```

```asm
    # 伪代码：
    la t0, _bss_start
    la t1, _bss_end
loop:
    bge t0, t1, done     # if t0 >= t1, 结束
    sd zero, 0(t0)       # 以 8 字节（双字）为单位清零
    addi t0, t0, 8
    j loop
done:
```

> `_bss_start` 和 `_bss_end` 来自你在 `linker.ld` 中定义的 `PROVIDE` 符号。

### 2.3 跳转到 C

```asm
    call main             # 调用 C 语言的 main 函数
```

如果 `main` 返回了（不应该发生），让 hart 进入停放状态：

```asm
    j _park_hart
```

### 2.4 完整的 entry.S 结构

```asm
.global _entry

.text
_entry:
    # 1. 读取 hart id，非零 hart 停放
    csrr t0, mhartid
    bnez t0, _park_hart

    # 2. 关闭中断
    csrw mie, zero
    csrw mstatus, zero

    # 3. 设置栈指针
    # ... 你来实现 ...

    # 4. 清零 BSS 段
    # ... 你来实现 ...

    # 5. 跳转到 C
    call main

    # main 不应该返回，但以防万一
    j _park_hart

_park_hart:
    wfi
    j _park_hart

# 栈空间定义（放在 .bss 段）
.section .bss
.global _stack_start
_stack_start:
    .space 4096
.global _stack_end
_stack_end:
```

---

## 3. 实现 UART 驱动

### 3.1 NS16550A 简介

NS16550A 是一个经典的串口芯片，QEMU virt 平台模拟了这个设备。它通过 **内存映射 I/O（MMIO）** 访问，基地址为 `0x10000000`。

你只需要通过读写特定的寄存器地址即可控制串口。

### 3.2 NS16550A 寄存器

每个寄存器占 1 字节，通过基地址 + 偏移访问：

| 偏移 | 寄存器 | 读/写 | 功能 |
|------|--------|-------|------|
| 0x00 | RHR/THR | 读：接收数据 / 写：发送数据 | 数据寄存器 |
| 0x01 | IER | 写 | 中断使能寄存器 |
| 0x02 | ISR/FCR | 读：中断状态 / 写：FIFO 控制 | |
| 0x03 | LCR | 读写 | 线路控制寄存器（数据位、停止位等） |
| 0x05 | LSR | 读 | 线路状态寄存器 |

### 3.3 UART 初始化

对于 QEMU 模拟的 UART，初始化非常简单：

```
1. 禁用中断：IER = 0x00
2. 设置波特率（QEMU 实际不关心，但走一遍流程）：
   a. LCR 置位 DLAB（bit 7），进入波特率设置模式
   b. 写入除数低/高字节
   c. LCR 清除 DLAB
3. 设置数据格式：LCR = 0x03（8 数据位，1 停止位，无校验）
4. 启用 FIFO：FCR = 0x07（使能 + 清空收发 FIFO）
5. （可选）使能接收中断：IER = 0x01
```

> **实际上**，在 QEMU 中你甚至可以跳过初始化，直接往 THR 写数据就能输出。但完整的初始化是好习惯。

### 3.4 发送一个字符

```c
#define UART0_BASE 0x10000000UL
#define UART_THR   0x00   // Transmit Holding Register
#define UART_LSR   0x05   // Line Status Register
#define UART_LSR_TX_EMPTY  (1 << 5)  // THR 空闲标志

void uart_putc(char c) {
    volatile char *uart = (volatile char *)UART0_BASE;

    // 等待发送寄存器空闲
    while ((uart[UART_LSR] & UART_LSR_TX_EMPTY) == 0)
        ;

    // 写入字符
    uart[UART_THR] = c;
}
```

**重点理解**：
- `volatile`：告诉编译器不要优化对这个地址的读写，因为硬件寄存器的值可能随时变化
- 必须等待 LSR 的 TX_EMPTY 位为 1 才能发送下一个字符，否则数据会丢失

### 3.5 接收一个字符

```c
#define UART_RHR   0x00   // Receive Holding Register
#define UART_LSR_RX_READY  (1 << 0)  // 接收数据就绪标志

int uart_getc(void) {
    volatile char *uart = (volatile char *)UART0_BASE;

    if (uart[UART_LSR] & UART_LSR_RX_READY) {
        return uart[UART_RHR];
    }
    return -1;  // 无数据可读
}
```

### 3.6 文件组织

UART 驱动放在 `kernel/driver/` 中，M-mode 和 S-mode 共用同一份代码：

```
kernel/driver/
├── uart.c           # UART 驱动实现
└── uart.h           # UART 接口声明

bootloader/
├── entry.S          # 你已有的入口
└── main.c           # M-mode 主函数（本阶段先在这里调用 UART）
```

> UART 驱动是纯 MMIO 操作，不依赖特权级，M-mode 和 S-mode 都能使用。
> 放在 `kernel/driver/` 中避免了代码重复。bootloader 通过 `-I kernel/driver` 找到头文件。

---

## 4. 实现简易 printf

有了 `uart_putc`，你就可以实现一个简易的 `printf`。

### 4.1 先实现 uart_puts

```c
void uart_puts(const char *s) {
    while (*s) {
        uart_putc(*s);
        s++;
    }
}
```

### 4.2 简易 printf 的核心

`printf` 的核心是解析格式字符串中的 `%` 占位符。你需要支持的格式：

| 格式 | 含义 | 优先级 |
|------|------|--------|
| `%s` | 字符串 | 必须 |
| `%d` | 十进制整数 | 必须 |
| `%x` | 十六进制整数 | 必须（调试时非常有用） |
| `%c` | 单个字符 | 有用 |
| `%p` | 指针（即 `0x` + 十六进制） | 有用 |
| `%%` | 输出一个 `%` | 有用 |

**实现提示**：
- 使用 C 的 **可变参数**（`<stdarg.h>` 中的 `va_list`, `va_start`, `va_arg`, `va_end`）
- `<stdarg.h>` 是编译器内置的头文件，裸机环境也可以使用
- 整数转字符串：用除法和取余，注意处理负数和零

```c
#include <stdarg.h>

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            uart_putc(*fmt);
            continue;
        }
        fmt++;  // 跳过 '%'
        switch (*fmt) {
        case 'd':
            // 获取参数：int val = va_arg(ap, int);
            // 将 val 转换为字符串并输出
            break;
        case 'x':
            // 十六进制输出
            break;
        case 's':
            // 字符串输出
            break;
        // ... 其他格式
        }
    }

    va_end(ap);
}
```

### 练习

自己实现 `print_int()` 和 `print_hex()` 辅助函数，然后在 `printf` 中调用。

---

## 5. 编写 main.c

注意：`printf` 函数在 `kernel/lib/printf.c` 中实现，头文件在 `kernel/lib/printf.h`。
从 `bootloader/main.c` 引用它的路径是 `"../kernel/lib/printf.h"`。

```c
// bootloader/main.c
#include "../kernel/driver/uart.h"
#include "../kernel/lib/printf.h"

void main(void) {
    uart_init();
    printf("Hello, RISC-V OS!\n");
    printf("We are running in M-mode at privilege level %d\n", 3);

    // 死循环，防止 main 返回
    while (1) {}
}
```

---

## 6. 更新 Makefile

确保 Makefile 能编译所有新文件：

- `bootloader/entry.S` → `entry.o`
- `bootloader/main.c` → `main.o`
- `kernel/driver/uart.c` → `uart.o`
- `kernel/lib/printf.c` → `printf.o`

链接顺序：`entry.o` 必须在第一个（因为 `_entry` 是入口点）。

```makefile
SRCS_ASM = bootloader/entry.S
SRCS_C = bootloader/main.c \
         kernel/driver/uart.c \
         kernel/lib/printf.c
```

---

## 7. 测试

```bash
make run
```

你应该在终端看到：
```
Hello, RISC-V OS!
We are running in M-mode at privilege level 3
```

如果没有输出或出现错误：

1. **QEMU 直接退出**：检查 `linker.ld` 的 `BASE_ADDRESS` 是否为 `0x80000000`
2. **没有输出**：用 GDB 检查是否到达了 `uart_putc`
3. **乱码**：检查 UART 初始化流程
4. **链接错误**：检查符号名是否匹配（`_entry`、`main` 等）

---

## 关键概念回顾

| 概念 | 说明 |
|------|------|
| M-mode | Machine mode，RISC-V 最高特权级，上电后的默认模式 |
| Hart | Hardware Thread，RISC-V 硬件线程 |
| MMIO | Memory-Mapped I/O，通过读写特定内存地址来访问硬件 |
| BSS | Block Started by Symbol，存放未初始化全局变量的段 |
| volatile | C 关键字，阻止编译器优化对某地址的读写 |

---

## 检查清单

- [ ] `entry.S` 正确设置了栈指针
- [ ] `entry.S` 正确清零了 BSS 段
- [ ] `entry.S` 成功跳转到 `main()`
- [ ] UART 驱动能正确输出字符
- [ ] `printf` 至少支持 `%s`, `%d`, `%x`
- [ ] `make run` 能看到 "Hello, RISC-V OS!"

---

## 下一步

有了串口输出，你就有了最重要的调试手段。接下来进入 [阶段 2：M-mode Trap 处理与 SBI 实现](02-m-mode-trap-and-sbi.md)。
