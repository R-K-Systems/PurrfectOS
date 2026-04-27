[bits 32]
section .text
global start
extern _kmain

start:
    mov esp, 0x90000
    call _kmain

.hang:
    jmp .hang
