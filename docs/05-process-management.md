# 阶段 5：进程管理

## 目标

完成本阶段后，你将：
- 理解进程抽象与 PCB 设计
- 实现上下文切换（寄存器保存/恢复）
- 实现 Round-Robin 调度器
- 能在多个内核线程之间进行时间片轮转

---

## 1. 进程的概念

### 1.1 进程 vs 线程

- **进程**：拥有独立地址空间（独立页表），是资源分配的基本单位
- **内核线程**：共享内核地址空间，只有独立的栈和上下文

建议的实现顺序：
1. 先实现**内核线程**（共享内核页表，不涉及用户态切换）
2. 验证上下文切换和调度器正确
3. 再扩展为**完整进程**（独立页表 + 用户态）

### 1.2 进程状态

```
    创建
     │
     ▼
  ┌──────┐   调度选中   ┌──────┐
  │READY ├─────────────→│RUNNING│
  │就绪   │◄─────────────┤运行   │
  └──┬───┘  时间片耗尽   └──┬───┘
     │                      │
     │                      │ 等待 I/O 或锁
     │                      ▼
     │                 ┌──────┐
     │◄────────────────┤SLEEPING│
     │   事件完成       │睡眠   │
     │                 └──────┘
     │
     └──── exit ──→ ZOMBIE（等待父进程回收）
                       │
                       └──→ 销毁
```

---

## 2. 进程控制块（PCB）

### 2.1 结构体设计

```c
// kernel/proc/proc.h

#ifndef __PROC_H__
#define __PROC_H__

#include "../mm/page_table.h"    // pagetable_t
#include "../trap/trap.h"        // struct trap_context

#define MAX_PROCS  64

enum proc_state {
    UNUSED,
    READY,
    RUNNING,
    SLEEPING,
    ZOMBIE,
};

// 上下文：只需保存 callee-saved 寄存器
// caller-saved 寄存器由函数调用约定保证被调用者不会破坏
struct context {
    unsigned long ra;       // 返回地址
    unsigned long sp;       // 栈指针
    unsigned long s0;
    unsigned long s1;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
};

struct proc {
    int pid;                        // 进程 ID
    enum proc_state state;          // 进程状态
    struct context context;         // 上下文（用于切换）
    unsigned long kstack;           // 内核栈底地址
    char name[16];                  // 进程名称

    // 以下字段本阶段不使用，为阶段 6（用户态）预留
    pagetable_t pagetable;          // 进程页表（阶段 6）
    struct trap_context *trapframe; // trap 帧（阶段 6）
    unsigned long sz;               // 进程内存大小（阶段 6）
};

// 全局进程表
extern struct proc proc_table[MAX_PROCS];

// 当前运行的进程（定义在 scheduler.c 中）
extern struct proc *current_proc;

// switch_to 是汇编函数（定义在 switch.S 中），需要在 C 中声明
extern void switch_to(struct context *old, struct context *new);

void proc_init(void);
struct proc *proc_create(const char *name, void (*entry)(void));
void proc_exit(int status);
void proc_destroy(struct proc *p);

#endif
```

### 2.2 为什么只保存 callee-saved 寄存器

上下文切换发生在函数调用内部（调用 `switch_to`）。根据 RISC-V 调用约定：
- **caller-saved**（t0-t6, a0-a7, ra）：调用者负责保存，在 `switch_to` 被调用前已经被编译器处理
- **callee-saved**（s0-s11, sp）：被调用者负责保存，需要我们手动保存

加上 `ra`（返回地址），切换后通过 `ret` 指令跳转到保存的 `ra`，实现执行流切换。

> **对比 trap 上下文**：trap 是异步/意外发生的，没有调用约定的保护，所以需要保存**所有 31 个通用寄存器**。

---

## 3. 上下文切换

### 3.1 switch.S

这是整个进程管理中最关键的函数：

```asm
# kernel/proc/switch.S
#
# void switch_to(struct context *old, struct context *new);
# a0 = old context 指针
# a1 = new context 指针

.global switch_to
switch_to:
    # 保存当前上下文到 old（a0 指向的 struct context）
    sd ra,  0(a0)
    sd sp,  8(a0)
    sd s0,  16(a0)
    sd s1,  24(a0)
    sd s2,  32(a0)
    sd s3,  40(a0)
    sd s4,  48(a0)
    sd s5,  56(a0)
    sd s6,  64(a0)
    sd s7,  72(a0)
    sd s8,  80(a0)
    sd s9,  88(a0)
    sd s10, 96(a0)
    sd s11, 104(a0)

    # 恢复新上下文从 new（a1 指向的 struct context）
    ld ra,  0(a1)
    ld sp,  8(a1)
    ld s0,  16(a1)
    ld s1,  24(a1)
    ld s2,  32(a1)
    ld s3,  40(a1)
    ld s4,  48(a1)
    ld s5,  56(a1)
    ld s6,  64(a1)
    ld s7,  72(a1)
    ld s8,  80(a1)
    ld s9,  88(a1)
    ld s10, 96(a1)
    ld s11, 104(a1)

    ret     # 跳转到 new context 的 ra
```

