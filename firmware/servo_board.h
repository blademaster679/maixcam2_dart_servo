#ifndef SERVO_BOARD_H
#define SERVO_BOARD_H
#include <stdint.h>
/* V1.1: retain servo numbering by H2 socket position, not PWM number.
 * Logical mask bits 0..3 select servos 1..4. Never use BMI270 pins B2/B3. */
#define SERVO_CARRIER_REVISION "V1.1"
#define SERVO_ABI_VERSION 2U
#define SERVO_MAGIC 0x53525632U
#define SERVO_LEASE_MAGIC 0x50574D32U
#define SERVO_PWM_BASE 0x06060000U
#define SERVO_PWM_MAP_BYTES 0x2000U
#define SERVO_CHANNEL_CLOCK_MASK ((1U<<19)|(1U<<20)|(1U<<25)|(1U<<26))
typedef struct {
    unsigned pwm, gpio, connector_pin;
    uint32_t pad;
    const char *pin;
} servo_channel_t;
static const servo_channel_t servo_channels[4] = {
    {7,31,12,0x02304060U,"A31"},
    {6,30,15,0x02304054U,"A30"},
    {1,25, 9,0x02304018U,"A25"},
    {0,24, 6,0x0230400CU,"A24"}
};
static inline int servo_pwm_allowed(unsigned pwm){return pwm==0||pwm==1||pwm==6||pwm==7;}
static inline unsigned servo_pwm_group_offset(unsigned i){return (servo_channels[i].pwm/4)*0x1000U;}
static inline unsigned servo_control_offset(unsigned i){return servo_pwm_group_offset(i)+8+20*(servo_channels[i].pwm%4);}
static inline unsigned servo_low_offset(unsigned i){return servo_pwm_group_offset(i)+20*(servo_channels[i].pwm%4);}
static inline unsigned servo_high_offset(unsigned i){return servo_pwm_group_offset(i)+0xb0+4*(servo_channels[i].pwm%4);}
#endif
