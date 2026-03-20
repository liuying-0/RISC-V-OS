#ifndef __TYPES_H__
#define __TYPES_H__

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned  long uint64_t;

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long int64_t;

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t ptrdiff_t;

typedef uint64_t uintptr_t;
typedef int64_t intptr_t;

#define NULL ((void *)0)
#define true 1
#define false 0
typedef int bool;

typedef uint64_t reg_t;

#endif /* __TYPES_H__ */