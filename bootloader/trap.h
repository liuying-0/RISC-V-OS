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
