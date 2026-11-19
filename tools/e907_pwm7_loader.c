/* Temporary RAM-only E907 probe. Boot sequence: official sipeed SDK
 * boot/bl1/driver/riscv/riscv.c + common/include/chip_reg.h.
 * This runtime CMM loader is experimental integration, not a vendor API.
 * Refuses any core not held in reset with its clock disabled.
 */
#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
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
#define PWM7_MAGIC 0x50373035U
static volatile sig_atomic_t cancelled;
static void onsignal(int s){(void)s;cancelled=1;}
static void barrier(void){__asm__ volatile("dmb sy" ::: "memory");}
static void pause_ms(void){struct timespec t={0,1000000};nanosleep(&t,NULL);}
static uint32_t rd(volatile uint32_t *r,unsigned off){uint32_t v=r[off/4];barrier();return v;}
static void wr(volatile uint32_t *r,unsigned off,uint32_t v){barrier();r[off/4]=v;barrier();}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
/* The Python supervisor holds all three locks and leases PWM1 before exec. */
int main(int argc,char **argv){
 int rc=1,fd=-1,initialized=0,allocated=0,touched=0;void *lib=NULL,*mem=NULL;
 volatile uint32_t *reg=MAP_FAILED,*shared=NULL;uint64_t phys=0;
 uint32_t old_mux=0,old_clock=0,old_reset=0,old_base=0;
 int (*init)(void)=NULL,(*deinit)(void)=NULL;
 int (*alloc)(uint64_t*,void**,uint32_t,uint32_t,const char*)=NULL;
 int (*release)(uint64_t,void*)=NULL;
 unsigned char blob[4096];size_t len=0;FILE *file=NULL;
 if(argc!=3||strcmp(argv[1],"--leased-pwm7")){fprintf(stderr,"Run test_pwm7_e907.py --run instead\n");return 2;}
 if(access("/sys/bus/platform/devices/6061000.pwm1/driver",F_OK)==0){fprintf(stderr,"PWM1 must be leased\n");return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);signal(SIGINT,onsignal);signal(SIGTERM,onsignal);signal(SIGHUP,onsignal);
 file=fopen(argv[2],"rb");if(!file){perror("firmware");goto out;}
 len=fread(blob,1,sizeof(blob),file);int read_error=ferror(file);fclose(file);
 if(read_error||!len||len>=sizeof(blob)){fprintf(stderr,"Invalid image size\n");goto out;}
 fd=open("/dev/mem",O_RDWR|O_SYNC);if(fd<0){perror("/dev/mem");goto out;}
 reg=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,fd,COMM_BASE);if(reg==MAP_FAILED){perror("map registers");goto out;}
 old_mux=rd(reg,0x0c);old_clock=rd(reg,0x30);old_reset=rd(reg,0x54);old_base=rd(reg,0x24c);
 printf("PRECHECK mux=%08x clock=%08x reset=%08x base=%08x\n",old_mux,old_clock,old_reset,old_base);
 if((old_reset&RESET_MASK)!=RESET_MASK||(old_clock&CLOCK_MASK)||old_base){fprintf(stderr,"REFUSED: E907 is not in expected unused reset state\n");goto out;}
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
 if(cancelled||rd(shared,0)!=PWM7_MAGIC||rd(shared,12)!=0){fprintf(stderr,"Dedicated PWM7 firmware not ready\n");goto out;}
 wr(shared,20,1);wr(shared,16,1);
 double begin=seconds(),next=begin;unsigned beat=1;
 while(!cancelled && seconds()-begin<305){
  uint32_t state=rd(shared,24);
  if(rd(shared,0)!=PWM7_MAGIC)break;
  if(state==2){
   double elapsed=((uint64_t)rd(shared,32)<<32|rd(shared,28))/24000000.0;
   printf("DONE E907 elapsed_s=%.6f host_elapsed_s=%.6f\n",elapsed,seconds()-begin);rc=0;break;
  }
  if(state>2)break;
  if(seconds()>=next){wr(shared,20,++beat);next=seconds()+0.1;}
  pause_ms();
 }
 if(rc)fprintf(stderr,"Interrupted, watchdog or firmware failure; stopping\n");
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
 if(allocated&&release(phys,mem)){fprintf(stderr,"CMM free failed\n");rc=1;}
 if(initialized&&deinit()){fprintf(stderr,"SYS deinit failed\n");rc=1;}
 if(lib)dlclose(lib);
 if(reg!=MAP_FAILED)munmap((void*)reg,4096);
 if(fd>=0)close(fd);
 printf("E907_END,%d\n",rc);return rc;
}
