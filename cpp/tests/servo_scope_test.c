#include <stdio.h>
#include <stdlib.h>
#include "../../tools/servo_scope_sequence.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
struct fake {
    uint64_t time,first,last;
    unsigned active,calls,stops,fail_set,cancel_wait,fail_stop;
};
static uint64_t now(void *ctx){return ((struct fake*)ctx)->time;}
static int wait_ms(void *ctx,unsigned ms){
    struct fake *f=ctx;CHECK(ms>0&&ms<=100);f->time+=ms;
    return f->cancel_wait&&f->time>=f->cancel_wait?-1:0;
}
static int set(void *ctx,unsigned mask,unsigned us){
    struct fake *f=ctx;CHECK(us==1500&&mask&&!(mask&(mask-1)));
    if(++f->calls==f->fail_set)return -1;
    if(f->active){CHECK(f->active==mask);CHECK(f->time-f->last<=103);}
    else f->first=f->time;
    f->active=mask;f->last=f->time;f->time+=3;return 0;
}
static int stop(void *ctx){
    struct fake *f=ctx;++f->stops;f->active=0;
    return f->stops==f->fail_stop?-1:0;
}
int main(void){
    unsigned pulse=0;
    CHECK(!servo_hold_parse_pulse("1400",&pulse)&&pulse==1400);
    CHECK(!servo_hold_parse_pulse("1500",&pulse)&&pulse==1500);
    CHECK(!servo_hold_parse_pulse("1600",&pulse)&&pulse==1600);
    const char *bad[]={"","1399","1601","-1500","+1500","1500x"," 1500","4294968796"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(servo_hold_parse_pulse(bad[i],&pulse)==-1);
    for(unsigned mask=1;mask<=8;mask<<=1){
        struct fake f={0};struct servo_scope_io io={&f,now,wait_ms,set,stop};
        CHECK(!servo_scope_run(&io,mask));
        CHECK(!f.active&&f.stops==2&&f.calls>500);
        CHECK(f.time-f.first==60003); /* Acknowledgement precedes minute. */
    }
    for(unsigned scenario=0;scenario<4;++scenario){
        struct fake f={0};
        if(scenario==0)f.fail_set=4;
        if(scenario==1)f.cancel_wait=300;
        if(scenario==2)f.fail_stop=1;
        if(scenario==3)f.fail_stop=2;
        struct servo_scope_io io={&f,now,wait_ms,set,stop};
        CHECK(servo_scope_run(&io,4)==-1&&!f.active);
        if(scenario==2)CHECK(!f.calls&&f.stops==1);
        else CHECK(f.stops==2);
    }
    struct fake f={0};struct servo_scope_io io={&f,now,wait_ms,set,stop};
    CHECK(servo_scope_run(&io,0)==-1&&servo_scope_run(&io,3)==-1&&servo_scope_run(&io,16)==-1);
    CHECK(!f.calls&&!f.stops);
    puts("PASS: continuous 60-second hold, single channel, heartbeat, stop on failure/cancel");
    return 0;
}
