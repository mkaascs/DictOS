# DictOS

A minimal x86 operating system with an English-to-Finnish dictionary, written as a university lab assignment.

Boots from a floppy image, switches the CPU into 32-bit protected mode, and drops into an interactive command line.

```
Welcome to DictOS!
EN->FI Dictionary OS | FASM + MSVC
Commands: info, dictinfo, translate <word>, wordstat <letter>, shutdown

# translate cat
kissa
# dictinfo
Dictionary: en -> fi
Number of words: 101
Number of loaded words: 11
# anyword a
apple: omena
# shutdown
Powering off...
```

## Stack

- **Bootloader** — FASM (x86 real mode, 512 bytes)
- **Kernel** — C++ compiled with MSVC (x86 protected mode, no stdlib)
- **Emulator** — QEMU

## How it works

The bootloader runs in 16-bit real mode. It lets the user toggle which letters of the alphabet are "active" (affects which words are available for translation), then loads the PE kernel from a second floppy image into memory and switches the CPU into 32-bit protected mode.

The kernel takes over from there — sets up the IDT, enables keyboard interrupts, and runs the command loop. All hardware interaction (video, keyboard, PIC) is done via direct port I/O and memory-mapped video buffer at `0xB8000`. No BIOS, no stdlib.

## Run

```bash
qemu-system-i386 -fda bootsect.bin -fdb kernel.bin
```

## Commands

| Command | Description |
|---|---|
| `info` | Author, compilers, active letter filter |
| `dictinfo` | Dictionary language, total and available word count |
| `translate <word>` | Translate English word to Finnish |
| `wordstat <letter>` | Count available words for a given letter |
| `anyword [letter]` | Random word and its translation |
| `shutdown` | Power off (ACPI via port 0x604) |