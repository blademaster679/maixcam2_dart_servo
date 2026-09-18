
/* Local extension: append to the verified SDK drv_pwm.c, not a standalone TU.
 * Bench use only: stop/reload/start can truncate an active pulse. Validate
 * transitions on a scope before using this for response-time identification.
 */
int servo_pwm_set_us(pwm_e pwm, uint32_t hz, uint32_t pulse_us)
{
    uint32_t base, ch, cycle, high;
    int rc;
    if (pwm < pwm_10 || pwm > pwm_13 || (hz != 50 && hz != 333)
        || pulse_us < 1400 || pulse_us > 1600) return -1;
    if (!(pwm_status[1].channel_status & (1U << (pwm % 4)))) return -2;
    cycle = PWM_CLK_SEL_FREQ_HZ / hz;
    high = (PWM_CLK_SEL_FREQ_HZ / 1000000U) * pulse_us;
    if (high >= cycle) return -3;
    base = pwm_base_addr[pwm / 4]; ch = pwm % 4;
    /* Avoid restarting an already enabled channel for an unchanged command.
     * Read hardware rather than a cache that would survive pwm_deinit(). */
    if (ax_readl(base + PWM_CONTROLREG_OFF(ch)) == (PWM_MODE | 1U)
        && ax_readl(base + PWM_LOADCOUNT_OFF(ch)) == cycle - high
        && ax_readl(base + PWM_LOADCOUNT2_OFF(ch)) == high) return 0;
    rc = pwm_stop(pwm);
    if (rc) return rc;
    ax_writel(cycle - high, base + PWM_LOADCOUNT_OFF(ch));
    ax_writel(high, base + PWM_LOADCOUNT2_OFF(ch));
    ax_writel(PWM_MODE, base + PWM_CONTROLREG_OFF(ch));
    return pwm_start(pwm);
}
