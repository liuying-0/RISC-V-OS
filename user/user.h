#ifndef __USER_H__
#define __USER_H__

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

#define NULL ((void *)0)

void exit(int status);
int write(int fd, const void *buf, int len);
int printf(const char *fmt, ...);

#endif