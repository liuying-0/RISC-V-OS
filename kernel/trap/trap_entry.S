# ---- S-mode trap handler (kernel text) ----
.text
.global _s_trap_from_kernel
.type _s_trap_from_kernel, @function
_s_trap_from_kernel:
    csrr t6, sscratch

    addi sp, sp, -256

    sd ra, 0(sp)
    sd gp, 16(sp)
    sd tp, 24(sp)
    sd t0, 32(sp)
    sd t1, 40(sp)
    sd t2, 48(sp)
    sd s0, 56(sp)
    sd s1, 64(sp)
    sd a0, 72(sp)
    sd a1, 80(sp)
    sd a2, 88(sp)
    sd a3, 96(sp)
    sd a4, 104(sp)
    sd a5, 112(sp)
    sd a6, 120(sp)
    sd a7, 128(sp)
    sd s2, 136(sp)
    sd s3, 144(sp)
    sd s4, 152(sp)
    sd s5, 160(sp)
    sd s6, 168(sp)
    sd s7, 176(sp)
    sd s8, 184(sp)
    sd s9, 192(sp)
    sd s10, 200(sp)
    sd s11, 208(sp)
    sd t3, 216(sp)
    sd t4, 224(sp)
    sd t5, 232(sp)
    sd t6, 240(sp)

    addi t0, sp, 256
    sd t0, 8(sp)

    csrr t0, sepc
    sd t0, 248(sp)

    csrr a0, scause
    mv a1, sp
    call s_trap_handler

    ld t0, 248(sp)
    csrw sepc, t0

    ld ra, 0(sp)
    ld gp, 16(sp)
    ld tp, 24(sp)
    ld t0, 32(sp)
    ld t1, 40(sp)
    ld t2, 48(sp)
    ld s0, 56(sp)
    ld s1, 64(sp)
    ld a0, 72(sp)
    ld a1, 80(sp)
    ld a2, 88(sp)
    ld a3, 96(sp)
    ld a4, 104(sp)
    ld a5, 112(sp)
    ld a6, 120(sp)
    ld a7, 128(sp)
    ld s2, 136(sp)
    ld s3, 144(sp)
    ld s4, 152(sp)
    ld s5, 160(sp)
    ld s6, 168(sp)
    ld s7, 176(sp)
    ld s8, 184(sp)
    ld s9, 192(sp)
    ld s10, 200(sp)
    ld s11, 208(sp)
    ld t3, 216(sp)
    ld t4, 224(sp)
    ld t5, 232(sp)
    ld t6, 240(sp)

    addi sp, sp, 256

    sret


# ---- Trampoline page (U-mode trap entry / return) ----
.section .trampoline, "ax"
.global s_trap_entry
.type s_trap_entry, @function
.align 4
s_trap_entry:
    # sscratch 交换后：t6 = trapframe 指针
    csrrw t6, sscratch, t6

    # t6 == 0 → 来自 S-mode（不应该发生，但保险处理）
    beqz t6, _trampoline_from_smode

_trampoline_from_umode:
    # ---- 保存用户寄存器到 trapframe ----
    sd ra, 0(t6)
    sd sp, 8(t6)
    sd gp, 16(t6)
    sd tp, 24(t6)
    sd t0, 32(t6)
    sd t1, 40(t6)
    sd t2, 48(t6)
    sd s0, 56(t6)
    sd s1, 64(t6)
    sd a0, 72(t6)
    sd a1, 80(t6)
    sd a2, 88(t6)
    sd a3, 96(t6)
    sd a4, 104(t6)
    sd a5, 112(t6)
    sd a6, 120(t6)
    sd a7, 128(t6)
    sd s2, 136(t6)
    sd s3, 144(t6)
    sd s4, 152(t6)
    sd s5, 160(t6)
    sd s6, 168(t6)
    sd s7, 176(t6)
    sd s8, 184(t6)
    sd s9, 192(t6)
    sd s10, 200(t6)
    sd s11, 208(t6)
    sd t3, 216(t6)
    sd t4, 224(t6)
    sd t5, 232(t6)

    csrr t0, sscratch
    sd t0, 240(t6)

    csrr t0, sepc
    sd t0, 248(t6)

    # 切换到内核栈
    ld sp, 256(t6)

    # 清零 sscratch，表示现在处于 S-mode
    csrw sscratch, zero

    # 把 trapframe 指针压栈，调用 usertrap
    addi sp, sp, -16
    sd t6, 0(sp)

    csrr a0, scause
    mv a1, t6
    call usertrap

    # usertrap 返回到这里（实际通过 s_trap_return 回来）
    ld a0, 0(sp)
    addi sp, sp, 16

    # ---- 恢复用户寄存器（从 trapframe） ----
    # a0 传入 trapframe 指针
    .global s_trap_return
    .type s_trap_return, @function
s_trap_return:
    # 切换到用户页表（sret 必须在用户页表下执行）
    csrw satp, a1
    sfence.vma zero, zero

    ld t0, 0(a0)
    csrw sepc, t0

    csrw sscratch, a0

    ld ra, 0(a0)
    ld sp, 8(a0)
    ld gp, 16(a0)
    ld tp, 24(a0)
    ld t0, 32(a0)
    ld t1, 40(a0)
    ld t2, 48(a0)
    ld s0, 56(a0)
    ld s1, 64(a0)
    ld a1, 80(a0)
    ld a2, 88(a0)
    ld a3, 96(a0)
    ld a4, 104(a0)
    ld a5, 112(a0)
    ld a6, 120(a0)
    ld a7, 128(a0)
    ld s2, 136(a0)
    ld s3, 144(a0)
    ld s4, 152(a0)
    ld s5, 160(a0)
    ld s6, 168(a0)
    ld s7, 176(a0)
    ld s8, 184(a0)
    ld s9, 192(a0)
    ld s10, 200(a0)
    ld s11, 208(a0)
    ld t3, 216(a0)
    ld t4, 224(a0)
    ld t5, 232(a0)
    ld t6, 240(a0)
    ld a0, 72(a0)

    sret

_trampoline_from_smode:
    # 来自 S-mode，sscratch == 0，直接跳到 kernel trap handler
    csrr a0, scause
    mv a1, sp
    call s_trap_handler
    sret
