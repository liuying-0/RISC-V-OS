#ifndef __SBI_H__
#define __SBI_H__

#include "trap.h"

void sbi_handler(struct trap_context *ctx);

#endif /* __SBI_H__ */
