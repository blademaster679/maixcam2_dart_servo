#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drv_pwm.h"
#include "ax_common.h"
#include "../../research/riscv__drivers__pwm__drv_pwm_reg.h"
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static pwm_status_t pwm_status[3];
static uint32_t pwm_base_addr[3]={0,256,512},regs[256];
static unsigned writes,starts,stops;static int stop_fail,start_fail;
uint32_t ax_readl(uint32_t a){CHECK(a/4<256);return regs[a/4];}
void ax_writel(uint32_t v,uint32_t a){CHECK(a/4<256);regs[a/4]=v;writes++;}
int pwm_stop(pwm_e p){stops++;if(stop_fail)return -7;regs[(pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4))/4]&=~1U;return 0;}
int pwm_start(pwm_e p){starts++;if(start_fail)return -8;regs[(pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4))/4]|=1U;return 0;}
#include "../../firmware/pwm_us_extension.c.inc"
int main(void){unsigned before;int p;
 CHECK(servo_pwm_set_us(pwm_10,333,1500)==-2);pwm_status[1].channel_status=15;
 CHECK(servo_pwm_set_us(pwm_00,333,1500)==-1);CHECK(servo_pwm_set_us(pwm_10,332,1500)==-1);CHECK(servo_pwm_set_us(pwm_10,333,1399)==-1);CHECK(servo_pwm_set_us(pwm_10,333,1601)==-1);CHECK(!writes&&!starts&&!stops);
 for(p=4;p<8;p++){CHECK(!servo_pwm_set_us((pwm_e)p,333,1500));CHECK(ax_readl(256+PWM_LOADCOUNT_OFF(p%4))==36072);CHECK(ax_readl(256+PWM_LOADCOUNT2_OFF(p%4))==36000);}
 before=writes;CHECK(!servo_pwm_set_us(pwm_10,333,1500));CHECK(writes==before&&starts==4&&stops==4);
 CHECK(!servo_pwm_set_us(pwm_10,333,1522));CHECK(ax_readl(256+PWM_LOADCOUNT2_OFF(0))==36528);
 CHECK(!servo_pwm_set_us(pwm_10,50,1400));CHECK(ax_readl(256+PWM_LOADCOUNT_OFF(0))==446400);
 stop_fail=1;before=writes;CHECK(servo_pwm_set_us(pwm_10,333,1500)==-7);CHECK(writes==before);stop_fail=0;
 start_fail=1;CHECK(servo_pwm_set_us(pwm_10,333,1500)==-8);start_fail=0;
 before=starts;CHECK(!servo_pwm_set_us(pwm_10,333,1500));CHECK(starts==before+1);
 puts("PASS: PWM bounds, channel registers, pulse arithmetic, unchanged-command skip, stop/start errors; simulated registers only");return 0;
}
