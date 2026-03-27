TEXT_CL equ 2     
TEXT_AL equ 10     
DATA_CL equ 12    
DATA_AL equ 31    

use16
org 0x7C00

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti

    mov  ax, 0x0003         ; clear screen, 80x25 text mode
    int  0x10

    mov  ax, 0x07E0
    mov  es, ax
    xor  bx, bx
    mov  ah, 0x02
    mov  al, 1
    mov  dl, 0x00           ; drive A (bootsect.bin)
    mov  dh, 0x00
    mov  ch, 0x00
    mov  cl, 0x02           ; sector 2
    int  0x13

    mov  di, 0x8000
    mov  cx, 26
    xor  al, al
.zero:
    mov  [di], al
    inc  di
    loop .zero

    mov  si, msg_title
    call puts
    call draw_mask
    call crlf
    mov  si, msg_hint
    call puts

input_loop:
    xor  ax, ax
    int  0x16               ; wait keypress: AL=char AH=scancode
    cmp  al, 0x0D           ; Enter = boot
    je   do_load
    cmp  al, 'a'
    jb   input_loop
    cmp  al, 'z'
    ja   input_loop
    sub  al, 'a'
    movzx bx, al
    xor  byte [bx + 0x8000], 1   ; toggle
    mov  ah, 0x0E
    mov  al, 0x0D           ; CR (no LF) - stay on same line
    int  0x10
    call draw_mask
    jmp  input_loop

draw_mask:
    mov  si, 0x8000
    mov  cx, 26
    xor  bx, bx
.l:
    mov  al, [si]
    test al, al
    jnz  .letter
    mov  al, '_'
    jmp  .print
.letter:
    mov  al, bl
    add  al, 'a'
.print:
    mov  ah, 0x0E
    int  0x10
    inc  si
    inc  bx
    loop .l
    ret

do_load:
    call crlf
    mov  si, msg_loading
    call puts

    mov  ax, 0x1100
    mov  es, ax
    xor  bx, bx
    mov  ah, 0x02
    mov  al, TEXT_AL
    mov  dl, 0x01           
    mov  dh, 0x00
    mov  ch, 0x00
    mov  cl, TEXT_CL
    int  0x13
    jc   disk_err

    mov  ax, 0x1300
    mov  es, ax
    xor  bx, bx
    mov  ah, 0x02
    mov  al, DATA_AL
    mov  dl, 0x01
    mov  dh, 0x00
    mov  ch, 0x00
    mov  cl, DATA_CL
    int  0x13
    jc   disk_err

.kbd_flush:
    in   al, 0x64
    test al, 0x01
    jz   .kbd_done
    in   al, 0x60
    jmp  .kbd_flush
.kbd_done:

    cli
    lgdt [gdt_desc]
    in   al, 0x92           ; enable A20
    or   al, 0x02
    out  0x92, al
    mov  eax, cr0
    or   al, 1
    mov  cr0, eax
    jmp  0x08:0x7E00        ; far jump to PM stub (per lab manual)

disk_err:
    mov  si, msg_err
    call puts
    jmp  $

puts:
    lodsb
    test al, al
    jz   .d
    mov  ah, 0x0E
    int  0x10
    jmp  puts
.d: ret

crlf:
    mov  ah, 0x0E
    mov  al, 0x0D
    int  0x10
    mov  al, 0x0A
    int  0x10
    ret

msg_title:   db "DictOS EN->FI | a-z toggles letter | Enter=boot", 13, 10, 0
msg_hint:    db "Enter to start:", 13, 10, 0
msg_loading: db "Loading...", 13, 10, 0
msg_err:     db "Disk error!", 13, 10, 0

align 4
gdt:
    db 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00  ; null
    db 0xFF,0xFF,0x00,0x00,0x00,0x9A,0xCF,0x00  ; code seg
    db 0xFF,0xFF,0x00,0x00,0x00,0x92,0xCF,0x00  ; data seg
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt

    times (510 - ($ - start)) db 0
    db 0x55, 0xAA

org  0x7E00
use32

protected_mode:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  fs, ax
    mov  gs, ax
    mov  esp, 0x7C00
    call 0x11000            ; .text entry = BASE(0x10000) + RVA(0x1000)
    hlt
    jmp  $