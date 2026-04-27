#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

KERNEL_SECTORS=48
KERNEL_BYTES=$((KERNEL_SECTORS * 512))
DISK_BYTES=$((2 * 1024 * 1024))

nasm -f bin boot.asm -o boot.bin
nasm -f win32 kernel_entry.asm -o kernel_entry.o
gcc -m32 -mno-stack-arg-probe -ffreestanding -Os -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-builtin -fno-ident -c kernel.c -o kernel.o
gcc -m32 -nostdlib -Wl,-e,start -Wl,--image-base,0x0 -Wl,-Ttext,0x1000 -Wl,--file-alignment,16 -Wl,--section-alignment,16 kernel_entry.o kernel.o -o kernel.exe

objcopy -O binary -j .text -j .rdata -j .data kernel.exe kernel.bin

size=$(stat -c%s kernel.bin)
if [ "$size" -gt "$KERNEL_BYTES" ]; then
  echo "Hata: kernel.bin ${size} bayt, boot.asm KERNEL_SECTORS=${KERNEL_SECTORS} icin fazla."
  echo "Cozum: KERNEL_SECTORS degerini artir."
  exit 1
fi

truncate -s "$KERNEL_BYTES" kernel.bin

if [ ! -f os-image.bin ]; then
  truncate -s "$DISK_BYTES" os-image.bin
else
  cur_size=$(stat -c%s os-image.bin)
  if [ "$cur_size" -lt "$DISK_BYTES" ]; then
    truncate -s "$DISK_BYTES" os-image.bin
  fi
fi

dd if=boot.bin of=os-image.bin conv=notrunc status=none
dd if=kernel.bin of=os-image.bin bs=512 seek=1 conv=notrunc status=none

echo "Hazir: os-image.bin"
echo "Calistir: qemu-system-i386 -drive format=raw,file=os-image.bin"
