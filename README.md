# PurrfectOS

Sürüm: v0.4.0-dev

Baslangic asamasi: 16-bit on yukleyici + 32-bit C cekirdek (MeowLine).

## MSYS2 derleme

```bash
./build_msys2.sh
```

## QEMU ile calistirma

```bash
qemu-system-i386 -drive format=raw,file=os-image.bin
```

## Not

- Link adiminda `--image-base=0` ve `-Ttext=0x1000` kullaniliyor.
- `objcopy` adiminda `.text + .rdata + .data` aliniyor.
- `boot.asm` icindeki `KERNEL_SECTORS` degeri ile `build_msys2.sh` icindeki `KERNEL_SECTORS` ayni olmali.
- `os-image.bin` 2 MB tutulur; CatLoaf FS sektorleri kalici oldugu icin reboot sonrasi veri korunur.
- Guvenli yukleme icin `KERNEL_SECTORS` degerini 48 tuttuk (boot kodu ustune yazmamak icin).

## CatLoaf FS V2 Komutlari

- `Gozleme`: dosyalari ve dizinleri listeler
- `pwd`: mevcut dizin yolunu gosterir
- `cd <dizin>`: dizin degistirir (`..`, mutlak `/a/b`, goreli `a/b`)
- `Yirt <dosya>`: dosya siler
- `Pencele <dizin>`: dizin ve icindeki dosyalari siler
- `Ton baligi <dosya>`: dosya olusturur
- `kedi mamasi <dizin>`: dizin olusturur
- `avla <dosya> <icerik>`: dosya icerigini gunceller
- `cat <dosya>`: dosya icerigini okur
- `uyu`: sistemi kapatmayi dener
- `esne`: sistemi yeniden baslatir
- `veteriner`: recovery kontrolu yapar, bozuksa FS'i onarir

Not: Bosluk/karakter sorunlarini onlemek icin komutlari ASCII yaz (`baligi`, `mamasi`).

## Dosyalar

- `boot.asm`: On yukleme sektoru (MBR), diskten cekirdek okuma, protected mode gecisi
- `kernel_entry.asm`: C cekirdege giris noktasi (`start`)
- `kernel.c`: MeowLine klavye girisi ve komut sistemi
- `build_msys2.sh`: MSYS2 icin derleme + paketleme scripti
