static inline int sbi_call(long eid, long fid, long arg0, long arg1, long arg2){
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a6 asm("a6") = fid;
    register long a7 asm("a7") = eid;

    asm volatile(
        "ecall" 
        : "+r"(a0) 
        : "r"(a1), "r"(a2), "r"(a6), "r"(a7) 
        : "memory"
    );
    return a0;
}

#define sbi_console_putchar(c) sbi_call(0x01, 0, (c), 0, 0)
#define sbi_console_getchar() sbi_call(0x02,0 ,0, 0, 0)
#define sbi_set_timer(t) sbi_call(0x00, 0, (t), 0, 0)
#define sbi_shutdown() sbi_call(0x08, 0, 0, 0, 0)