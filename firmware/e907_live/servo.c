/* RAM-only E907 PWM service, official AX630C PWM register layout.
 * No RT-Thread, UART, IRQ, heap or libc. Shared-memory command ABI v1.
 * Loader must exclusively lease PWM1 and preserve/restore its MMIO state.
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
static const uint32_t pads[4]={0x02304084,0x02304090,0x02304054,0x02304060};
static void stop_all(void){unsigned i;for(i=0;i<4;i++)pwm_stop((pwm_e)(4+i));}
static void snapshot(volatile uint32_t *m){unsigned i;for(i=0;i<4;i++){
 m[20+3*i]=ax_readl(0x06061000+PWM_CONTROLREG_OFF(i));
 m[21+3*i]=ax_readl(0x06061000+PWM_LOADCOUNT_OFF(i));
 m[22+3*i]=ax_readl(0x06061000+PWM_LOADCOUNT2_OFF(i));
}}
void live_main(volatile uint32_t *m){
 uint32_t last=0,active=0;int initialized=0;
 m[33]=1;fence();m[0]=0x53525631U; /* SRV1 */
 for(;;){
  uint32_t now=ax_readl(0x04820000); /* official timer64, 24 MHz, read only */
  m[19]=now;
  if(active&&(uint32_t)(now-last)>=24000000U){stop_all();active=0;m[17]=0;m[18]=2;m[32]++;snapshot(m);}
  uint32_t seq=m[5];if(seq==m[6])continue;fence();
  uint32_t cmd=m[4],mask=m[12];int result=0;unsigned i;
  if(cmd==1){result=(int)~m[7];}
  else if(cmd==2){
   if(m[34]!=0x50574D31U||!mask||(mask&~15U))result=-20;
   for(i=0;i<4;i++)if((mask&(1U<<i))&&(m[13+i]<1400||m[13+i]>1600))result=-21;
   if(!result){
    if(!initialized){
     ax_writel(1U,0x048700c8); /* PWM1 PCLK */
     ax_writel(15U<<23,0x048700b8); /* PWM4..7 clocks */
     stop_all();
     for(i=0;i<4;i++)ax_writel((ax_readl(pads[i])&~(7U<<16))|(3U<<16),pads[i]);
     pwm_status[1].channel_status=15;initialized=1;
    }
    for(i=0;i<4;i++)if(mask&(1U<<i)){
     int r=servo_pwm_set_us((pwm_e)(4+i),333,m[13+i]);if(r)result=r;
    }
    if(result){stop_all();active=0;m[18]=3;}
    else{active|=mask;last=now;m[18]=1;}
   }
  }
  else if(cmd==3||cmd==5){if(initialized)stop_all();active=0;m[18]=0;}
  else if(cmd!=4){result=-22;}
  if(initialized)snapshot(m);
  m[17]=active;m[8]=(uint32_t)result;fence();m[6]=seq;
  if(cmd==5)for(;;)__asm__ volatile("nop");
 }
}
