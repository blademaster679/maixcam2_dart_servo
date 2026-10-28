#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../research/riscv__drivers__pwm__drv_pwm.h"
#include "../../research/riscv__drivers__pwm__drv_pwm_reg.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const uint32_t pwm_base_addr[3]={0x06060000,0x06061000,0x06062000};
static pwm_status_t pwm_status[3];
static uint32_t registers[0x2000/4],pads[4],clock_writes[3];
static unsigned writes;
static const uint32_t expected_pads[4]={0x02304060,0x02304054,0x02304018,0x0230400c};
static const unsigned expected_pwms[4]={7,6,1,0};
static void fence(void){}
static int owned_register(uint32_t address){
    for(unsigned i=0;i<4;++i){unsigned pwm=expected_pwms[i];uint32_t base=pwm_base_addr[pwm/4];
        if(address==base+PWM_CONTROLREG_OFF(pwm%4)||address==base+PWM_LOADCOUNT_OFF(pwm%4)||address==base+PWM_LOADCOUNT2_OFF(pwm%4))return 1;
    }
    return 0;
}
static uint32_t ax_readl(uint32_t address){
    if(address>=0x06060000&&address<0x06062000)return registers[(address-0x06060000)/4];
    for(unsigned i=0;i<4;++i)if(address==expected_pads[i])return pads[i];
    CHECK(0);return 0;
}
static void ax_writel(uint32_t value,uint32_t address){
    ++writes;
    if(address>=0x06060000&&address<0x06062000){CHECK(owned_register(address));registers[(address-0x06060000)/4]=value;return;}
    for(unsigned i=0;i<4;++i)if(address==expected_pads[i]){pads[i]=value;return;}
    if(address==0x048700c0){CHECK(value==(1U<<31));clock_writes[0]++;return;}
    if(address==0x048700c8){CHECK(value==1);clock_writes[1]++;return;}
    if(address==0x048700b8){CHECK(value==((1U<<19)|(1U<<20)|(1U<<25)|(1U<<26)));clock_writes[2]++;return;}
    CHECK(0); /* Includes forbidden B2/B3 and all unrelated MMIO. */
}
int pwm_stop(pwm_e p){uint32_t a=pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4);ax_writel(ax_readl(a)&~1U,a);return 0;}
int pwm_start(pwm_e p){uint32_t a=pwm_base_addr[p/4]+PWM_CONTROLREG_OFF(p%4);ax_writel(ax_readl(a)|1U,a);return 0;}
#include "../../firmware/pwm_us_extension.c.inc"
#include "../../firmware/e907_live/servo_service.h"
static void request(uint32_t *m,servo_service_state *s,unsigned cmd,unsigned mask,uint32_t now,int expected){
    m[4]=cmd;m[12]=mask;m[5]++;servo_service_step(m,now,s);CHECK(m[6]==m[5]&&(int32_t)m[8]==expected);
}
int main(void){
    uint32_t m[64]={0};servo_service_state state={0,0,0};
    for(unsigned i=0;i<0x2000/4;++i)registers[i]=0x5a5a5a5a;
    for(unsigned i=0;i<4;++i){pads[i]=0x83;m[13+i]=1445+22*i;CHECK(servo_channels[i].pwm==expected_pwms[i]);}
    request(m,&state,2,15,0,-20);CHECK(writes==0);
    m[34]=0x50574D31U;request(m,&state,2,15,0,-20);CHECK(writes==0); /* Old loader rejected. */
    m[34]=SERVO_LEASE_MAGIC;request(m,&state,2,16,0,-20);CHECK(writes==0);
    m[13]=1399;request(m,&state,2,15,0,-21);CHECK(writes==0);m[13]=1445;
    request(m,&state,2,15,0xfffffff0U,0);
    CHECK(m[17]==15&&clock_writes[0]==1&&clock_writes[1]==1&&clock_writes[2]==1);
    for(unsigned i=0;i<4;++i){
        CHECK(m[20+3*i]==0x1f&&m[22+3*i]==24*m[13+i]&&m[21+3*i]+m[22+3*i]==72072);
        CHECK(pads[i]==0x30083);
    }
    servo_service_step(m,0xfffffff0U+24000000U,&state);CHECK(m[17]==0&&m[18]==2&&m[32]==1);
    for(unsigned i=0;i<4;++i){
        request(m,&state,3,0,100,0);request(m,&state,2,1U<<i,100,0);
        for(unsigned j=0;j<4;++j)CHECK((m[20+3*j]&1)==(i==j));
    }
    request(m,&state,3,0,101,0);
    for(unsigned i=0;i<0x2000/4;++i)if(!owned_register(0x06060000+4*i))CHECK(registers[i]==0x5a5a5a5a);
    puts("PASS: real E907 service, cross-group mapping, old ABI rejection, watchdog wrap, logical masks and untouched PWM2/3/4/5/BMI pins");
}
