#include "proc.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "elf.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "memlayout.h"
#include "../trap/trap.h"

extern void s_trap_entry(void);
extern void usertrapret(void);
extern void yield(void);

struct proc proc_table[MAX_PROC];
static int next_pid = 1;


void proc_init(void){
    memset(proc_table, 0, sizeof(proc_table));
}

struct proc *proc_create(const char * name, void (*entry)(void)){
    struct proc *p = NULL;
    for(int i = 0; i < MAX_PROC; i++){
        if(proc_table[i].state == UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if(!p) return NULL;

    p->kstack = (unsigned long) pmm_alloc();
    if (!p->kstack) return NULL;

    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (unsigned long) entry;
    p->context.sp = p->kstack + PAGE_SIZE;

    p->pagetable = NULL; 
    p->trapframe = NULL;

    p->pid = next_pid++;
    p->state = READY;
    strncpy(p->name, name, sizeof(p->name));
    return p;
}

struct proc *user_proc_create(const char *name, const unsigned char *elf_data, unsigned long elf_size) {
    struct proc *p = NULL;
    for (int i = 0; i < MAX_PROC; i++) {
        if (proc_table[i].state == UNUSED) {
            p = &proc_table[i];
            break;
        }
    }
    if (!p) return NULL;

    // 1. 分配内核栈
    p->kstack = (unsigned long)pmm_alloc();
    if (!p->kstack) return NULL;

    // 2. 创建用户页表（仅包含用户区域和 trampoline，不映射内核）
    // 用户态无法访问内核虚拟地址（0xFFFFFFFFC0000000 以上）
    p->pagetable = user_pagetable_create();
    if (!p->pagetable) {
    pmm_free((void *)p->kstack);
    return NULL;
    }

    // 3. 分配 trapframe 页
    p->trapframe = (struct trap_context *)pmm_alloc();
    if (!p->trapframe) {
    pagetable_free(p->pagetable);
    pmm_free((void *)p->kstack);
    return NULL;
    }
    memset(p->trapframe, 0, sizeof(struct trap_context));

    // 4. 加载 ELF
    unsigned long entry_addr = elf_load(p->pagetable, elf_data, elf_size, &p->sz);
    if (entry_addr == 0) {
    printf("user_proc_create: elf_load failed\n");
    pmm_free(p->trapframe);
    pagetable_free(p->pagetable);
    pmm_free((void *)p->kstack);
    return NULL;
    }

    // 5. 初始化堆
    // heap_end = ELF 加载后的最大地址（= p->sz），brk() 在此基础上推进
    // 实际物理页在 page fault 时按需分配（lazy allocation）
    p->heap_end = p->sz;

    // 6. 分配用户栈（guard page + 4 页）
    unsigned long stack_guard = PGROUNDUP(p->sz);
    unsigned long stack_bottom = stack_guard + PAGE_SIZE;  // guard page 不映射
    for (int i = 0; i < 4; i++) {
    void *pa = pmm_alloc();
    if (!pa) {
    panic("user_proc_create: out of memory for user stack");
    }
    pagetable_map(p->pagetable, stack_bottom + i * PAGE_SIZE,
    (unsigned long)pa, PAGE_SIZE,
    PTE_R | PTE_W | PTE_U);
    }
    unsigned long stack_top = stack_bottom + 4 * PAGE_SIZE;
    // 栈顶是用户地址空间的最高地址，更新 sz
    p->sz = stack_top;

    // 7. 设置 trapframe
    p->trapframe->sepc = entry_addr;              // 用户程序入口（U-mode 虚拟地址）
    p->trapframe->sp = stack_top;                 // 用户栈顶
    p->trapframe->kernel_sp = p->kstack + PAGE_SIZE; // 内核栈顶
    p->trapframe->kernel_satp = (8UL << 60) |    // 内核页表（切换到用户页表前保存）
    ((unsigned long)kernel_pagetable >> 12);
    p->trapframe->trap_handler = (unsigned long)s_trap_entry; // trampoline 入口

    // 8. 设置内核上下文
    // 首次调度时，switch_to 恢复 context 后 ret 跳到 usertrapret
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (unsigned long)usertrapret;   // 关键：首次运行走 usertrapret
    p->context.sp = p->kstack + PAGE_SIZE;         // 内核栈顶

    p->pid = next_pid++;
    p->state = READY;
    strncpy(p->name, name, sizeof(p->name));

    printf("[kernel] created user process '%s' (pid=%d, entry=0x%lx)\n",
    p->name, p->pid, entry_addr);
    return p;
}

void proc_exit(int status) {
    struct proc *p = current_proc;
    if (!p) return;
    (void)status;
    p->state = ZOMBIE;
    extern void yield(void);
    yield();
    panic("proc_exit returned");
}

void proc_destroy(struct proc *p) {
    if (!p || p->state == UNUSED) return;
    if (p->trapframe) {
        pmm_free(p->trapframe);
        p->trapframe = NULL;
    }
    if (p->pagetable) {
        pagetable_free(p->pagetable);
        p->pagetable = NULL;
    }
    if (p->kstack) {
        pmm_free((void *)p->kstack);
        p->kstack = 0;
    }
    memset(p, 0, sizeof(*p));
}