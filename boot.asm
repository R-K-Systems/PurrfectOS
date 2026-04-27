[bits 16]
[org 0x7C00]

KERNEL_OFFSET equ 0x1000
KERNEL_SECTORS equ 48
CODE_SEG equ 0x08
DATA_SEG equ 0x10

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl
    call clear_screen

    mov si, boot_msg
    call print_string

    call load_kernel
    call enter_protected_mode

hang:
    hlt
    jmp hang

; BIOS teletype output (int 10h, ah=0x0E)
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

clear_screen:
    ; 80x25 text mode'u yeniden ayarla, ekran temizlenir ve imlec sifirlanir.
    mov ax, 0x0003
    int 0x10
    ret

load_kernel:
    mov ah, 0x42        ; LBA read (INT 13h extensions)
    mov dl, [boot_drive]
    mov si, disk_address_packet
    int 0x13
    jc disk_error
    ret

enter_protected_mode:
    cli
    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 0x1
    mov cr0, eax

    jmp CODE_SEG:protected_mode_start

[bits 32]
protected_mode_start:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    mov eax, KERNEL_OFFSET
    jmp eax

[bits 16]

gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

disk_address_packet:
    db 0x10             ; packet size
    db 0x00             ; reserved
    dw KERNEL_SECTORS   ; sector count
    dw KERNEL_OFFSET    ; destination offset
    dw 0x0000           ; destination segment
    dq 0x0000000000000001 ; LBA start (sector after boot sector)

disk_error:
    mov si, disk_err_msg
    call print_string
    jmp hang

boot_msg db "PurrfectOS yukleniyor...", 0x0D, 0x0A, 0
disk_err_msg db "Disk okuma hatasi!", 0x0D, 0x0A, 0
boot_drive db 0

times 510-($-$$) db 0
dw 0xAA55
