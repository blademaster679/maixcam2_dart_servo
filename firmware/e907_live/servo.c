/* RAM-only E907 PWM service, official AX630C PWM register layout.
 * No RT-Thread, UART, IRQ, heap or libc. Shared-memory command ABI v2 for carrier V1.1.
 * Loader must exclusively lease PWM0/PWM1 and preserve/restore its MMIO state.
 */
#include <stdint.h>
#include "../../research/riscv__drivers__pwm__drv_pwm.h"
#include "../../research/riscv__drivers__pwm__drv_pwm_reg.h"
static inline void fence(void){__asm__ volatile("fence iorw, iorw" ::: "memory");}
static uint32_t ax_readl(uint32_t address){uint32_t v=*(volatile uint32_t*)address;fence();return v;}
static void ax_writel(uint32_t v,uint32_t address){fence();*(volatile uint32_t*)address=v;fence();}
static const uint32_t pwm_base_addr[3]={0x06060000,0x06061000,0x06062000};
static pwm_status_t pwm_status[3];
int pwm_stop(pwm_e p){uint32_t a=pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4);ax_writel(ax_readl(a)&~PWM_EN,a);return 0;}
int pwm_start(pwm_e p){uint32_t a=pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4);ax_writel(ax_readl(a)|PWM_EN,a);return 0;}
#include "../pwm_us_extension.c.inc"
#include "servo_service.h"
void live_main(volatile uint32_t *m){
    servo_service_state state={0,0,0};
    m[33]=SERVO_ABI_VERSION;fence();m[0]=SERVO_MAGIC;
    for(;;)if(servo_service_step(m,ax_readl(0x04820000),&state))
        for(;;)__asm__ volatile("nop");
}
