#ifndef SERVO_SERVICE_H
#define SERVO_SERVICE_H
#include "../servo_board.h"
/* Included after PWM/MMIO primitives; exercised by host register tests. */
typedef struct {uint32_t last,active;int initialized;} servo_service_state;
static void stop_all(void){for(unsigned i=0;i<4;++i)pwm_stop((pwm_e)servo_channels[i].pwm);}
static void snapshot(volatile uint32_t *m){for(unsigned i=0;i<4;++i){
    m[20+3*i]=ax_readl(SERVO_PWM_BASE+servo_control_offset(i));
    m[21+3*i]=ax_readl(SERVO_PWM_BASE+servo_low_offset(i));
    m[22+3*i]=ax_readl(SERVO_PWM_BASE+servo_high_offset(i));
}}
static int servo_service_step(volatile uint32_t *m,uint32_t now,servo_service_state *state){
    m[19]=now;
    if(state->active&&(uint32_t)(now-state->last)>=24000000U){
        stop_all();state->active=0;m[17]=0;m[18]=2;m[32]++;snapshot(m);
    }
    uint32_t seq=m[5];if(seq==m[6])return 0;fence();
    uint32_t cmd=m[4],mask=m[12];int result=0;
    if(cmd==1)result=(int)~m[7];
    else if(cmd==2){
        if(m[34]!=SERVO_LEASE_MAGIC||!mask||(mask&~15U))result=-20;
        for(unsigned i=0;i<4;++i)if((mask&(1U<<i))&&(m[13+i]<1400||m[13+i]>1600))result=-21;
        if(!result){
            if(!state->initialized){
                ax_writel(1U<<31,0x048700c0);
                ax_writel(1U,0x048700c8);
                ax_writel(SERVO_CHANNEL_CLOCK_MASK,0x048700b8);
                stop_all();
                for(unsigned i=0;i<4;++i){uint32_t pad=servo_channels[i].pad;
                    ax_writel((ax_readl(pad)&~(7U<<16))|(3U<<16),pad);
                }
                pwm_status[0].channel_status=3;pwm_status[1].channel_status=12;state->initialized=1;
            }
            for(unsigned i=0;i<4;++i)if(mask&(1U<<i)){
                int r=servo_pwm_set_us((pwm_e)servo_channels[i].pwm,333,m[13+i]);if(r)result=r;
            }
            if(result){stop_all();state->active=0;m[18]=3;}
            else{state->active|=mask;state->last=now;m[18]=1;}
        }
    }
    else if(cmd==3||cmd==5){if(state->initialized)stop_all();state->active=0;m[18]=0;}
    else if(cmd!=4)result=-22;
    if(state->initialized)snapshot(m);
    m[17]=state->active;m[8]=(uint32_t)result;fence();m[6]=seq;
    return cmd==5;
}
#endif
