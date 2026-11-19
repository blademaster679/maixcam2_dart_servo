#include <stdio.h>
#include <stdlib.h>
#include "../../tools/servo_four_sweep_sequence.h"
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct sim {uint64_t now,last_set;unsigned sets,stops,cycles,positive,negative,fail_at,latency,max_step,forward_steps,reverse_steps;int last,cancel,at1000,at2500,at4000;};
static uint64_t now(void *p){return ((struct sim *)p)->now;}
static int wait_ms(void *p,unsigned ms){struct sim *s=p;CHECK(ms<=100);s->now+=ms;return s->cancel&&s->now>=12300?-1:0;}
static int set(void *p,int offset){
 struct sim *s=p;CHECK(offset>=-375&&offset<=375);
 if(s->sets){CHECK(s->now-s->last_set<=120+s->latency);if(s->latency<=20)CHECK(abs(offset-s->last)<=(int)(s->max_step?s->max_step:5));}
 if(offset==375&&s->last!=375)s->positive++;
 if(offset==-375&&s->last!=-375)s->negative++;
 if(s->last==-375&&offset==375){
  CHECK(s->now==6000+4000*s->forward_steps);s->forward_steps++;
 }
 if(s->last==375&&offset==-375)s->reverse_steps++;
 s->last=offset;s->last_set=s->now;s->sets++;
 if(s->now==1000)s->at1000=offset;
 if(s->now==2500)s->at2500=offset;
 if(s->now==4000)s->at4000=offset;
 s->now+=s->latency;
 return s->fail_at&&s->sets==s->fail_at?-1:0;
}
static int stop(void *p){((struct sim *)p)->stops++;return 0;}
static void cycle(void *p,unsigned n,unsigned total){struct sim *s=p;CHECK(n==++s->cycles&&n<=total);}
static struct servo_sweep_io io(struct sim *s){return (struct servo_sweep_io){s,now,wait_ms,set,stop,cycle};}
int main(void){
 for(unsigned count=1;count<=3;count+=2){
  struct sim s={0};struct servo_sweep_io x=io(&s);
  CHECK(!servo_four_sweep_run(&x,count));CHECK(s.now==14000+12000*count);
  CHECK(s.last==0&&s.stops==2&&s.cycles==count);
  CHECK(s.positive==count&&s.negative==count+1);
 }
 struct sim s={0};struct servo_sweep_io x=io(&s);
 CHECK(servo_four_sweep_run(&x,0)==-1&&servo_four_sweep_run(&x,101)==-1);CHECK(!s.sets&&!s.stops);
 s.fail_at=120;CHECK(servo_four_sweep_run(&x,3)==-1&&s.stops==2&&s.sets==120);
 s=(struct sim){.cancel=1};CHECK(servo_four_sweep_run(&x,3)==-1&&s.stops==2&&s.now==12300);
 s=(struct sim){0};CHECK(!servo_sweep_ramp(&x,-375,375,5000));
 CHECK(s.now==5000&&s.sets==250&&s.last==375);
 CHECK(s.at1000==-297&&s.at2500==0&&s.at4000==297);
 s=(struct sim){.latency=7};CHECK(!servo_sweep_ramp(&x,-375,375,5000));
 CHECK(s.now==5007&&s.sets==250&&s.last==375); /* No 250 * 7ms drift. */
 s=(struct sim){.latency=120};CHECK(!servo_sweep_ramp(&x,-375,375,5000));
 CHECK(s.now<=5240&&s.sets<50&&s.last==375); /* Skip stale samples. */
 for(unsigned ms=1000;ms<=10000;ms+=1000){
  s=(struct sim){.max_step=(22500+ms-1)/ms};
  CHECK(!servo_four_sweep_run_timed(&x,3,ms));
  CHECK(s.now==10000+8*ms&&s.last==0&&s.cycles==3&&s.stops==2);
 }
 s=(struct sim){0};
 CHECK(servo_four_sweep_run_timed(&x,3,999)==-1);
 CHECK(servo_four_sweep_run_timed(&x,3,10001)==-1);
 CHECK(servo_sweep_ramp(&x,-375,375,0)==-1);
 CHECK(!s.sets&&!s.stops&&!s.now);
 s=(struct sim){.max_step=750};CHECK(!servo_four_step_run(&x));
 CHECK(s.now==20000&&s.forward_steps==3&&s.reverse_steps==2);
 CHECK(s.last==0&&s.cycles==3&&s.stops==2);
 s=(struct sim){.max_step=750,.fail_at=141};CHECK(servo_four_step_run(&x)==-1);
 CHECK(s.forward_steps==1&&s.stops==2&&s.now==6000);
 s=(struct sim){.max_step=750,.cancel=1};CHECK(servo_four_step_run(&x)==-1);
 CHECK(s.forward_steps==2&&s.stops==2&&s.now==12300);
 puts("PASS: smooth four-servo trajectory, variable speed bounds, timing/latency, endpoints, heartbeat, failure and cancellation");
}
