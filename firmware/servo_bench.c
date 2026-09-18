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
static unsigned selected_mask = 15U;

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
    int i,rep,s,failed=0;
    unsigned owned=0,muxed=0;
    (void)arg;
    rt_kprintf("SERVO_META,tick_hz,%u\n",(unsigned)RT_TICK_PER_SECOND);
    for (i=0;i<4;i++) {
        int rc;
        if (stop_requested) goto cleanup;
        if (!(selected_mask & (1U << i))) continue;
        rc=pwm_init((pwm_e)(4+i),333,50);
        if (rc) {
            rt_kprintf("SERVO_ERROR,init,%d,%d\n",i+1,rc);
            failed=1;goto cleanup;
        }
        owned |= 1U << i;
        saved[i]=ax_readl(pads[i]);
        ax_writel((saved[i]&~(7U<<16))|(3U<<16),pads[i]);
        muxed |= 1U << i;
        if (stop_requested) goto cleanup;
        if (set_pulse(i,1500)) {failed=1;goto cleanup;}
    }
    failed=0;
    dwell(3000);
    for (rep=0;rep<3 && !stop_requested;rep++)
        for (i=0;i<4 && !stop_requested;i++) {
            if (!(selected_mask & (1U << i))) continue;
            for (s=0;s<9 && !stop_requested;s++) {
                if (set_pulse(i,1500+steps[s])) {failed=1;goto cleanup;}
                dwell(1500);
            }
        }
cleanup:
    for (i=0;i<4;i++) if (owned & (1U << i)) {
        int stop_rc=pwm_stop((pwm_e)(4+i));
        int deinit_rc=pwm_deinit((pwm_e)(4+i));
        if (stop_rc || deinit_rc) {
            failed=1;
            rt_kprintf("SERVO_ERROR,cleanup,%d,%d,%d\n",i+1,stop_rc,deinit_rc);
        }
    }
    for (i=0;i<4;i++) if (muxed & (1U << i)) ax_writel(saved[i],pads[i]);
    rt_kprintf("SERVO_END,%u,stop_requested,%d,failed,%d\n",seq,stop_requested,failed);
    running=0;
}
static void servo_bench(int argc,char **argv)
{
    rt_thread_t thread;
    unsigned mask=15U;
    if (argc==2 && !strcmp(argv[1],"stop")) {stop_requested=1;return;}
    if (argc==2 && !strcmp(argv[1],"status")) {
        rt_kprintf("SERVO_STATUS,running,%d,stop_requested,%d,mask,%u,seq,%u,console,%s\n",
                   running,stop_requested,selected_mask,seq,RT_CONSOLE_DEVICE_NAME);
        return;
    }
    if ((argc!=2 && argc!=3) || strcmp(argv[1],"start")) {
        rt_kprintf("servo_bench start [1|2|3|4|all]|stop|status\n");return;
    }
    if (argc==3 && strcmp(argv[2],"all")) {
        if (strlen(argv[2])!=1 || argv[2][0]<'1' || argv[2][0]>'4') {
            rt_kprintf("Invalid servo: use 1..4 or all\n");return;
        }
        mask=1U << (argv[2][0]-'1');
    }
    if ((mask & 12U) && !strcmp(RT_CONSOLE_DEVICE_NAME,"uart1")) {
        rt_kprintf("REFUSED: relocate UART1 console before using A30/A31 PWM.\n");return;
    }
    if ((mask & 3U) && !strcmp(RT_CONSOLE_DEVICE_NAME,"uart3")) {
        rt_kprintf("REFUSED: relocate UART3 console before using B2/B3 PWM.\n");return;
    }
    if (running) {rt_kprintf("Already running\n");return;}
    selected_mask=mask;running=1;stop_requested=0;seq=0;
    thread=rt_thread_create("srvbench",worker,RT_NULL,4096,20,10);
    if (!thread) {running=0;rt_kprintf("Thread allocation failed\n");return;}
    if (rt_thread_startup(thread)!=RT_EOK) {
        rt_thread_delete(thread);running=0;rt_kprintf("Thread start failed\n");
    }
}
MSH_CMD_EXPORT(servo_bench, Four servo bench test with command telemetry);
