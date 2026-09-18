OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { *(.text.start) *(.text*) }
  .rodata : { *(.rodata*) *(.srodata*) }
  .data : { *(.data*) *(.sdata*) }
  .bss (NOLOAD) : { *(.bss*) *(.sbss*) *(COMMON) }
  __image_end = .;
  __shared = 0x1000;
  __stack_top = 0x4000;
  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.eh_frame*) }
  ASSERT(__image_end < 0x1000, "Image must fit before shared page")
}
