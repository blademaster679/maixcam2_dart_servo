#ifndef SERVO_FOUR_SWEEP_SEQUENCE_H
#define SERVO_FOUR_SWEEP_SEQUENCE_H
#include <stdint.h>
#include "../firmware/servo_board.h"
#define SERVO_SWEEP_MAX_CYCLES 100U
#define SERVO_SWEEP_UPDATE_MS 20U
#define SERVO_SWEEP_RAMP_MS 5000U
#define SERVO_SWEEP_MIN_RAMP_MS 1000U
#define SERVO_SWEEP_MAX_RAMP_MS 10000U
struct servo_sweep_io {
    void *ctx;
    uint64_t (*now_ms)(void *);
    int (*wait_ms)(void *,unsigned);
    int (*set_offset)(void *,int);
    int (*stop)(void *);
    void (*cycle_begin)(void *,unsigned,unsigned);
};
static int servo_sweep_dwell(const struct servo_sweep_io *io,int offset,unsigned ms){
    uint64_t end=io->now_ms(io->ctx)+ms;
    do{
        if(io->set_offset(io->ctx,offset))return -1;
        uint64_t now=io->now_ms(io->ctx);
        if(now>=end)break;
        unsigned wait=end-now>100?100:(unsigned)(end-now);
        if(io->wait_ms(io->ctx,wait))return -1;
    }while(io->now_ms(io->ctx)<end);
    return 0;
}
static int servo_sweep_ramp(const struct servo_sweep_io *io,int from,int to,unsigned ramp_ms){
    if(ramp_ms<SERVO_SWEEP_MIN_RAMP_MS||ramp_ms>SERVO_SWEEP_MAX_RAMP_MS)return -1;
    const uint64_t duration=ramp_ms;
    const uint64_t denominator=duration*duration*duration;
    const uint64_t begin=io->now_ms(io->ctx);
    const int delta=to-from;
    /* Absolute deadlines avoid adding transport latency to every step.
     * Cubic smoothstep gives zero target velocity at either endpoint.
     * Late callbacks skip stale targets instead of bursting catch-up writes. */
    for(;;){
        uint64_t elapsed=io->now_ms(io->ctx)-begin;
        if(elapsed>=duration)return io->set_offset(io->ctx,to);
        uint64_t next=(elapsed/SERVO_SWEEP_UPDATE_MS+1)*SERVO_SWEEP_UPDATE_MS;
        if(next>duration)next=duration;
        if(io->wait_ms(io->ctx,(unsigned)(next-elapsed)))return -1;
        elapsed=io->now_ms(io->ctx)-begin;
        if(elapsed>duration)elapsed=duration;
        uint64_t numerator=elapsed*elapsed*(3*duration-2*elapsed);
        unsigned distance=(unsigned)(((uint64_t)(delta<0?-delta:delta)*numerator+denominator/2)/denominator);
        int target=from+(delta<0?-(int)distance:(int)distance);
        if(io->set_offset(io->ctx,target))return -1;
        if(elapsed==duration)return 0;
    }
}
/* 2 s neutral, one ramp to negative endpoint; N * (ramp+1s+ramp+1s);
 * one ramp back to neutral, 2 s settle, then disable PWM. */
static int servo_four_sweep_run_timed(const struct servo_sweep_io *io,unsigned cycles,unsigned ramp_ms){
    const int travel=SERVO_SWEEP_TRAVEL_US;
    if(!cycles||cycles>SERVO_SWEEP_MAX_CYCLES
       ||ramp_ms<SERVO_SWEEP_MIN_RAMP_MS||ramp_ms>SERVO_SWEEP_MAX_RAMP_MS)return -1;
    if(io->stop(io->ctx))return -1;
    if(servo_sweep_dwell(io,0,2000)||servo_sweep_ramp(io,0,-travel,ramp_ms))goto failed;
    for(unsigned cycle=1;cycle<=cycles;++cycle){
        if(io->cycle_begin)io->cycle_begin(io->ctx,cycle,cycles);
        if(servo_sweep_ramp(io,-travel,travel,ramp_ms)||servo_sweep_dwell(io,travel,1000)
           ||servo_sweep_ramp(io,travel,-travel,ramp_ms)||servo_sweep_dwell(io,-travel,1000))goto failed;
    }
    if(servo_sweep_ramp(io,-travel,0,ramp_ms)||servo_sweep_dwell(io,0,2000))goto failed;
    return io->stop(io->ctx);
failed:
    (void)io->stop(io->ctx);return -1;
}
static inline int servo_four_sweep_run(const struct servo_sweep_io *io,unsigned cycles){
    return servo_four_sweep_run_timed(io,cycles,SERVO_SWEEP_RAMP_MS);
}
/* Exactly three -375 -> +375 us steps, with 2 s observation holds.
 * Only entry/exit use ramps. Approximate duration: 20 seconds. */
static inline int servo_four_step_run(const struct servo_sweep_io *io){
    const int travel=SERVO_SWEEP_TRAVEL_US;
    if(io->stop(io->ctx))return -1;
    if(servo_sweep_dwell(io,0,2000)||servo_sweep_ramp(io,0,-travel,2000)
       ||servo_sweep_dwell(io,-travel,2000))goto failed;
    for(unsigned cycle=1;cycle<=3;++cycle){
        if(io->cycle_begin)io->cycle_begin(io->ctx,cycle,3);
        /* First dwell call submits the endpoint directly, with no interpolation. */
        if(servo_sweep_dwell(io,travel,2000))goto failed;
        if(cycle<3&&servo_sweep_dwell(io,-travel,2000))goto failed;
    }
    if(servo_sweep_ramp(io,travel,0,2000)||servo_sweep_dwell(io,0,2000))goto failed;
    return io->stop(io->ctx);
failed:
    (void)io->stop(io->ctx);return -1;
}
#endif
