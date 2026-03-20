typedef unsigned long pte_t;
typedef unsigned long *pagetable_t;

#define PTE_V (1 << 0) // 有效位
#define PTE_R (1 << 1) // 可读属性
#define PTE_W (1 << 2) // 可写属性
#define PTE_X (1 << 3) // 可执行熟悉
#define PTE_U (1 << 4) // 用户访问模式
#define PTE_G (1 << 5) // 全局映射
#define PTE_A (1 << 6) // 访问标记位
#define PTE_D (1 << 7) // 脏位

// 从 PTE 中提取物理页号
#define PTE2PA(pte) (((pte) >> 10) << 12)

// 从物理地址生成 PTE（不含标志位）
#define PA2PTE(pa) ((((unsigned long)(pa)) >> 12) << 10)

// 从虚拟地址中提取 VPN[level]
#define VA_VPN(va, level) (((va) >> (12 + 9 * (level))) & 0x1FF)

// 创建新页表（返回物理地址）
pagetable_t pagetable_create(void); 

// 映射：将虚拟地址 va 映射到物理地址 pa，权限为 flags
// size 可以是 PAGE_SIZE 的整数倍（映射多个页）
int pagetable_map(pagetable_t pt, unsigned long va, unsigned long pa, unsigned long size, int flags);

// 取消映射：删除虚拟地址 va 的映射，size 是页数
void pagetable_unmap(pagetable_t pt, unsigned long va, unsigned long size, int do_free);

// 递归释放整棵页表树（调用前须先 unmap 所有叶子映射）
void pagetable_free(pagetable_t pt);

// 查找虚拟地址对应的 PTE（如果不存在，alloc=1 则创建中间页表）
pte_t *pagetable_walk(pagetable_t pt, unsigned long va, int alloc);