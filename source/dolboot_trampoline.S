// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

    .text
    .balign 32
    .global dolboot_trampoline
    .global dolboot_trampoline_end
dolboot_trampoline:
    # r3: validated DOL in MEM2. r4: its command line in MEM2.
    # r5: optional command line destination in MEM1. r6: command line bytes.
    # This code and the file are in MEM2. Never touch the old MEM1 stack.
    mr 31, 3
    mr 30, 4
    mr 29, 5
    mr 28, 6
    li 27, 0
.Lsection:
    addi 8, 31, 144
    lwzx 5, 8, 27
    cmpwi 5, 0
    beq .Lnext
    addi 8, 31, 72
    lwzx 4, 8, 27
    lwzx 3, 31, 27
    add 3, 31, 3
    bl .Lcopy
    bl .Lsync_section
.Lnext:
    addi 27, 27, 4
    cmpwi 27, 72
    blt .Lsection

    lwz 5, 220(31)
    cmpwi 5, 0
    beq .Largv
    lwz 4, 216(31)
    addi 6, 4, -1
    li 7, 0
    mtctr 5
.Lzero:
    stbu 7, 1(6)
    bdnz .Lzero
    bl .Lsync_section

.Largv:
    cmpwi 29, 0
    beq .Lstart
    mr 3, 30
    mr 4, 29
    mr 5, 28
    bl .Lcopy
    bl .Lsync_section
    lwz 4, 72(31)
    addi 4, 4, 8
    lis 7, 0x5f61
    ori 7, 7, 0x7267
    stw 7, 0(4)
    stw 29, 4(4)
    stw 28, 8(4)
    li 5, 12
    bl .Lsync_section

.Lstart:
    sync
    isync
    lwz 3, 224(31)
    mtctr 3
    bctrl
.Lhalt:
    b .Lhalt

.Lcopy:
    addi 6, 3, -1
    addi 7, 4, -1
    mtctr 5
.Lbyte:
    lbzu 8, 1(6)
    stbu 8, 1(7)
    bdnz .Lbyte
    blr

.Lsync_section:
    clrrwi 6, 4, 5
    add 7, 4, 5
.Lflush:
    dcbf 0, 6
    addi 6, 6, 32
    cmplw 6, 7
    blt .Lflush
    sync
    clrrwi 6, 4, 5
.Linvalidate:
    icbi 0, 6
    addi 6, 6, 32
    cmplw 6, 7
    blt .Linvalidate
    blr
dolboot_trampoline_end:
    .if (dolboot_trampoline_end - dolboot_trampoline) > 768
    .error "DOL handoff code does not fit before the command line"
    .endif
