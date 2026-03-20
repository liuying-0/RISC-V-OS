#include "types.h"
#include "riscv.h"
#include "../../include/sbi_call.h"
#include <stddef.h>
#include <stdarg.h>

static void kputc(char c){
    sbi_console_putchar(c);
}

static void kputs(const char *s){
    while(*s) kputc(*s++);
}
static int _vsnprintf(char *out, size_t n, const char *s, va_list vl){
    int format = 0;

    int longarg = 0;

    size_t pos = 0;

    for(; *s; s++){
        if(format){
            switch(*s){
                case 'l':{
                    longarg = 1;
                    break;
                }
                case 'p': {
                    longarg = 1;
                    if (out && pos < n) {
                        out[pos] = '0';
                    }
                    pos++;
                    if (out && pos < n) {
                        out[pos] = 'x';
                    }
                    pos++;
                }
                case 'x': {
                    long num = longarg ? va_arg(vl, long) : va_arg(vl, int);

                    int hexdigits = 2 * (longarg ? sizeof(long) : sizeof(int)) - 1;

                    for (int i = hexdigits; i >= 0; i--) {
                        int d = (num >> (i * 4)) & 0xF;

                        if(out && pos < n){
                            out[pos] = (d < 10 ? '0' + d : 'a' + d - 10);
                        }
                        pos++;
                    }

                    longarg = 0;
                    format = 0;
                    break;
                }
                case 'd': {
                    long num = longarg ? va_arg(vl, long) : va_arg(vl, int);
                    if(num < 0){
                        num = -num;
                        if (out && pos < n) {
                            out[pos] = '-';
                        }
                        pos++;
                    }

                    long digits = 1;
                    for (long nn = num; nn /= 10; digits++){}
                    for (int i = digits - 1; i >= 0; i--) {
                        if (out && pos + i < n) {
                            out[pos + i] = '0' + (num % 10);
                        }
                        num /= 10;
                     }
                     pos += digits;
                     longarg = 0;
                     format = 0;
                     break;
                }
                case 's': {
                    const char *s2 = va_arg(vl, const char *);
                    while(*s2){
                        if(out && pos < n){
                            out[pos] = *s2;
                        }
                        pos++;
                        s2++;
                    }
                    longarg = 0;
                    format = 0;
                    break;
                }
                case 'c': {
                    if (out && pos < n) {
                        out[pos] = (char)va_arg(vl, int);
                    }
                    pos++;
                    longarg = 0;
                    format = 0;
                    break;
                }
                default:
                    break;
            }
        } else if (*s == '%'){
            format = 1;
        } else {
            if (out && pos < n){
                out[pos] = *s;
            }
            pos++;
        }
    }
    if(out && pos < n){
        out[pos] = 0;
    } else if (out && n) {
        out[n-1] = 0;
    }

    return pos;
}

static char out_buf[1000];

static int _vprintf(const char *s, va_list vl){
    int res = _vsnprintf(NULL, -1, s, vl);
    if(res+1 > sizeof(out_buf)){
        kputs("printf buffer overflow!!!\n");
        while(1){}
    }
    _vsnprintf(out_buf, res+1, s, vl);
    kputs(out_buf);
    return res;
}

int printf(const char *s, ...){
    int res = 0;
    va_list vl;
    va_start(vl, s);
    res = _vprintf(s, vl);
    va_end(vl);
    return res;
}

static int panicked = 0;
void panic(const char *s, ...){
    csrc(sstatus, SSTATUS_SIE); // 关闭S-mode全局中断
    if (panicked) {
        while (1){}
    }
    panicked = 1;

    printf("\n!!! KERNEL PANIC !!!\n");

    va_list vl;
    va_start(vl, s);
    _vprintf(s, vl);
    va_end(vl);
    printf("\n");

    printf("  sepc   = 0x%lx\n", csrr(sepc));
    printf("  scause = 0x%lx\n", csrr(scause));
    printf("  stval  = 0x%lx\n", csrr(stval));

    while(1){}

}