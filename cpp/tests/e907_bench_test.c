#include <stdio.h>
#include <stdlib.h>
#include "../../tools/e907_bench_sequence.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check failed line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
struct fake {
    uint64_t time,last_set;
    unsigned active,seen,calls,stops,fail_at,cancel_at;
    unsigned transitions[4],last_pulse[4];
};
static uint64_t now(void *p) { return ((struct fake *)p)->time; }
static int wait_ms(void *p,unsigned ms) {
    struct fake *f=p;
    CHECK(ms<=100);
    f->time+=ms;
    return f->cancel_at && f->time>=f->cancel_at ? -1 : 0;
}
static int set(void *p,unsigned mask,unsigned us) {
    struct fake *f=p;
    CHECK(mask && !(mask&~15U) && us>=1400 && us<=1600);
    CHECK(!f->active || f->active==mask);
    if(f->active) CHECK(f->time-f->last_set<=101);
    if(++f->calls==f->fail_at) return -1;
    if(!f->active) CHECK(us==1500);
    f->active=mask;f->seen|=f->active;f->last_set=f->time;
    for(unsigned ch=0;ch<4;ch++) if(mask&(1U<<ch))
    if(f->last_pulse[ch]!=us) { ++f->transitions[ch];f->last_pulse[ch]=us; }
    ++f->time; /* Simulate acknowledgment latency. */
    return 0;
}
static int stop(void *p) {
    struct fake *f=p;f->active=0;++f->stops;return 0;
}
int main(void) {
    for(unsigned mask=1;mask<=15;mask++) {
        struct fake f={0};
        struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
        CHECK(e907_bench_run(&io,mask,1)==0);
        CHECK(!f.active && f.seen==mask);
        unsigned selected=0;
        for(unsigned ch=0;ch<4;ch++) {
            if(mask&(1U<<ch)) {
                ++selected;CHECK(f.transitions[ch]==9 && f.last_pulse[ch]==1500);
            } else CHECK(f.transitions[ch]==0);
        }
        CHECK(f.stops==2*selected);
        CHECK(f.time>=15000*selected && f.time<=15009*selected);
    }
    /* Failure/cancellation during an active hold must stop and never advance. */
    for(unsigned failure=0;failure<2;failure++) {
        struct fake f={0};
        if(failure) f.fail_at=5; else f.cancel_at=400;
        struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
        CHECK(e907_bench_run(&io,15,1)==-1);
        CHECK(!f.active && f.seen==1 && f.stops==2 && f.calls<=5);
    }
    /* Repeat whole channel sequence; cancellation in round two must not restart. */
    for(unsigned cancel=0;cancel<2;cancel++) {
        struct fake repeated={0};
        if(cancel) repeated.cancel_at=60400;
        struct e907_bench_io repeat_io={&repeated,now,wait_ms,set,stop,NULL};
        CHECK(e907_bench_run(&repeat_io,15,3)==(cancel?-1:0));
        CHECK(!repeated.active);
        if(cancel) {
            CHECK(repeated.time>=60400 && repeated.time<61000);
            CHECK(repeated.stops==10);
        } else {
            CHECK(repeated.time>=180000 && repeated.time<=180108);
            CHECK(repeated.stops==24);
            for(unsigned ch=0;ch<4;ch++) CHECK(repeated.transitions[ch]==25);
        }
    }
    /* Together mode must enable all four in one callback, finish in 15 s/round,
       and stop the entire group on a failed command or interrupted wait. */
    for(unsigned scenario=0;scenario<3;scenario++) {
        struct fake group={0};
        if(scenario==1) group.fail_at=5;
        if(scenario==2) group.cancel_at=15400;
        struct e907_bench_io io={&group,now,wait_ms,set,stop,NULL};
        CHECK(e907_bench_run_mode(&io,15,2,1)==(scenario?-1:0));
        CHECK(group.seen==15 && !group.active);
        if(!scenario) {
            CHECK(group.time>=30000 && group.time<=30018 && group.stops==4);
            for(unsigned ch=0;ch<4;ch++) CHECK(group.transitions[ch]==17);
        } else CHECK(group.time<16000);
        for(unsigned ch=1;ch<4;ch++) {
            CHECK(group.transitions[ch]==group.transitions[0]);
            CHECK(group.last_pulse[ch]==group.last_pulse[0]);
        }
    }
    unsigned count=0;
    CHECK(e907_bench_parse_cycles("1",&count)==0 && count==1);
    CHECK(e907_bench_parse_cycles("10000",&count)==0 && count==10000);
    const char *bad[]={"", "0", "-1", "+2", " 3", "3x", "1.5", "10001", "4294967297"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        CHECK(e907_bench_parse_cycles(bad[i],&count)==-1);
    struct fake f={0};
    struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
    CHECK(e907_bench_run(&io,0,1)==-1 && e907_bench_run(&io,16,1)==-1);
    CHECK(e907_bench_run(&io,15,0)==-1 && e907_bench_run(&io,15,10001)==-1);
    CHECK(!f.calls && !f.stops);
    puts("PASS: sequential selection, bounded duration, heartbeat, stop on failure/cancel");
    return 0;
}
