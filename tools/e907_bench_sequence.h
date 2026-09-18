#ifndef E907_BENCH_SEQUENCE_H
#define E907_BENCH_SEQUENCE_H
#include <stdint.h>

#define E907_BENCH_MAX_CYCLES 10000U
/* Strict decimal input: no signs, whitespace, zero, or overflow. */
static inline int e907_bench_parse_cycles(const char *text, unsigned *cycles)
{
    unsigned value = 0;
    if (!text || !*text) return -1;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return -1;
        unsigned digit = (unsigned)(*text - '0');
        if (value > (E907_BENCH_MAX_CYCLES - digit) / 10) return -1;
        value = value * 10 + digit;
    }
    if (!value) return -1;
    *cycles = value;
    return 0;
}

/* Fixed, bounded bench sequence. Callbacks return nonzero on error/cancel.
 * set must acknowledge and verify the output; stop must disable all outputs.
 * now_ms is monotonic; wait_ms must remain interruptible. */
struct e907_bench_io {
    void *ctx;
    uint64_t (*now_ms)(void *);
    int (*wait_ms)(void *, unsigned);
    int (*set)(void *, unsigned channel_mask, unsigned pulse_us);
    int (*stop)(void *);
    void (*cycle_begin)(void *, unsigned cycle, unsigned total);
};

static int e907_bench_run_mode(const struct e907_bench_io *io, unsigned mask, unsigned cycles, int together)
{
    static const unsigned pulses[] = {1500, 1550, 1500, 1450, 1500,
                                      1600, 1500, 1400, 1500};
    if (!mask || (mask & ~15U) || !cycles || cycles > E907_BENCH_MAX_CYCLES) return -1;
    for (unsigned cycle = 1; cycle <= cycles; ++cycle) {
        if (io->cycle_begin) io->cycle_begin(io->ctx, cycle, cycles);
        for (unsigned ch = 0; ch < (together ? 1U : 4U); ++ch) {
            unsigned selected = together ? mask : mask & (1U << ch);
            if (!selected) continue;
            if (io->stop(io->ctx)) return -1;
            for (unsigned step = 0; step < sizeof(pulses)/sizeof(pulses[0]); ++step) {
                uint64_t end = io->now_ms(io->ctx) + (step == 0 ? 3000 : 1500);
                do {
                    if (io->set(io->ctx, selected, pulses[step])) goto failed;
                    uint64_t now = io->now_ms(io->ctx);
                    if (now >= end) break;
                    unsigned wait = end - now > 100 ? 100 : (unsigned)(end - now);
                    if (io->wait_ms(io->ctx, wait)) goto failed;
                } while (io->now_ms(io->ctx) < end);
            }
            if (io->stop(io->ctx)) return -1;
        }
    }
    return 0;
failed:
    (void)io->stop(io->ctx);
    return -1;
}
static inline int e907_bench_run(const struct e907_bench_io *io, unsigned mask, unsigned cycles)
{
    return e907_bench_run_mode(io, mask, cycles, 0);
}
#endif
