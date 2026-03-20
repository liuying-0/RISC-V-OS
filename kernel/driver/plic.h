#ifndef __PLIC_C__
#define __PLIC_C__

void plic_init(void);
void plic_init_hart(void);
int plic_claim(void);
void plic_complete(int irq);

#endif /* __PLIC_H__ */