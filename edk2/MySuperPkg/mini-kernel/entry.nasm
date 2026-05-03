global _entry

extern main
extern info;

section .entry
[bits 64]
_entry:
  ; RAX - указатель на структуру с видеобуфером
  test rax, rax
  jz .skip_setup

      mov [rel info], rax

      mov rsp, 0x100000
      mov rbp, rsp

      jmp .to_main

.skip_setup:
    ;mov rax, 0xdedababa
    ;mov [0x300], rax
    ;jmp $

.to_main:
  call main

  jmp $
