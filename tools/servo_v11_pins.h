#ifndef SERVO_V11_PINS_H
#define SERVO_V11_PINS_H
/* Linux pin lease shared by the A53/MaixCDK and E907 loader paths.
 * PWM ownership must be checked separately before acquiring these pins. */
#include "../firmware/servo_board.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd,lock,gpio,detached,saved;
    volatile uint32_t *pad;
    uint32_t old_pad[4];
    char i2c_driver[PATH_MAX];
} servo_pin_lease;
static inline void servo_pins_init(servo_pin_lease *p){memset(p,0,sizeof(*p));p->fd=p->lock=p->gpio=-1;}
static inline int servo_sysfs_action(const char *driver,const char *action,const char *device){
    char path[PATH_MAX];
    if(snprintf(path,sizeof(path),"%s/%s",driver,action)>=(int)sizeof(path))return -1;
    int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0)return -1;
    ssize_t n=write(fd,device,strlen(device));int error=errno;close(fd);errno=error;
    return n==(ssize_t)strlen(device)?0:-1;
}
static inline int servo_pwm_groups_idle(void){
    char buf[32768];FILE *f=fopen("/sys/kernel/debug/pwm","r");if(!f)return 0;
    size_t n=fread(buf,1,sizeof(buf)-1,f);int bad=ferror(f)||!feof(f);fclose(f);buf[n]=0;
    if(bad)return 0;
    for(unsigned g=0;g<2;++g){
        const char *tag=g?"platform/6061000.pwm1, 4 PWM devices":"platform/6060000.pwm0, 4 PWM devices";
        const char *start=strstr(buf,tag);if(!start)return 0;
        const char *end=strstr(start,"\n\n");if(!end)end=buf+n;
        const char *requested=strstr(start,"requested"),*enabled=strstr(start,"enabled");
        if((requested&&requested<end)||(enabled&&enabled<end))return 0;
    }
    return 1;
}
static inline int servo_pin_clients_idle(void){
    FILE *cmdline=fopen("/proc/cmdline","r");if(!cmdline)return 0;
    char boot[4096];size_t boot_size=fread(boot,1,sizeof(boot)-1,cmdline);int boot_error=ferror(cmdline);fclose(cmdline);boot[boot_size]=0;
    if(boot_error||strstr(boot,"console=ttyS1"))return 0;
    DIR *dir=opendir("/sys/bus/i2c/devices");if(!dir)return 0;
    struct dirent *entry;int ok=1;char path[PATH_MAX];
    while((entry=readdir(dir))!=NULL)if(!strncmp(entry->d_name,"0-",2)){
        snprintf(path,sizeof(path),"/sys/bus/i2c/devices/%s/driver",entry->d_name);
        if(access(path,F_OK)==0){ok=0;break;}
    }
    closedir(dir);if(!ok)return 0;
    dir=opendir("/proc");if(!dir)return 0;
    while(ok&&(entry=readdir(dir))!=NULL){
        if(entry->d_name[0]<'0'||entry->d_name[0]>'9')continue;
        snprintf(path,sizeof(path),"/proc/%s/fd",entry->d_name);
        DIR *fds=opendir(path);if(!fds)continue;
        struct dirent *fd;
        while((fd=readdir(fds))!=NULL){
            char link[PATH_MAX],target[PATH_MAX];
            int count=snprintf(link,sizeof(link),"%s/%s",path,fd->d_name);
            if(count<0||count>=(int)sizeof(link)){ok=0;break;}
            ssize_t n=readlink(link,target,sizeof(target)-1);if(n<0)continue;target[n]=0;
            if(!strcmp(target,"/dev/i2c-0")||!strcmp(target,"/dev/ttyS1")) {ok=0;break;}
        }
        closedir(fds);
    }
    closedir(dir);return ok;
}
static inline int servo_pins_release(servo_pin_lease *p){
    int rc=0;
    if(p->gpio>=0){close(p->gpio);p->gpio=-1;}
    if(p->saved){
        for(unsigned i=0;i<4;++i){unsigned offset=(servo_channels[i].pad-0x02304000U)/4;
            p->pad[offset]=p->old_pad[i];__sync_synchronize();
            if(p->pad[offset]!=p->old_pad[i])rc=-1;
        }
        p->saved=0;
    }
    if(p->detached){
        if(servo_sysfs_action(p->i2c_driver,"bind","4850000.i2c"))rc=-1;
        else p->detached=0;
    }
    if(p->pad){munmap((void*)p->pad,4096);p->pad=NULL;}
    if(p->fd>=0){close(p->fd);p->fd=-1;}
    if(p->lock>=0){close(p->lock);p->lock=-1;}
    return rc;
}
static inline int servo_pins_acquire(servo_pin_lease *p){
    if(geteuid()!=0){fprintf(stderr,"V1.1 pin lease requires root\n");return -1;}
    p->lock=open("/run/lock/maixcam2-servo-v11.lock",O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
    if(p->lock<0||flock(p->lock,LOCK_EX|LOCK_NB)){fprintf(stderr,"V1.1 servo pins already leased\n");return -1;}
    if(!servo_pin_clients_idle()){fprintf(stderr,"REFUSED: I2C0 client or I2C0/UART1 user on servo pins\n");return -1;}
    p->fd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(p->fd<0)return -1;
    void *map=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,p->fd,0x02304000U);
    if(map==MAP_FAILED)return -1;
    p->pad=(volatile uint32_t*)map;
    for(unsigned i=0;i<4;++i)p->old_pad[i]=p->pad[(servo_channels[i].pad-0x02304000U)/4];
    p->saved=1;
    const char *driver="/sys/bus/platform/devices/4850000.i2c/driver";
    if(access(driver,F_OK)==0){
        if(!realpath(driver,p->i2c_driver)||servo_sysfs_action(p->i2c_driver,"unbind","4850000.i2c"))return -1;
        p->detached=1;
    }
    int chip=open("/dev/gpiochip0",O_RDONLY|O_CLOEXEC);if(chip<0)return -1;
    struct gpiochip_info info;memset(&info,0,sizeof(info));
    if(ioctl(chip,GPIO_GET_CHIPINFO_IOCTL,&info)||strcmp(info.label,"4800000.gpio")){close(chip);return -1;}
    struct gpiohandle_request request;memset(&request,0,sizeof(request));
    request.flags=GPIOHANDLE_REQUEST_INPUT;request.lines=4;strcpy(request.consumer_label,"servo-v11");
    for(unsigned i=0;i<4;++i){
        struct gpioline_info line;memset(&line,0,sizeof(line));line.line_offset=servo_channels[i].gpio;
        if(ioctl(chip,GPIO_GET_LINEINFO_IOCTL,&line)||(line.flags&(GPIOLINE_FLAG_KERNEL|GPIOLINE_FLAG_IS_OUT))){close(chip);return -1;}
        request.lineoffsets[i]=servo_channels[i].gpio;
    }
    if(ioctl(chip,GPIO_GET_LINEHANDLE_IOCTL,&request)){close(chip);return -1;}
    close(chip);p->gpio=request.fd;return 0;
}
static inline int servo_pins_mux(servo_pin_lease *p,unsigned i){
    if(!p->saved||i>=4)return -1;
    unsigned offset=(servo_channels[i].pad-0x02304000U)/4;
    uint32_t value=(p->pad[offset]&~(7U<<16))|(3U<<16);
    p->pad[offset]=value;__sync_synchronize();return p->pad[offset]==value?0:-1;
}
#endif
