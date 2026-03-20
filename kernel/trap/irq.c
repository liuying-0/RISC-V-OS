#include "../driver/uart.h"
#include "../driver/plic.h"
#include "../lib/printf.h"

#define UART0_IRQ 10

void external_irq_handler(void) {
    int irq = plic_claim();

    switch (irq) {
        case UART0_IRQ:
            uart_intr();
            break;
        case 0: // 没有中断待处理
            break;
        default:
            printf("Unexpected external interrupt: irq=%d\n", irq);
            break;
    }
    if (irq) plic_complete(irq);
}