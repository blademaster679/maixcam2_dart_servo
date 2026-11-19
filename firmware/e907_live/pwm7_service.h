#ifndef PWM7_SERVICE_H
#define PWM7_SERVICE_H
#include <stdint.h>
#define PWM7_MAGIC 0x50373035U
#define PWM7_DURATION_TICKS (300ULL * 24000000ULL)
/* Only PWM7 and A31 are writable. Timer deltas survive the 179 s wrap. */
typedef struct { uint32_t previous, heartbeat_tick, heartbeat; uint64_t elapsed; int started, finished; } pwm7_state;
static void pwm7_stop(void) { ax_writel(ax_readl(0x06061044)&~1U,0x06061044); }
static void pwm7_step(volatile uint32_t *m,uint32_t now,pwm7_state *s) {
    if(s->finished)return;
    if(!s->started){
        if(m[4]!=1)return;
        ax_writel(1,0x048700c8); /* PWM1 peripheral clock only */
        ax_writel(1U<<26,0x048700b8); /* PWM7 channel clock only */
        pwm7_stop();
        ax_writel((ax_readl(0x02304060)&~(7U<<16))|(3U<<16),0x02304060);
        ax_writel(36072,0x0606103c); ax_writel(36000,0x060610bc);
        ax_writel(0x1e,0x06061044); ax_writel(0x1f,0x06061044);
        s->previous=now;s->heartbeat_tick=now;s->heartbeat=m[5];s->started=1;
        m[6]=1;fence();return;
    }
    s->elapsed+=(uint32_t)(now-s->previous);s->previous=now;
    if(m[5]!=s->heartbeat){s->heartbeat=m[5];s->heartbeat_tick=now;}
    uint32_t done=s->elapsed>=PWM7_DURATION_TICKS?2U:
        (m[4]==2?3U:((uint32_t)(now-s->heartbeat_tick)>=24000000U?4U:0U));
    if(done){pwm7_stop();s->finished=1;m[7]=(uint32_t)s->elapsed;m[8]=(uint32_t)(s->elapsed>>32);fence();m[6]=done;}
}
#endif
