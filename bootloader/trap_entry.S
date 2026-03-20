.global m_trap_entry

.align 4

m_trap_entry:
    # 在栈上分配 256 字节空间
    # 布局：31 个通用寄存器（x1-x31）各 8 字节 = 248 字节
    #       + mepc 8 字节 = 共 256 字节
    addi sp, sp, -256

    # 保存所有通用寄存器（x0=zero 不需要保存，永远是 0）
    sd ra,    0(sp)   # x1
    sd sp,    8(sp)   # x2（保存的是减去 256 之后的 sp）
    sd gp,   16(sp)   # x3
    sd tp,   24(sp)   # x4
    sd t0,   32(sp)   # x5
    sd t1,   40(sp)   # x6
    sd t2,   48(sp)   # x7
    sd s0,   56(sp)   # x8
    sd s1,   64(sp)   # x9
    sd a0,   72(sp)   # x10
    sd a1,   80(sp)   # x11
    sd a2,   88(sp)   # x12
    sd a3,   96(sp)   # x13
    sd a4,  104(sp)   # x14
    sd a5,  112(sp)   # x15
    sd a6,  120(sp)   # x16
    sd a7,  128(sp)   # x17
    sd s2,  136(sp)   # x18
    sd s3,  144(sp)   # x19
    sd s4,  152(sp)   # x20
    sd s5,  160(sp)   # x21
    sd s6,  168(sp)   # x22
    sd s7,  176(sp)   # x23
    sd s8,  184(sp)   # x24
    sd s9,  192(sp)   # x25
    sd s10, 200(sp)   # x26
    sd s11, 208(sp)   # x27
    sd t3,  216(sp)   # x28
    sd t4,  224(sp)   # x29
    sd t5,  232(sp)   # x30
    sd t6,  240(sp)   # x31

    # 保存 mepc（trap 发生时的 PC，即出问题的指令地址）
    csrr t0, mepc
    sd   t0, 248(sp)

    # 调用 C 语言的 trap 处理函数
    # 参数1（a0）：mcause，告诉 C 函数是什么原因触发了 trap
    # 参数2（a1）：指向栈上 trap_context 结构体的指针（即当前 sp）
    csrr a0, mcause
    mv   a1, sp
    call m_trap_handler

    # 从 trap_context 中恢复 mepc
    ld   t0, 248(sp)
    csrw mepc, t0

    # 恢复所有通用寄存器（sp 最后恢复，否则无法寻址）
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

    # 返回 trap 发生前的地方继续执行
    mret
