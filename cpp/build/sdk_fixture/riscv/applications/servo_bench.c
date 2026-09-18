/* E907 / RT-Thread bench firmware, official AX630C SDK integration.
 * Console must be relocated away from UART1 first (A30/A31 are PWM6/7).
 * No automatic startup, no fabricated position feedback.
 */
#include <rtthread.h>
#include <finsh.h>
#include <stdint.h>
#include <string.h>
#include "drv_pwm.h"
#include "ax_common.h"
extern int servo_pwm_set_us(pwm_e pwm, uint32_t hz, uint32_t pulse_us);
static volatile int running, stop_requested;
static const uint32_t pads[4] = {0x02304084,0x02304090,0x02304054,0x02304060};
static uint32_t saved[4];
static unsigned seq;

static int set_pulse(int i, int pulse)
{
    unsigned before = (unsigned)rt_tick_get();
    int rc = servo_pwm_set_us((pwm_e)(4+i),333,(uint32_t)pulse);
    unsigned after = (unsigned)rt_tick_get();
    rt_kprintf("SERVO,%u,%d,%d,%d,333,%u,%u,%d\n",++seq,i+1,4+i,pulse,before,after,rc);
    return rc;
}
static void dwell(unsigned ms)
{
    unsigned elapsed;
    for (elapsed=0; elapsed<ms && !stop_requested; elapsed+=10)
        rt_thread_mdelay(10);
}
static void worker(void *arg)
{
    const int steps[] = {0,22,0,-22,0,55,0,-55,0};
    int i,rep,s,owned=0,muxed=0,failed=1;
    (void)arg;
    rt_kprintf("SERVO_META,tick_hz,%u\n",(unsigned)RT_TICK_PER_SECOND);
    for (i=0;i<4;i++) {
        if (pwm_init((pwm_e)(4+i),333,50)) goto cleanup;
        owned++;
        saved[i]=ax_readl(pads[i]);
        ax_writel((saved[i]&~(7U<<16))|(3U<<16),pads[i]);
        muxed++;
        if (set_pulse(i,1500)) goto cleanup;
    }
    failed=0;
    dwell(3000);
    for (rep=0;rep<3 && !stop_requested;rep++)
        for (i=0;i<4 && !stop_requested;i++)
            for (s=0;s<9 && !stop_requested;s++) {
                if (set_pulse(i,1500+steps[s])) {failed=1;goto cleanup;}
                dwell(1500);
            }
cleanup:
    for (i=0;i<owned;i++) {pwm_stop((pwm_e)(4+i));pwm_deinit((pwm_e)(4+i));}
    for (i=0;i<muxed;i++) ax_writel(saved[i],pads[i]);
    rt_kprintf("SERVO_END,%u,stop_requested,%d,failed,%d\n",seq,stop_requested,failed);
    running=0;
}
static void servo_bench(int argc,char **argv)
{
    rt_thread_t thread;
    if (argc==2 && !strcmp(argv[1],"stop")) {stop_requested=1;return;}
    if (argc!=2 || strcmp(argv[1],"start")) {
        rt_kprintf("servo_bench start|stop\n");return;
    }
    if (!strcmp(RT_CONSOLE_DEVICE_NAME,"uart1")) {
        rt_kprintf("REFUSED: relocate UART1 console before using A30/A31 PWM.\n");return;
    }
    if (running) {rt_kprintf("Already running\n");return;}
    running=1;stop_requested=0;seq=0;
    thread=rt_thread_create("srvbench",worker,RT_NULL,4096,20,10);
    if (!thread) {running=0;rt_kprintf("Thread allocation failed\n");return;}
    if (rt_thread_startup(thread)!=RT_EOK) {
        rt_thread_delete(thread);running=0;rt_kprintf("Thread start failed\n");
    }
}
MSH_CMD_EXPORT(servo_bench, Four servo bench test with command telemetry);
