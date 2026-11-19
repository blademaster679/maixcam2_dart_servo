#define _GNU_SOURCE
#include "servo_v11_pins.h"
#include "servo_scope_sequence.h"
#include <signal.h>
#include <time.h>
#include <sys/utsname.h>

/* User's PCB silkscreen "1" is PWM01/A25, logical servo index 2. */
#define TARGET_INDEX 2U
#define TARGET_MASK (1U << TARGET_INDEX)
static volatile sig_atomic_t cancelled;
static void on_signal(int signo){(void)signo;cancelled=1;}

struct native_scope {
    servo_pin_lease pins;
    volatile uint32_t *pwm;
    char chip[PATH_MAX],node[PATH_MAX];
    unsigned hz,period_ns,exported,calls;
};

static uint32_t read32(volatile uint32_t *p,unsigned offset){
    uint32_t value=p[offset/4];__sync_synchronize();return value;
}
static uint64_t now_ms(void *ctx){
    struct timespec ts;(void)ctx;
    if(clock_gettime(CLOCK_MONOTONIC,&ts)){cancelled=1;return 0;}
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
static int wait_ms(void *ctx,unsigned ms){
    uint64_t deadline=now_ms(ctx)+ms;
    while(!cancelled&&now_ms(ctx)<deadline){struct timespec ts={0,1000000};nanosleep(&ts,NULL);}
    return cancelled?-1:0;
}
static int write_text(const char *dir,const char *field,const char *text){
    char path[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/%s",dir,field)>=(int)sizeof(path))return -1;
    int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0){perror(path);return -1;}
    size_t size=strlen(text);
    ssize_t count=write(fd,text,size);int error=errno;close(fd);errno=error;
    if(count!=(ssize_t)size){perror(path);return -1;}
    return 0;
}
static int write_value(const char *dir,const char *field,unsigned value){
    char text[32];snprintf(text,sizeof(text),"%u",value);return write_text(dir,field,text);
}
static int read_value(const char *dir,const char *field,unsigned *value){
    char path[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/%s",dir,field)>=(int)sizeof(path))return -1;
    FILE *file=fopen(path,"r");if(!file)return -1;
    int count=fscanf(file,"%u",value);fclose(file);return count==1?0:-1;
}
static int find_chip(char *path,size_t size){
    const char *parent="/sys/bus/platform/devices/6060000.pwm0/pwm";
    DIR *dir=opendir(parent);if(!dir)return -1;
    unsigned found=0;struct dirent *entry;
    while((entry=readdir(dir)))if(!strncmp(entry->d_name,"pwmchip",7)){
        if(snprintf(path,size,"%s/%s",parent,entry->d_name)>=(int)size){closedir(dir);return -1;}
        ++found;
    }
    closedir(dir);return found==1?0:-1;
}
static int stop_output(void *ctx){
    struct native_scope *scope=ctx;int result=0;
    if(scope->exported){
        if(write_value(scope->node,"enable",0))result=-1;
        if(write_value(scope->chip,"unexport",1))result=-1;
        else scope->exported=0;
    }
    if(scope->pwm!=MAP_FAILED)for(unsigned i=0;i<4;++i)
        if(read32(scope->pwm,servo_control_offset(i))&1U)result=-1;
    return result;
}
static int set_output(void *ctx,unsigned mask,unsigned pulse){
    struct native_scope *scope=ctx;
    if(cancelled||mask!=TARGET_MASK||pulse!=SERVO_SCOPE_PULSE_US)return -1;
    if(!scope->exported){
        if(access(scope->node,F_OK)==0){fputs("REFUSED: PWM01 is already exported\n",stderr);return -1;}
        if(servo_pins_mux(&scope->pins,TARGET_INDEX)||write_value(scope->chip,"export",1))return -1;
        scope->exported=1;
        if(write_value(scope->node,"period",scope->period_ns)||
           write_text(scope->node,"polarity","normal")||
           write_value(scope->node,"duty_cycle",pulse*1000U)||
           write_value(scope->node,"enable",1))return -1;
    }
    /* Subsequent calls only read: do not restart PWM every 100 ms. */
    unsigned period,duty,enabled;
    if(read_value(scope->node,"period",&period)||period!=scope->period_ns||
       read_value(scope->node,"duty_cycle",&duty)||duty!=pulse*1000U||
       read_value(scope->node,"enable",&enabled)||enabled!=1)return -1;
    for(unsigned i=0;i<4;++i){
        uint32_t control=read32(scope->pwm,servo_control_offset(i));
        if(i!=TARGET_INDEX){if(control&1U)return -1;continue;}
        if(control!=0x1fU||read32(scope->pwm,servo_high_offset(i))!=24U*pulse||
           read32(scope->pwm,servo_low_offset(i))!=24000000U/scope->hz-24U*pulse)return -1;
    }
    if(((scope->pins.pad[(servo_channels[TARGET_INDEX].pad-0x02304000U)/4]>>16)&7U)!=3U)return -1;
    if(scope->calls++==0)printf("SCOPE_ACTIVE backend=Linux pwm=1 pin=A25 hz=%u pulse_us=%u duration_ms=%u\n",
                               scope->hz,pulse,SERVO_SCOPE_DURATION_MS);
    if(scope->calls%10==0)printf("SCOPE_CHECK,%llu,4,%u,%u,0\n",(unsigned long long)now_ms(scope),pulse,scope->hz);
    return 0;
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--help")){
        puts("Usage: servo_scope_linux --hz 333|50\n"
             "PCB silkscreen 1 = PWM01/A25; 1500 us continuously for 60 seconds.\n"
             "Requires root on the AArch64 board; use run_scope.py to coordinate the display.");
        return 0;
    }
    if(argc!=3||strcmp(argv[1],"--hz")||(strcmp(argv[2],"333")&&strcmp(argv[2],"50"))){
        fputs("Usage: servo_scope_linux --hz 333|50\n",stderr);return 2;
    }
    struct utsname system;
    if(uname(&system)||strcmp(system.machine,"aarch64")||geteuid()!=0){
        fputs("REFUSED: requires root on the AArch64 board\n",stderr);return 1;
    }
    struct native_scope scope;memset(&scope,0,sizeof(scope));servo_pins_init(&scope.pins);
    scope.pwm=MAP_FAILED;scope.hz=(unsigned)strtoul(argv[2],NULL,10);scope.period_ns=1000000000U/scope.hz;
    int result=1;
    setvbuf(stdout,NULL,_IOLBF,0);
    signal(SIGINT,on_signal);signal(SIGTERM,on_signal);signal(SIGHUP,on_signal);
    if(!servo_pwm_groups_idle()){fputs("REFUSED: PWM0/PWM1 not idle\n",stderr);goto out;}
    if(find_chip(scope.chip,sizeof(scope.chip))||
       snprintf(scope.node,sizeof(scope.node),"%s/pwm1",scope.chip)>=(int)sizeof(scope.node))goto out;
    if(servo_pins_acquire(&scope.pins))goto out;
    scope.pwm=mmap(NULL,SERVO_PWM_MAP_BYTES,PROT_READ,MAP_SHARED,scope.pins.fd,SERVO_PWM_BASE);
    if(scope.pwm==MAP_FAILED){perror("map PWM");goto out;}
    for(unsigned i=0;i<4;++i)if(read32(scope.pwm,servo_control_offset(i))&1U){
        fputs("REFUSED: hardware servo PWM already enabled\n",stderr);goto out;
    }
    {
        const struct servo_scope_io io={&scope,now_ms,wait_ms,set_output,stop_output};
        printf("SCOPE_BEGIN backend=Linux mask=0x4 frequency_hz=%u pulse_us=1500 duration_ms=60000\n",scope.hz);
        result=servo_scope_run(&io,TARGET_MASK)?1:0;
    }
out:
    if(stop_output(&scope))result=1;
    if(servo_pins_release(&scope.pins))result=1;
    if(scope.pwm!=MAP_FAILED)munmap((void*)scope.pwm,SERVO_PWM_MAP_BYTES);
    printf("SCOPE_END,%d\n",result);return result;
}
