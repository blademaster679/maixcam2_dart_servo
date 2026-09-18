/* Minimal position-independent RV32 probe. No UART/PWM/interrupt use.
 * Shared page: +0x1000. RV32 caches are left at reset settings.
 * Host owns command +0x10 and sequence +0x14; core owns acknowledgement.
 */
.section .text.start,"ax",@progbits
.globl _start
_start:
    auipc s0, 1
    csrwi mie, 0
    csrci mstatus, 8
    la t0, trap
    csrw mtvec, t0
    csrr t0, mhartid
    sw t0, 4(s0)
    csrr t0, misa
    sw t0, 8(s0)
    csrr t0, 0x7c1 /* T-Head MHCR: observe reset cache controls */
    sw t0, 12(s0)
    li t0, 0x45393037 /* E907 */
    fence iorw, iorw
    sw t0, 0(s0)
loop:
    lw t0, 20(s0)
    lw t1, 24(s0)
    beq t0, t1, loop
    fence iorw, iorw
    lw t1, 16(s0)
    lw t2, 28(s0)
    xori t2, t2, -1
    sw t2, 32(s0)
    fence iorw, iorw
    sw t0, 24(s0)
    j loop
trap:
    csrr t0, mcause
    sw t0, 36(s0)
    csrr t0, mepc
    sw t0, 40(s0)
    csrr t0, mtval
    sw t0, 44(s0)
    li t0, 0xBAD00907
    sw t0, 0(s0)
1:  j 1b
