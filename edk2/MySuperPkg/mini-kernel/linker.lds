PHDRS {
  load      PT_LOAD   FLAGS(5);
}

SECTIONS {
  .text : ALIGN(CONSTANT(COMMONPAGESIZE)) {
    KEEP(*(.entry))
    *(.text .text.*)
    *(.rodata .rodata.*)
  } : load

  .data ALIGN(ALIGNOF(.text)) : ALIGN(CONSTANT(COMMONPAGESIZE)) {
    *(.data .data.*)
    *(.bss .bss.*)
  } : load

  /DISCARD/ : {
    *(.note.GNU-stack)
    *(.gnu_debuglink)
    *(.interp)
    *(.dynsym)
    *(.dynstr)
    *(.hash .gnu.hash)
    *(.comment)
    *(.dynamic)
    *(.note.gnu.build-id)
  }
}
