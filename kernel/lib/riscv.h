#ifndef __RISCV_H__
#define __RISCV_H__

#define csrr(csr) ({                                       \
    unsigned long __val;                                    \
    asm volatile("csrr %0, " #csr : "=r"(__val));          \
    __val;                                                  \
})
#define csrw(csr, val) asm volatile("csrw " #csr ", %0" :: "r"((unsigned long)(val)));
#define csrs(csr, val) asm volatile("csrs " #csr ", %0" :: "r"((unsigned long)(val)));
#define csrc(csr, val) asm volatile("csrc " #csr ", %0" :: "r"((unsigned long)(val)));

// mstatus 寄存器位域
#define MSTATUS_MPP_MASK (3UL << 11) // bit[12:11] MPP 掩码，用于清除 MPP 字段
#define MSTATUS_MPP_M    (3UL << 11) // MPP = 11，mret 后回到 M-mode
#define MSTATUS_MPP_S    (1UL << 11) // MPP = 01，mret 后回到 S-mode
#define MSTATUS_MPP_U    (0UL << 11) // MPP = 00，mret 后回到 U-mode
#define MSTATUS_MIE      (1UL << 3)  // bit[3] M-mode 全局中断使能
#define MSTATUS_MPIE     (1UL << 7)  // bit[7] trap 前的 MIE 值，mret 时恢复到 MIE

// sstatus 寄存器位域（mstatus 的 S-mode 可见子集）
#define SSTATUS_SPP      (1UL << 8)  // bit[8] trap 前的特权级（0=U, 1=S），sret 时恢复
#define SSTATUS_SIE      (1UL << 1)  // bit[1] S-mode 全局中断使能
#define SSTATUS_SPIE     (1UL << 5)  // bit[5] trap 前的 SIE 值，sret 时恢复到 SIE

// sie 寄存器位域（S-mode 各类中断单独使能）
#define SIE_SSIE         (1UL << 1)  // bit[1] S-mode 软件中断使能
#define SIE_STIE         (1UL << 5)  // bit[5] S-mode 定时器中断使能
#define SIE_SEIE         (1UL << 9)  // bit[9] S-mode 外部中断使能

#endif /* __RISCV_H__ */