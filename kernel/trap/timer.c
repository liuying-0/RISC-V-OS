#include "../lib/printf.h"
#include "../proc/scheduler.h"
#include "../../include/sbi_call.h"
#include "trap.h"

static unsigned long ticks = 0;
#define TIMER_INTERVAL 10000000

static unsigned long rdtime(void){
    unsigned long t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

void timer_init(void) {
    sbi_set_timer(rdtime() + TIMER_INTERVAL);
}

void timer_handler(void) {
    ticks++;
    sbi_set_timer(rdtime() + TIMER_INTERVAL);
    yield();
}