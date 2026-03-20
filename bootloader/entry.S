.global _entry

.text
_entry:
    csrr t0, mhartid
    bnez t0,_park_hart

    csrw mie, zero
    csrw mstatus, zero

    
    la sp, _stack_end

    la t1, _bss_start
    la t2, _bss_end
loop:
    bge t1, t2, done
    sd zero, 0(t1)
    addi t1,t1,8
    j loop
done:

    la t0, m_trap_entry
    csrw mtvec, t0

    call main

    j _park_hart


_park_hart:
    wfi
    j _park_hart

.section .bss
.global _stack_start
_stack_start:
    .space 4096

    .global _stack_end
_stack_end:


