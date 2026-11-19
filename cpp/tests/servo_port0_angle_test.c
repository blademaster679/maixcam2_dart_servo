#include <assert.h>
#include <stdio.h>
#include "../../tools/servo_port0_angle_sequence.h"
struct fake {uint64_t now,last;unsigned pulse,calls,stops,peak_ms,fail_at,cancel_at;int active;};
static uint64_t now(void *p){return ((struct fake*)p)->now;}
static int wait_ms(void *p,unsigned ms){
 struct fake *f=p;assert(ms<=100);f->now+=ms;
 if(f->pulse==1955)f->peak_ms+=ms;
 return f->cancel_at&&f->now>=f->cancel_at?-1:0;
}
static int set(void *p,unsigned mask,unsigned us){
 struct fake *f=p;assert(mask==8&&us>=1580&&us<=1955);
 if(++f->calls==f->fail_at)return -1;
 if(!f->active)assert(us==1580);
 else{assert(f->now-f->last<=100);assert(us<=f->pulse+10&&f->pulse<=us+10);}
 f->pulse=us;f->active=1;f->last=f->now;return 0;
}
static int stop(void *p){struct fake *f=p;f->active=0;f->stops++;return 0;}
int main(void){
 for(unsigned scenario=0;scenario<3;scenario++){
  struct fake f={0};if(scenario==1)f.fail_at=35;if(scenario==2)f.cancel_at=8500;
  const struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
  assert(servo_port0_angle_run(&io)==(scenario?-1:0));
  assert(!f.active&&f.stops==2);
  if(!scenario){assert(f.now==19000&&f.pulse==1580&&f.peak_ms==5100);}
 }
 /* Hold remains at its peak beyond the timed test's 19-second finish. */
 for(unsigned scenario=0;scenario<2;scenario++){
  struct fake f={0};if(scenario)f.fail_at=301;else f.cancel_at=60000;
  const struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
  assert(servo_port0_angle_run_mode(&io,1)==-1);
  assert(!f.active&&f.stops==2&&f.pulse==1955);
  if(!scenario)assert(f.now==60000&&f.peak_ms==53100);
 }
 for(unsigned scenario=0;scenario<2;scenario++){
  struct fake f={0};if(scenario)f.fail_at=301;else f.cancel_at=60000;
  const struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
  assert(servo_port0_angle_run_target(&io,1,1830)==-1);
  assert(!f.active&&f.stops==2&&f.pulse==1830&&f.peak_ms==0);
 }
 struct fake f={0};const struct e907_bench_io io={&f,now,wait_ms,set,stop,NULL};
 assert(servo_port0_angle_run_target(&io,1,1579)==-1);
 assert(servo_port0_angle_run_target(&io,1,1956)==-1);
 assert(f.calls==0&&f.stops==0);
 puts("PASS: only port 0, ramp, timed return, 45/30 degree pulse holds, limits and cancellation");
}
