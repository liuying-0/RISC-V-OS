extern void uart_init(void);
extern int uart_putc(char ch);
extern void uart_puts(char *str);
extern int uart_getc(void);
extern void uart_intr(void);