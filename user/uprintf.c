#include "user.h"

static void putc(char c) {
    write(1, &c, 1);
}

static void puts(const char *s) {
    if (!s) s = "(null)";
    while (*s) putc(*s++);
}

static void printint(long x, int base, int is_signed) {
    char buf[20];
    int i = 0;
    int neg = 0;
    unsigned long ux;

    if (is_signed && x < 0) {
        neg = 1;
        ux = -x;
    } else {
        ux = x;
    }

    do {
        int d = ux % base;
        buf[i++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        ux /= base;
    } while (ux);

    if (neg) putc('-');
    while (i > 0) putc(buf[--i]);
}

int printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') {
            putc(fmt[i]);
            continue;
        }
        i++;
        switch (fmt[i]) {
            case 'd':
                printint(va_arg(ap, int), 10, 1);
                break;
            case 'x':
                printint(va_arg(ap, unsigned long), 16, 0);
                break;
            case 'p':
                puts("0x");
                printint(va_arg(ap, unsigned long), 16, 0);
                break;
            case 's':
                puts(va_arg(ap, const char *));
                break;
            case 'c':
                putc(va_arg(ap, int));
                break;
            case '%':
                putc('%');
                break;
            default:
                putc('%');
                putc(fmt[i]);
                break;
        }
    }

    va_end(ap);
    return 0;
}