### 3.2 切换流程图解

```
进程 A 运行中
    │
    │ 定时器中断触发
    ▼
trap handler → 调用 schedule()
    │
    ▼
schedule() 选择进程 B → 调用 switch_to(&A.context, &B.context)
    │
    │  保存 A 的 ra, sp, s0~s11
    │  恢复 B 的 ra, sp, s0~s11
    │  ret → 跳到 B 的 ra
    ▼
进程 B 从上次被切换走的位置继续执行
```

---

## 4. 调度器

### 4.1 调度器接口

```c
// kernel/proc/scheduler.h

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__

#include "proc.h"        // struct context, struct proc, current_proc

void scheduler(void);    // 调度器主循环（永不返回）
void yield(void);        // 当前进程让出 CPU

#endif
```

### 4.2 Round-Robin（时间片轮转）

最简单的调度算法：按顺序遍历进程表，找到下一个 READY 的进程。

```c
// kernel/proc/scheduler.c

#include "scheduler.h"
#include "proc.h"

// 调度器自身的上下文（不属于任何进程）
// 进程 yield 时通过 switch_to 回到调度器，就是恢复到这里保存的 ra/sp/s0-s11
static struct context scheduler_context;

// 当前正在运行的进程（全局，在 proc.h 中 extern 声明）
struct proc *current_proc = NULL;

// 调度器主循环
// 这个函数在内核初始化完成后被调用，永不返回
void scheduler(void) {
    struct proc *p;

    while (1) {
        for (int i = 0; i < MAX_PROCS; i++) {
            p = &proc_table[i];
            if (p->state != READY) continue;

            // 找到一个就绪进程，切换到它
            p->state = RUNNING;
            current_proc = p;

            // 保存调度器上下文，恢复进程上下文，ret 跳到进程的 ra
            switch_to(&scheduler_context, &p->context);

            // 进程调用 yield() 后，switch_to 回到这里继续循环
            current_proc = NULL;
        }
        // 没有就绪进程时，空转（或者 wfi 等待中断）
    }
}

// 进程主动让出 CPU，切换回调度器
void yield(void) {
    current_proc->state = READY;
    // 保存进程上下文，恢复调度器上下文，ret 回到 scheduler() 的 for 循环
    switch_to(&current_proc->context, &scheduler_context);
}
```

**关键理解**：
- `scheduler_context` 是一个全局的 `struct context`，代表调度器的执行上下文
- `switch_to` 保存当前进程的上下文，切换到调度器；调度器选择下一个进程后，再 `switch_to` 切换到新进程
- 这形成了一个循环：进程 → 调度器 → 进程 → 调度器 → ...

### 4.3 定时器驱动调度

在 S-mode 定时器中断处理中触发调度：

```c
// kernel/trap/timer.c（在阶段 3 已新建，本阶段扩展完善）

#include "trap.h"
#include "../../include/sbi_call.h"   // sbi_set_timer
#include "../lib/printf.h"
#include "../proc/scheduler.h"        // yield

static unsigned long ticks = 0;
#define TIMER_INTERVAL  10000000      // 大约 1 秒（QEMU 默认 10MHz 时钟）

static unsigned long rdtime(void) {
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

void timer_init(void) {
    sbi_set_timer(rdtime() + TIMER_INTERVAL);
}

void timer_handler(void) {
    ticks++;
    sbi_set_timer(rdtime() + TIMER_INTERVAL);
    yield();   // 触发调度，切换到下一个就绪进程
}
```

---

## 5. 创建内核线程

### 5.1 proc.c — 进程操作

进程的创建、销毁等操作放在 `kernel/proc/proc.c` 中：

```c
// kernel/proc/proc.c

#include "proc.h"
#include "../mm/pmm.h"          // pmm_alloc, pmm_free, PAGE_SIZE
#include "../lib/string.h"      // memset, strncpy

struct proc proc_table[MAX_PROCS];
static int next_pid = 1;

// 初始化进程表
// proc_table 是全局数组，C 语言保证零初始化，而 UNUSED = 0，
// 所以所有槽位默认就是 UNUSED。这里显式 memset 是防御性编程。
void proc_init(void) {
    memset(proc_table, 0, sizeof(proc_table));
}

struct proc *proc_create(const char *name, void (*entry)(void)) {
    // 1. 从进程表中找到一个 UNUSED 的槽位
    struct proc *p = NULL;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (proc_table[i].state == UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p) return NULL;

    // 2. 分配内核栈（1 页 = 4KB）
    p->kstack = (unsigned long)pmm_alloc();
    if (!p->kstack) return NULL;

    // 3. 设置初始上下文
    //    关键：将 ra 设为入口函数地址
    //    当 switch_to 恢复这个上下文时，ret 会跳到 entry
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (unsigned long)entry;
    p->context.sp = p->kstack + PAGE_SIZE;  // 栈从高地址向低地址增长，所以 sp 指向页顶

    // 4. 设置其他字段
    p->pid = next_pid++;
    p->state = READY;
    strncpy(p->name, name, sizeof(p->name));

    return p;
}
```

### 5.2 测试：两个内核线程交替运行

