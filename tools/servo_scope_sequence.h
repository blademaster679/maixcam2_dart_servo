#ifndef SERVO_SCOPE_SEQUENCE_H
#define SERVO_SCOPE_SEQUENCE_H
#include <stdint.h>

#define SERVO_SCOPE_DURATION_MS 60000U
#define SERVO_SCOPE_PULSE_US 1500U
#define SERVO_SCOPE_KEEPALIVE_MS 100U

static inline int servo_hold_parse_pulse(const char *text,unsigned *pulse){
    unsigned value=0;
    if(!text||!*text)return -1;
    for(;*text;++text){
        if(*text<'0'||*text>'9')return -1;
        unsigned digit=(unsigned)(*text-'0');
        if(value>(1600U-digit)/10U)return -1;
        value=value*10U+digit;
    }
    if(value<1400U||value>1600U)return -1;
    *pulse=value;return 0;
}

struct servo_scope_io {
    void *ctx;
    uint64_t (*now_ms)(void *);
    int (*wait_ms)(void *, unsigned);
    int (*set)(void *, unsigned mask, unsigned pulse_us);
    int (*stop)(void *);
};

/* The minute starts after the first successfully verified target. Keep the
 * output continuous; set() must avoid restarting an unchanged PWM target.
 * Every exit attempts to stop. The caller still owns resource restoration. */
static inline int servo_scope_run(const struct servo_scope_io *io, unsigned mask)
{
    if (!io || !io->now_ms || !io->wait_ms || !io->set || !io->stop ||
        !mask || (mask & ~15U) || (mask & (mask - 1U))) return -1;
    if (io->stop(io->ctx)) return -1;
    int result = -1;
    if (io->set(io->ctx, mask, SERVO_SCOPE_PULSE_US)) goto out;
    uint64_t deadline = io->now_ms(io->ctx) + SERVO_SCOPE_DURATION_MS;
    for (;;) {
        uint64_t now = io->now_ms(io->ctx);
        if (now >= deadline) break;
        unsigned wait = deadline - now > SERVO_SCOPE_KEEPALIVE_MS
                      ? SERVO_SCOPE_KEEPALIVE_MS : (unsigned)(deadline - now);
        if (io->wait_ms(io->ctx, wait)) goto out;
        if (io->now_ms(io->ctx) >= deadline) break;
        if (io->set(io->ctx, mask, SERVO_SCOPE_PULSE_US)) goto out;
    }
    result = 0;
out:
    if (io->stop(io->ctx)) result = -1;
    return result;
}
#endif
