#include "plic.h"
#include "../lib/types.h"

#define PLIC_BASE 0x0C000000UL

#define PLIC_PRIORITY(irq) (PLIC_BASE + (irq) *4)
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart) * 0x2000)

#define UART0_IRQ 10

void plic_init(void){
    *(volatile uint32_t *)PLIC_PRIORITY(UART0_IRQ) = 1;
}

void plic_init_hart(void){
    int hart = 0;
    *(volatile uint32_t *)PLIC_SENABLE(hart) = (1 << UART0_IRQ);
    *(volatile uint32_t *)PLIC_SPRIORITY(hart) = 0;
}

int plic_claim(void){
    int hart = 0;
    return *(volatile uint32_t *)PLIC_SCLAIM(hart);
}

void plic_complete(int irq) {
    int hart = 0;
    *(volatile uint32_t *)PLIC_SCLAIM(hart) = irq;
}