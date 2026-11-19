#ifndef SERVO_PORT0_ANGLE_SEQUENCE_H
#define SERVO_PORT0_ANGLE_SEQUENCE_H
#include "e907_bench_sequence.h"
#include "../firmware/servo_board.h"
/* 2 s neutral, 5 s outward ramp, 5 s target, 5 s return, 2 s neutral.
 * These are pulse targets; the nominal +45 degrees is not measured feedback. */
static int servo_port0_angle_dwell(const struct e907_bench_io *io,unsigned pulse,unsigned ms){
    uint64_t end=io->now_ms(io->ctx)+ms;
    do{
        if(io->set(io->ctx,8U,pulse))return -1;
        uint64_t now=io->now_ms(io->ctx);
        if(now>=end)break;
        unsigned wait=end-now>100?100:(unsigned)(end-now);
        if(io->wait_ms(io->ctx,wait))return -1;
    }while(io->now_ms(io->ctx)<end);
    return 0;
}
static int servo_port0_angle_run_target(const struct e907_bench_io *io,int hold_target,unsigned target){
    if(target<SERVO_PORT0_NEUTRAL_US||target>SERVO_PORT0_PLUS45_US)return -1;
    if(io->stop(io->ctx))return -1;
    if(servo_port0_angle_dwell(io,SERVO_PORT0_NEUTRAL_US,2000))goto failed;
    const unsigned travel=target-SERVO_PORT0_NEUTRAL_US;
    /* Fifty 100ms steps land exactly on either endpoint, including travel
     * not divisible by 10 us (e.g. the measured-calibration estimate 375 us). */
    for(unsigned step=1;step<=50;++step)
        if(servo_port0_angle_dwell(io,SERVO_PORT0_NEUTRAL_US+travel*step/50,100))goto failed;
    if(hold_target){
        /* Keep sending the same target/heartbeat without restarting PWM.
         * Cancellation or a failed acknowledgment takes the stop path. */
        for(;;)if(servo_port0_angle_dwell(io,target,100))goto failed;
    }
    if(servo_port0_angle_dwell(io,target,5000))goto failed;
    for(unsigned step=1;step<=50;++step)
        if(servo_port0_angle_dwell(io,target-travel*step/50,100))goto failed;
    if(servo_port0_angle_dwell(io,SERVO_PORT0_NEUTRAL_US,2000))goto failed;
    return io->stop(io->ctx);
failed:
    (void)io->stop(io->ctx);return -1;
}
static inline int servo_port0_angle_run_mode(const struct e907_bench_io *io,int hold_target){
    return servo_port0_angle_run_target(io,hold_target,SERVO_PORT0_PLUS45_US);
}
static inline int servo_port0_angle_run(const struct e907_bench_io *io){
    return servo_port0_angle_run_mode(io,0);
}
#endif
