.global _start

_start:
    call main

    li a0, 0
    li a7, 1 
    ecall

loop:
    j loop