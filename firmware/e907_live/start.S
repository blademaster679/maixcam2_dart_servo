.section .text.start,"ax",@progbits
.globl _start
_start:
    auipc a0, 1
    csrwi mie, 0
    csrci mstatus, 8
    la sp, __stack_top
    la t0, trap
    csrw mtvec, t0
    csrr t0, mhartid
    sw t0, 4(a0)
    csrr t0, misa
    sw t0, 8(a0)
    csrr t0, 0x7c1
    sw t0, 12(a0)
    tail live_main
.balign 4
trap:
    la t1, __shared
    csrr t0, mcause
    sw t0, 36(t1)
    csrr t0, mepc
    sw t0, 40(t1)
    csrr t0, mtval
    sw t0, 44(t1)
    li t0, 0xBAD00907
    sw t0, 0(t1)
1:  j 1b
