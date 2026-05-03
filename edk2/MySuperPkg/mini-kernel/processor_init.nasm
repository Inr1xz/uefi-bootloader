; См. x86 mem map, памамять гарантированно доступная для использования
; начинастся с 500h и до 7BFFh

org 1000h
use16
START:
    xor ax, ax
    mov ds, ax

    cli

    lgdt [tmp_gdtr]

    mov eax, cr0
    or al, 01h
    mov cr0, eax

    jmp 8h:PROTECTED_MODE_ENTRY_POINT

align 8
tmp_gdt:
    db  00h,  00h, 00h, 00h, 00h,       00h,       00h, 00h
    db 0FFh, 0FFh, 00h, 00h, 00h, 10011010b, 11001111b, 00h  ; сегмент кода 32
    db 0FFh, 0FFh, 00h, 00h, 00h, 10010010b, 11001111b, 00h  ; сегмент данных
    db  00h,  00h, 00h, 00h, 00h,       9Ah,       20h, 00h  ; сегмент кода 64
    db  00h,  00h, 00h, 00h, 00h,       92h,       00h, 00h  ; сегмент данных 64

tmp_gdtr dw (8 * 5  - 1)
         dd tmp_gdt

use32
PROTECTED_MODE_ENTRY_POINT:
    mov ax, 10h
    mov ds, ax

    mov ecx, 0xFFFFFFFF

    ; Начинаем переход в long mode --------------------------------------------
    mov eax, cr4
    bts eax, 5
    mov cr4, eax

    mov eax, [pml4_address]  ; временно сформированная таблица
    mov cr3, eax

    mov ecx, 0C0000080h  ; EFER
    rdmsr
    bts eax, 8  ; EFER.LME
    wrmsr

    mov eax, cr0
    bts eax, 31  ; PG = 1
    mov cr0, eax

    jmp 18h:LONG_MODE_ENTRY_POINT

jmp_value:
    .address  dq 0x00
    .selector dw 0x18

use64
LONG_MODE_ENTRY_POINT:
    mov ax, 20h  ; сегмент данных 64 бит
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsi, 0xfee00020
    mov eax, [rsi]
    shr eax, 24  ; apic_id
    shl eax, 12  ; apic_id * 4096

    ; Формируем стек приложения
    lea rsp, [0x100000 + eax]
    mov rbp, rsp

    mov rbx, [entry_point]
    mov [jmp_value.address], rbx

    xor rax, rax
    ; переходим на код, который используется для инициализации данных ---------
    jmp far [jmp_value]

align 8

pml4_address dq 0
entry_point  dq 0