```c
// kernel/main.c

#include "lib/printf.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "proc/proc.h"
#include "proc/scheduler.h"

void thread_a(void) {
    while (1) {
        printf("Thread A running\n");
        yield();
    }
}

void thread_b(void) {
    while (1) {
        printf("Thread B running\n");
        yield();
    }
}

void kernel_main(void) {
    // ... 之前阶段的初始化代码（uart_init, plic_init 等）...

    pmm_init();
    vmm_init();
    proc_init();

    proc_create("thread_a", thread_a);
    proc_create("thread_b", thread_b);

    printf("Starting scheduler...\n");
    scheduler();  // 永不返回
}
```

预期输出：
```
Starting scheduler...
Thread A running
Thread B running
Thread A running
Thread B running
...
```

如果用定时器驱动，把 `yield()` 去掉，线程内用死循环打印，然后由定时器中断触发切换。

---

## 6. 进程退出与回收

### 6.1 exit

以下函数也放在 `kernel/proc/proc.c` 中（需要 `#include "../proc/scheduler.h"` 以使用 `yield`）：

```c
// kernel/proc/proc.c（续）

#include "scheduler.h"   // yield

void proc_exit(int status) {
    current_proc->state = ZOMBIE;
    yield();  // 切回调度器，永不回来（调度器不会选中 ZOMBIE 进程）
}
```

### 6.2 进程销毁

```c
// kernel/proc/proc.c（续）

void proc_destroy(struct proc *p) {
    pmm_free((void *)p->kstack);
    p->kstack = 0;
    p->pid = 0;
    p->state = UNUSED;
}
```

---

## 7. 进程管理的数据结构选择

当前使用固定大小的数组 `proc_table[MAX_PROCS]`，这对学习足够了。

更好的方案（可选改进）：
- **就绪队列**：用链表维护 READY 状态的进程，调度器只遍历就绪队列
- **多级反馈队列**：不同优先级的就绪队列，实现更复杂的调度策略
- **等待队列**：每个同步原语（锁、信号量）有自己的等待队列

---

## 8. 调试技巧

### 8.1 检查上下文切换

在 `switch_to` 前后打印关键信息：

```c
printf("switch: %s(pid=%d) -> %s(pid=%d)\n",
       old_proc->name, old_proc->pid,
       new_proc->name, new_proc->pid);
```

### 8.2 栈溢出检测

内核栈很小（4KB），如果函数调用太深或局部变量太大，会溢出。可以在栈底放一个"金丝雀"值：

```c
#define STACK_CANARY 0xDEADBEEFDEADBEEFUL

// 创建进程时
*(unsigned long *)p->kstack = STACK_CANARY;

// 定期检查
void check_stack(struct proc *p) {
    if (*(unsigned long *)p->kstack != STACK_CANARY) {
        panic("Stack overflow detected for process %s!", p->name);
    }
}
```

### 8.3 GDB 调试上下文切换

```
(gdb) break switch_to
(gdb) continue
(gdb) info registers ra sp s0 s1
(gdb) si    # 单步观察寄存器变化
```

---

## 本阶段新增文件清单

```
kernel/proc/
├── proc.c             # 进程创建/销毁
├── proc.h             # PCB 和 context 结构体定义
├── scheduler.c        # Round-Robin 调度器
├── scheduler.h        # 调度器接口
└── switch.S           # 上下文切换

kernel/trap/
└── timer.c            # 阶段 3 已新建，本阶段扩展（timer_init、sbi_set_timer、yield）
```

---

## 本阶段 Makefile 更新

```makefile
# 在上阶段基础上追加：
SRCS_ASM += kernel/proc/switch.S

SRCS_C += kernel/proc/proc.c \
          kernel/proc/scheduler.c
# timer.c 已在阶段 3 加入 SRCS_C
```

同时在 `kernel/main.c` 里把最后的 `while(1){}` 替换为：

```c
proc_init();
proc_create("thread_a", thread_a);
proc_create("thread_b", thread_b);
timer_init();
scheduler();   // 永不返回
```

---

## 检查清单

- [ ] 定义了 `proc` 结构体（PCB）和 `context` 结构体（`proc.h`）
- [ ] `proc.h` 有 header guard、正确的 include、`switch_to` 的 extern 声明
- [ ] 理解了为什么上下文切换只需保存 callee-saved 寄存器
- [ ] 实现了 `switch.S` 中的 `switch_to` 函数
- [ ] 实现了 `kernel/proc/proc.c`（proc_init, proc_create, proc_exit, proc_destroy）
- [ ] 实现了 `kernel/proc/scheduler.c`（scheduler, yield, scheduler_context）
- [ ] 扩展了 `kernel/trap/timer.c`（timer_init、timer_handler 中调用 yield）
- [ ] 多个内核线程能交替运行（先用 yield 手动切换验证）
- [ ] 定时器中断能触发调度（时间片轮转）
- [ ] `kernel/main.c` 中正确 include 了 `proc/proc.h` 和 `proc/scheduler.h`

---

## 下一步

进程管理就绪后，进入 [阶段 6：系统调用与用户态](06-syscall-and-usermode.md)，让程序运行在 U-mode。
