[bits 16]
[org 0x1000]

start:
    xor ax, ax
    mov ds, ax

    mov si, kernel_msg
    call print_string

hang:
    hlt
    jmp hang

print_char:
    mov ah, 0x0E
    int 0x10
    ret

print_string:
    lodsb
    test al, al
    jz .done
    call print_char
    jmp print_string
.done:
    ret

kernel_msg db "PurrfectOS cekirdegine hos geldin :3", 0x0D, 0x0A, 0

times 2048-($-$$) db 0
