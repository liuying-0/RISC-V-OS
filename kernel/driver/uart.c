#include "../lib/types.h"

#define UART0_BASE 0x10000000UL

#define UART_REG(reg) ((volatile uint8_t *)(UART0_BASE + reg))

#define RHR 0
#define THR 0
#define DLL 0
#define IER 1
#define DLM 1
#define FCR 2
#define ISR 2
#define LCR 3
#define MCR 4
#define LSR 5
#define MSR 6
#define SPR 7

#define LSR_RX_READY (1 << 0)
#define LSR_TX_IDLE (1 << 5)

#define uart_read_reg(reg) (*(UART_REG(reg)))
#define uart_write_reg(reg, value) (*(UART_REG(reg)) = (value))

void uart_init(void) {
    uart_write_reg(IER, 0x00);

    uint8_t lcr = uart_read_reg(LCR);
    uart_write_reg(LCR, lcr | (1 << 7));
    uart_write_reg(DLL, 0x03);
    uart_write_reg(DLM, 0x00);

    lcr = 0;

    uart_write_reg(LCR, lcr | (3 << 0));

    uint8_t ier = uart_read_reg(IER);
    uart_write_reg(IER, ier | (1 << 0));
}

int uart_putc(char ch){
    while((uart_read_reg(LSR) & LSR_TX_IDLE) == 0);
    return uart_write_reg(THR, ch);
}

void uart_puts(char *str){
    while(*str){
        uart_putc(*str++);
    }
}

int uart_getc(void){
    if (uart_read_reg(LSR) & LSR_RX_READY)
        return uart_read_reg(RHR);
    return -1;
}

void uart_intr(void) {
    int c;
    while ((c = uart_getc()) != -1){
        uart_putc((char)c);
    }
}