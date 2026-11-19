/* Dedicated RAM-only PWM7 test; A53 sends heartbeat, E907 owns output/timing. */
#include <stdint.h>
static inline void fence(void){__asm__ volatile("fence iorw, iorw" ::: "memory");}
static uint32_t ax_readl(uint32_t a){uint32_t v=*(volatile uint32_t*)a;fence();return v;}
static void ax_writel(uint32_t v,uint32_t a){fence();*(volatile uint32_t*)a=v;fence();}
#include "pwm7_service.h"
void live_main(volatile uint32_t *m){
    pwm7_state s={0};fence();m[0]=PWM7_MAGIC;
    for(;;)pwm7_step(m,ax_readl(0x04820000),&s);
}
