/* Temporary RAM-only E907 probe. Boot sequence: official sipeed SDK
 * boot/bl1/driver/riscv/riscv.c + common/include/chip_reg.h.
 * This runtime CMM loader is experimental integration, not a vendor API.
 * Refuses any core not held in reset with its clock disabled.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <dlfcn.h>
#include <errno.h>
#define REGION_SIZE 16384U
#define COMM_BASE 0x02340000UL
#define RESET_MASK (3U<<16)
#define CLOCK_MASK (1U<<8)
#define MUX_MASK (1U<<25)
static volatile sig_atomic_t cancelled;
static void onsignal(int s){(void)s;cancelled=1;}
static void barrier(void){__asm__ volatile("dmb sy" ::: "memory");}
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static uint32_t rd(volatile uint32_t *r,unsigned off){uint32_t v=r[off/4];barrier();return v;}
static void wr(volatile uint32_t *r,unsigned off,uint32_t v){barrier();r[off/4]=v;barrier();}
static int driver_action(const char *action){
 char path[160];snprintf(path,sizeof(path),"/sys/bus/platform/drivers/axera-pwm/%s",action);
 int f=open(path,O_WRONLY);if(f<0)return -1;
 const char dev[]="6061000.pwm1";ssize_t n=write(f,dev,sizeof(dev)-1);close(f);
 return n==(ssize_t)sizeof(dev)-1?0:-1;
}
static int pwm_idle(void){
 char buf[16384];FILE *f=fopen("/sys/kernel/debug/pwm","r");if(!f)return 0;
 size_t n=fread(buf,1,sizeof(buf)-1,f);int error=ferror(f);fclose(f);buf[n]=0;if(error)return 0;
 char *p=strstr(buf,"platform/6061000.pwm1, 4 PWM devices");if(!p)return 0;
 char *end=strstr(p,"\n\n");if(!end)return 0;*end=0;
 return !strstr(p,"requested")&&!strstr(p,"enabled");
}
static int command(volatile uint32_t *s,unsigned op,unsigned seq,int32_t expected){
 if(cancelled)return -1;
 wr(s,16,op);wr(s,20,seq);
 for(unsigned i=0;i<200&&!cancelled&&rd(s,24)!=seq;i++)pause_ms();
 if(cancelled||rd(s,0)!=0x53525631U||rd(s,24)!=seq||(int32_t)rd(s,32)!=expected){
  fprintf(stderr,"Command failed op=%u seq=%u ack=%u result=%d\n",op,seq,rd(s,24),(int32_t)rd(s,32));return -1;
 }return 0;
}
static int check_pwm(volatile uint32_t *s,volatile uint32_t *pwm,unsigned ch,unsigned us){
 uint32_t ctl=rd(s,80+12*ch),low=rd(s,84+12*ch),high=rd(s,88+12*ch);
 if(ctl!=0x1f||high!=24*us||low+high!=72072)return -1;
 /* Independent A53 readback: the loader did not write these values. */
 return rd(pwm,8+20*ch)==ctl&&rd(pwm,20*ch)==low&&rd(pwm,0xb0+4*ch)==high?0:-1;
}
static int servo_test(volatile uint32_t *s,volatile uint32_t *pwm){
 unsigned seq=0;const unsigned pulses[]={1500,1522,1478,1555,1445,1500};
 wr(s,136,0x50574D31U); /* lease established by loader */
 wr(s,48,16);if(command(s,2,++seq,-20))return -1;
 wr(s,48,15);for(unsigned ch=0;ch<4;ch++)wr(s,52+4*ch,1500);
 wr(s,52,1399);if(command(s,2,++seq,-21))return -1;
 wr(s,52,1601);if(command(s,2,++seq,-21))return -1;
 for(unsigned step=0;step<sizeof(pulses)/sizeof(pulses[0]);step++){
  for(unsigned ch=0;ch<4;ch++)wr(s,52+4*ch,pulses[step]);
  if(command(s,2,++seq,0))return -1;
  for(unsigned ch=0;ch<4;ch++){
   uint32_t ctl=rd(s,80+12*ch),low=rd(s,84+12*ch),high=rd(s,88+12*ch);
   printf("PWM step=%u channel=%u pulse_us=%u control=%08x low=%u high=%u\n",step,ch+4,pulses[step],ctl,low,high);
   if(check_pwm(s,pwm,ch,pulses[step]))return -1;
  }
  for(unsigned i=0;i<50&&!cancelled;i++)pause_ms();
 }
 const unsigned mixed[4]={1445,1478,1522,1555};
 for(unsigned ch=0;ch<4;ch++)wr(s,52+4*ch,mixed[ch]);
 if(command(s,2,++seq,0))return -1;
 for(unsigned ch=0;ch<4;ch++)if(check_pwm(s,pwm,ch,mixed[ch]))return -1;
 puts("PASS: independent A53 MMIO readback of four different E907 pulse targets");
 if(command(s,3,++seq,0))return -1;
 for(unsigned ch=0;ch<4;ch++)if(rd(s,80+12*ch)&1)return -1;
 /* Only one channel should enable after an explicit stop. */
 wr(s,48,4);if(command(s,2,++seq,0))return -1;
 for(unsigned ch=0;ch<4;ch++)if((rd(s,80+12*ch)&1)!=(ch==2))return -1;
 /* Deliberately stop sending commands: E907 must shut PWM off itself. */
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
 for(unsigned i=0;i<2000&&!cancelled&&rd(s,72)!=2;i++)pause_ms();
 clock_gettime(CLOCK_MONOTONIC,&end);
 printf("WATCHDOG host_elapsed_ms=%.3f trips=%u active_mask=%u\n",(end.tv_sec-begin.tv_sec)*1000.0+(end.tv_nsec-begin.tv_nsec)/1e6,rd(s,128),rd(s,68));
 if(rd(s,72)!=2||rd(s,128)!=1||rd(s,68)!=0){fprintf(stderr,"Watchdog failed\n");return -1;}
 if(command(s,4,++seq,0))return -1;
 for(unsigned ch=0;ch<4;ch++)if((rd(s,80+12*ch)|rd(pwm,8+20*ch))&1)return -1;
 if(command(s,5,++seq,0))return -1;
 puts("PASS: E907 four-channel register control, invalid-input rejection, stop, single-channel selection and 1s command watchdog");
 return 0;
}
int main(int argc,char **argv){
 int rc=1,fd=-1,lockfd=-1,initialized=0,allocated=0,touched=0,leased=0,saved_pwm=0;void *lib=NULL,*mem=NULL;
 int servo=argc==3&&!strcmp(argv[1],"--run-servo-test");
 volatile uint32_t *pad=MAP_FAILED,*periph=MAP_FAILED,*pwm=MAP_FAILED;
 const unsigned poff[4]={0x84,0x90,0x54,0x60};uint32_t old_pad[4],old_pwm[4][3],old_pclk=0,old_chclk=0;
 volatile uint32_t *reg=MAP_FAILED,*shared=NULL;uint64_t phys=0;
 uint32_t old_mux=0,old_clock=0,old_reset=0,old_base=0;
 int (*init)(void)=NULL,(*deinit)(void)=NULL;
 int (*alloc)(uint64_t*,void**,uint32_t,uint32_t,const char*)=NULL;
 int (*release)(uint64_t,void*)=NULL;
 unsigned char blob[4096];size_t len=0;FILE *file=NULL;
 if(argc!=3||(strcmp(argv[1],"--run-probe")&&!servo)){fprintf(stderr,"Usage: %s --run-probe|--run-servo-test firmware.bin\n",argv[0]);return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGINT,onsignal);signal(SIGTERM,onsignal);signal(SIGHUP,onsignal);
 lockfd=open("/tmp/servo_e907.lock",O_CREAT|O_RDWR,0600);
 if(lockfd<0||flock(lockfd,LOCK_EX|LOCK_NB)){fprintf(stderr,"Another E907 loader is running\n");goto out;}
 file=fopen(argv[2],"rb");if(!file){perror("firmware");goto out;}
 len=fread(blob,1,sizeof(blob),file);int read_error=ferror(file);fclose(file);file=NULL;
 if(read_error||!len||len>=sizeof(blob)){fprintf(stderr,"Invalid probe size\n");goto out;}
 fd=open("/dev/mem",O_RDWR|O_SYNC);if(fd<0){perror("/dev/mem");goto out;}
 reg=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,COMM_BASE);if(reg==MAP_FAILED){perror("map registers");goto out;}
 old_mux=rd(reg,0x0c);old_clock=rd(reg,0x30);old_reset=rd(reg,0x54);old_base=rd(reg,0x24c);
 printf("PRECHECK mux=%08x clock=%08x reset=%08x base=%08x\n",old_mux,old_clock,old_reset,old_base);
 if((old_reset&RESET_MASK)!=RESET_MASK||(old_clock&CLOCK_MASK)||old_base){fprintf(stderr,"REFUSED: E907 is not in expected unused reset state\n");goto out;}
 if(servo){
  pad=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x02304000);
  periph=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x04870000);
  pwm=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0x06061000);
  if(pad==MAP_FAILED||periph==MAP_FAILED||pwm==MAP_FAILED){perror("map PWM");goto out;}
  if(!pwm_idle()){fprintf(stderr,"REFUSED: PWM1 not idle\n");goto out;}
  for(unsigned i=0;i<4;i++)old_pad[i]=rd(pad,poff[i]);
  old_pclk=rd(periph,0x10);old_chclk=rd(periph,0x08);
  if(driver_action("unbind")){perror("unbind PWM1");goto out;}leased=1;
  wr(periph,0xc8,1);wr(periph,0xb8,15U<<23);
  for(unsigned i=0;i<4;i++){
   old_pwm[i][0]=rd(pwm,i*0x14+8);old_pwm[i][1]=rd(pwm,i*0x14);old_pwm[i][2]=rd(pwm,0xb0+4*i);
  }
  saved_pwm=1;
  for(unsigned i=0;i<4;i++)if(old_pwm[i][0]&1){fprintf(stderr,"REFUSED: hardware PWM already enabled\n");goto out;}
  puts("LEASE: Linux PWM1 unbound, idle registers saved; PWM0/PWM2 untouched");
 }
 lib=dlopen("/opt/lib/libax_sys.so",RTLD_NOW|RTLD_LOCAL);if(!lib){fprintf(stderr,"%s\n",dlerror());goto out;}
 *(void**)(&init)=dlsym(lib,"AX_SYS_Init");*(void**)(&deinit)=dlsym(lib,"AX_SYS_Deinit");
 *(void**)(&alloc)=dlsym(lib,"AX_SYS_MemAlloc");*(void**)(&release)=dlsym(lib,"AX_SYS_MemFree");
 if(!init||!deinit||!alloc||!release){fprintf(stderr,"Missing official CMM APIs\n");goto out;}
 if(init()){fprintf(stderr,"AX_SYS_Init failed\n");goto out;}initialized=1;
 if(alloc(&phys,&mem,REGION_SIZE,4096,"servo_e907_probe")){fprintf(stderr,"CMM allocation failed\n");goto out;}allocated=1;
 if(!mem||phys<0x60000000ULL||phys+REGION_SIZE>0x80000000ULL||phys%4096){fprintf(stderr,"REFUSED: allocation outside verified board CMM range\n");goto out;}
 memset(mem,0,REGION_SIZE);memcpy(mem,blob,len);shared=(volatile uint32_t*)((char*)mem+4096);barrier();
 printf("ALLOC phys=%08llx bytes=%u firmware_bytes=%zu\n",(unsigned long long)phys,REGION_SIZE,len);
 if(cancelled)goto out;
 touched=1;
 wr(reg,0x58,1U<<16);wr(reg,0x24c,(uint32_t)phys);wr(reg,0x34,CLOCK_MASK);
 wr(reg,0x5c,1U<<17);wr(reg,0x10,MUX_MASK);wr(reg,0x5c,1U<<16);
 for(unsigned i=0;i<1000&&!cancelled&&rd(shared,0)==0;i++)pause_ms();
 printf("BOOT magic=%08x hart=%08x misa=%08x mhcr=%08x cause=%08x epc=%08x tval=%08x\n",rd(shared,0),rd(shared,4),rd(shared,8),rd(shared,12),rd(shared,36),rd(shared,40),rd(shared,44));
 if(cancelled||rd(shared,0)!=(servo?0x53525631U:0x45393037U)||rd(shared,12)!=0){fprintf(stderr,"Probe not ready or cache state unexpected\n");goto out;}
 if(servo){rc=servo_test(shared,pwm)?1:0;goto out;}
 for(unsigned seq=1;seq<=100&&!cancelled;seq++){
  uint32_t value=0x90700000U+seq;wr(shared,28,value);wr(shared,16,1);wr(shared,20,seq);
  unsigned wait;for(wait=0;wait<100&&rd(shared,24)!=seq&&!cancelled;wait++)pause_ms();
  if(cancelled||rd(shared,24)!=seq||rd(shared,32)!=~value){fprintf(stderr,"ACK failure seq=%u\n",seq);goto out;}
 }
 if(!cancelled){puts("PASS: 100 E907 shared-memory request/reply checks; no PWM/UART access");rc=0;}
 out:
 if(touched){
  /* Hold the core before releasing memory. SET/CLR changes only its bits. */
  wr(reg,0x58,RESET_MASK);pause_ms();
  if((rd(reg,0x54)&RESET_MASK)!=RESET_MASK){
   fprintf(stderr,"FATAL: reset not confirmed; retaining allocated memory and process. Power cycle required.\n");
   for(;;)sleep(1);
  }
  wr(reg,0x24c,old_base);wr(reg,0x14,MUX_MASK & ~old_mux);wr(reg,0x10,MUX_MASK & old_mux);
  wr(reg,0x38,CLOCK_MASK & ~old_clock);wr(reg,0x34,CLOCK_MASK & old_clock);
  uint32_t mux=rd(reg,0x0c),clock=rd(reg,0x30),reset=rd(reg,0x54),base=rd(reg,0x24c);
  printf("RESTORE mux=%08x clock=%08x reset=%08x base=%08x\n",mux,clock,reset,base);
  if((mux&MUX_MASK)!=(old_mux&MUX_MASK)||(clock&CLOCK_MASK)!=(old_clock&CLOCK_MASK)||(reset&RESET_MASK)!=(old_reset&RESET_MASK)||base!=old_base)rc=1;
 }
 if(leased){
  if(saved_pwm)for(unsigned i=0;i<4;i++){
   wr(pwm,i*0x14+8,0x1e);wr(pwm,i*0x14,old_pwm[i][1]);wr(pwm,0xb0+4*i,old_pwm[i][2]);wr(pwm,i*0x14+8,old_pwm[i][0]);
   wr(pad,poff[i],old_pad[i]);
   if(rd(pad,poff[i])!=old_pad[i]||rd(pwm,i*0x14+8)!=old_pwm[i][0]||rd(pwm,i*0x14)!=old_pwm[i][1]||rd(pwm,0xb0+4*i)!=old_pwm[i][2])rc=1;
  }
  wr(periph,0xbc,(15U<<23)&~old_chclk);wr(periph,0xb8,(15U<<23)&old_chclk);
  wr(periph,0xcc,1U&~old_pclk);wr(periph,0xc8,1U&old_pclk);
  if(driver_action("bind")){perror("restore Linux PWM1 binding");rc=1;}
  else puts("RESTORE: PWM1 registers/pads restored and Linux driver rebound");
 }
 if(allocated&&release(phys,mem)){fprintf(stderr,"CMM free failed\n");rc=1;}
 if(initialized&&deinit()){fprintf(stderr,"SYS deinit failed\n");rc=1;}
 if(lib)dlclose(lib);
 if(reg!=MAP_FAILED)munmap((void*)reg,4096);
 if(pad!=MAP_FAILED)munmap((void*)pad,4096);
 if(periph!=MAP_FAILED)munmap((void*)periph,4096);
 if(pwm!=MAP_FAILED)munmap((void*)pwm,4096);
 if(fd>=0)close(fd);
 if(lockfd>=0)close(lockfd);
 return rc;
}
