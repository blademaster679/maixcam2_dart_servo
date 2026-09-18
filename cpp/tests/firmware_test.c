/* Runs the actual firmware against simulated RT-Thread/PWM APIs.
 * No hardware timing or target ABI is validated here. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "../../firmware/servo_bench.c"
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
const char *test_console="uart2";
static unsigned inits,stops,deinits,writes,commands,ticks;
static int fail_init=-1,fail_set=-1,cancel_init=-1,cancel_delay;
static int allocation_fail,startup_fail,cleanup_fail;
static uint32_t regs[4];
static void (*pending)(void*);
static char output[32768];static size_t used;
unsigned rt_tick_get(void){return ticks;}
void rt_kprintf(const char *f,...){va_list a;int n;va_start(a,f);n=vsnprintf(output+used,sizeof(output)-used,f,a);va_end(a);CHECK(n>=0&&(size_t)n<sizeof(output)-used);used+=(size_t)n;}
void rt_thread_mdelay(int n){ticks+=(unsigned)n/10;if(cancel_delay)stop_requested=1;}
rt_thread_t rt_thread_create(const char*n,void(*f)(void*),void*a,unsigned s,unsigned p,unsigned t){(void)n;(void)a;(void)s;(void)p;(void)t;if(allocation_fail)return NULL;pending=f;return (void*)1;}
int rt_thread_startup(rt_thread_t t){(void)t;return startup_fail?-1:0;}
int rt_thread_delete(rt_thread_t t){(void)t;pending=NULL;return 0;}
uint32_t ax_readl(uint32_t a){int i;for(i=0;i<4;i++)if(a==pads[i])return regs[i];CHECK(0);return 0;}
void ax_writel(uint32_t v,uint32_t a){int i;for(i=0;i<4;i++)if(a==pads[i]){regs[i]=v;writes++;return;}CHECK(0);}
int pwm_init(pwm_e p,uint32_t h,uint8_t d){int i=(int)p-4;CHECK(i>=0&&i<4&&h==333&&d==50);if(i==fail_init)return -7;inits|=1U<<i;if(i==cancel_init)stop_requested=1;return 0;}
int pwm_stop(pwm_e p){CHECK(inits&(1U<<(p-4)));stops|=1U<<(p-4);return cleanup_fail?-8:0;}
int pwm_deinit(pwm_e p){deinits|=1U<<(p-4);return 0;}
int servo_pwm_set_us(pwm_e p,uint32_t h,uint32_t us){CHECK(inits&(1U<<(p-4)));CHECK(h==333&&us>=1445&&us<=1555);commands++;return (int)p-4==fail_set?-9:0;}
static void reset(void){int i;running=stop_requested=0;selected_mask=15;seq=0;inits=stops=deinits=writes=commands=ticks=0;fail_init=fail_set=cancel_init=-1;cancel_delay=allocation_fail=startup_fail=cleanup_fail=0;pending=NULL;used=0;output[0]=0;test_console="uart2";for(i=0;i<4;i++)regs[i]=0x83U+(unsigned)i;}
static void start(char *id){char *v[]={"servo_bench","start",id};servo_bench(id?3:2,v);}
static void finish(void){int i;CHECK(pending);pending(NULL);pending=NULL;CHECK(!running&&stops==inits&&deinits==inits);for(i=0;i<4;i++)CHECK(regs[i]==0x83U+(unsigned)i);}
int main(void){int i;char id[2]={0,0};char *status[]={"servo_bench","status"};char *stop[]={"servo_bench","stop"};
 reset();servo_bench(2,status);CHECK(!inits&&!writes&&strstr(output,"SERVO_STATUS"));
 reset();start(NULL);CHECK(running);finish();CHECK(commands==112&&strstr(output,"failed,0"));
 for(i=0;i<4;i++){reset();id[0]=(char)('1'+i);start(id);finish();CHECK(inits==(1U<<i)&&commands==28);}
 reset();test_console="uart1";start(NULL);CHECK(!running&&!pending);start("3");CHECK(!running);start("1");finish();CHECK(inits==1);
 reset();test_console="uart3";start("2");CHECK(!running);start("4");finish();CHECK(inits==8);
 reset();start("0");start("12");CHECK(!running&&!pending);
 reset();start("1");start("2");CHECK(selected_mask==1);servo_bench(2,stop);finish();CHECK(!inits&&!commands);
 for(i=0;i<4;i++){reset();fail_init=i;start(NULL);finish();CHECK(inits==((1U<<i)-1)&&strstr(output,"SERVO_ERROR,init")&&strstr(output,"failed,1"));}
 for(i=0;i<4;i++){reset();fail_set=i;start(NULL);finish();CHECK(strstr(output,"failed,1"));}
 reset();cancel_init=0;start(NULL);finish();CHECK(inits==1&&!commands);
 reset();cancel_delay=1;start(NULL);finish();CHECK(commands==4&&strstr(output,"stop_requested,1"));
 reset();allocation_fail=1;start(NULL);CHECK(!running&&!pending);
 reset();startup_fail=1;start(NULL);CHECK(!running&&!pending);
 reset();cleanup_fail=1;start("4");finish();CHECK(strstr(output,"SERVO_ERROR,cleanup")&&strstr(output,"failed,1"));
 puts("PASS: firmware selection, console conflicts, stop, partial failures, cleanup, thread failures; simulated APIs only");return 0;
}
