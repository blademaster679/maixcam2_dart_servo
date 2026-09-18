#include "drv_pwm.h"
#include "ax_common.h"
#define PWM_CLK_SEL_FREQ_HZ 24000000U
#define PWM_LOADCOUNT_OFF(n) ((n)*0x14U)
#define PWM_LOADCOUNT2_OFF(n) (0xB0U+(n)*4U)
#define PWM_CONTROLREG_OFF(n) (8U+(n)*0x14U)
#define PWM_MODE 0x1EU
static struct {unsigned channel_status;} pwm_status[3];
static uint32_t pwm_base_addr[3];


/* Local extension: append to the verified SDK drv_pwm.c, not a standalone TU.
 * Bench use only: stop/reload/start can truncate an active pulse. Validate
 * transitions on a scope before using this for response-time identification.
 */
int servo_pwm_set_us(pwm_e pwm, uint32_t hz, uint32_t pulse_us)
{
    uint32_t base, ch, cycle, high;
    if (pwm < pwm_10 || pwm > pwm_13 || (hz != 50 && hz != 333)
        || pulse_us < 1400 || pulse_us > 1600) return -1;
    if (!(pwm_status[1].channel_status & (1U << (pwm % 4)))) return -2;
    cycle = PWM_CLK_SEL_FREQ_HZ / hz;
    high = (PWM_CLK_SEL_FREQ_HZ / 1000000U) * pulse_us;
    if (high >= cycle) return -3;
    base = pwm_base_addr[pwm / 4]; ch = pwm % 4;
    pwm_stop(pwm);
    ax_writel(cycle - high, base + PWM_LOADCOUNT_OFF(ch));
    ax_writel(high, base + PWM_LOADCOUNT2_OFF(ch));
    ax_writel(PWM_MODE, base + PWM_CONTROLREG_OFF(ch));
    return pwm_start(pwm);
}

