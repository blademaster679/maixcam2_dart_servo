#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t ctl,pad=0x83,low,high;
static unsigned writes;
static void fence(void){}
static uint32_t ax_readl(uint32_t a){
 switch(a){case 0x06061044:return ctl;case 0x02304060:return pad;default:assert(0);return 0;}
}
static void ax_writel(uint32_t v,uint32_t a){
 writes++;
 switch(a){
 case 0x06061044:ctl=v;break;case 0x02304060:pad=v;break;
 case 0x0606103c:low=v;break;case 0x060610bc:high=v;break;
 case 0x048700c8:assert(v==1);break;case 0x048700b8:assert(v==(1U<<26));break;
 default:assert(0); /* Any PWM0/6, other pad, GPIO or I2C write fails. */
 }
}
#include "../../firmware/e907_live/pwm7_service.h"
int main(void){
 volatile uint32_t m[40]={0};pwm7_state s={0};uint32_t now=0xffff0000;
 pwm7_step(m,now,&s);assert(!writes);
 m[4]=1;m[5]=1;pwm7_step(m,now,&s);
 assert(m[6]==1&&ctl==31&&low==36072&&high==36000&&pad==0x30083);
 for(unsigned i=1;i<=3000;i++){
  now+=2400000;m[5]++;pwm7_step(m,now,&s);
  assert(ctl==(i==3000?30:31));
 }
 assert(m[6]==2&&s.elapsed==7200000000ULL);
 unsigned saved=writes;pwm7_step(m,now+2400000,&s);assert(writes==saved);
 s=(pwm7_state){0};m[6]=0;pwm7_step(m,0,&s);pwm7_step(m,24000000,&s);assert(ctl==30&&m[6]==4);
 s=(pwm7_state){0};m[4]=1;pwm7_step(m,0,&s);m[4]=2;pwm7_step(m,1,&s);assert(ctl==30&&m[6]==3);
 puts("PASS: PWM7-only writes, 300s across timer wraps, watchdog and stop");
}
