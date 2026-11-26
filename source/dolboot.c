// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "dolboot.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DOL_MAX (16u << 20)
#define MEM1_LO 0x80003f00u
#define MEM1_HI 0x81700000u  // above: what IOS and the loaders keep

typedef struct {
    u32 offset[18];
    u32 address[18];
    u32 size[18];
    u32 bss_address;
    u32 bss_size;
    u32 entry;
} DolHeader;

static bool in_mem1(u32 address, u32 bytes);

static u16 be16(const u8 *p) {
    return ((u16)p[0] << 8) | p[1];
}

static u32 be32(const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

// Keep the ELF bytes behind a DOL header so both formats can use the same
// relocated loader. The original ELF stays available until the handoff.
static bool ConvertElf(u8 *image, u32 size) {
    const u8 *elf = image + 0x100;
    if (size < 52 || elf[4] != 1 || elf[5] != 2 || elf[6] != 1 ||
        be16(elf + 16) != 2 || be16(elf + 18) != 20 || be32(elf + 20) != 1 ||
        be16(elf + 42) != 32) return false;
    const u32 table = be32(elf + 28);
    const u32 count = be16(elf + 44);
    if (table > size || !count || count > 64 || count > (size - table) / 32) return false;
    memset(image, 0, 0x100);
    DolHeader *dol = (DolHeader *)image;
    dol->entry = be32(elf + 24);
    unsigned text = 0;
    unsigned data = 7;
    u32 zero_begin = MEM1_HI;
    u32 zero_end = 0;
    for (u32 i = 0; i < count; ++i) {
        const u8 *segment = elf + table + i * 32;
        if (be32(segment) != 1) continue;
        const u32 offset = be32(segment + 4);
        const u32 address = be32(segment + 8);
        const u32 bytes = be32(segment + 16);
        const u32 memory = be32(segment + 20);
        if (!memory) continue;
        if (offset > size || bytes > size - offset || memory < bytes || !in_mem1(address, memory))
            return false;
        if (bytes) {
            const bool executable = (be32(segment + 24) & 1) != 0;
            if ((executable && text == 7) || (!executable && data == 18)) return false;
            const unsigned slot = executable ? text++ : data++;
            dol->offset[slot] = 0x100 + offset;
            dol->address[slot] = address;
            dol->size[slot] = bytes;
        }
        if (memory > bytes) {
            if (address + bytes < zero_begin) zero_begin = address + bytes;
            if (address + memory > zero_end) zero_end = address + memory;
        }
    }
    if (zero_end > zero_begin) {
        for (unsigned i = 0; i < 18; ++i) {
            if (dol->size[i] && dol->address[i] < zero_end && dol->address[i] + dol->size[i] > zero_begin)
                return false;
        }
        dol->bss_address = zero_begin;
        dol->bss_size = zero_end - zero_begin;
    }
    return dolboot_valid(image, size + 0x100);
}

u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size), void (*release)(void* data)) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    u8 magic[4];
    const bool elf = fread(magic, 1, sizeof(magic), f) == sizeof(magic) &&
                     memcmp(magic, "\x7f" "ELF", sizeof(magic)) == 0;
    const long n = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    u8* data = NULL;
    const u32 prefix = elf ? 0x100 : 0;
    if (n > 0x100 && (u32)n <= DOL_MAX && fseek(f, 0, SEEK_SET) == 0)
        data = (u8*)alloc((u32)n + prefix);
    if (data && (fread(data + prefix, 1, (size_t)n, f) != (size_t)n ||
                 (elf && !ConvertElf(data, (u32)n)))) {
        release(data);
        data = NULL;
    }
    fclose(f);
    *size = data ? (u32)n + prefix : 0;
    return data;
}

// [address, address + bytes) inside MEM1's free part, the sum not wrapping
// (a damaged header passed as 2 GB copied over MEM1: a black screen).
static bool in_mem1(u32 address, u32 bytes) {
    return address >= MEM1_LO && address <= MEM1_HI && bytes <= MEM1_HI - address;
}

bool dolboot_valid(const u8* dol, u32 size) {
    if (size < 0x100) return false;
    const DolHeader* h = (const DolHeader*)dol;
    bool entry_loaded = false;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] == 0) continue;
        if (h->offset[i] < 0x100 || h->offset[i] > size || h->size[i] > size - h->offset[i] ||
            !in_mem1(h->address[i], h->size[i])) return false;
        const u32 lo = h->address[i], hi = lo + h->size[i];
        if (i < 7 && h->entry >= lo && h->entry < hi) entry_loaded = true;
    }
    return entry_loaded && (h->entry & 3) == 0 && (!h->bss_size || in_mem1(h->bss_address, h->bss_size));
}

static u32 argv_target(const u8 *dol, const DolHeader *h, u32 length) {
    if (h->size[0] < 8 + sizeof(struct __argv) || (h->address[0] & 3)) return 0;
    u32 magic;
    memcpy(&magic, dol + h->offset[0] + 4, sizeof(magic));
    if (magic != ARGV_MAGIC) return 0;
    u32 end = 0;
    for (int i = 0; i < 18; ++i) {
        if (h->size[i] && h->address[i] + h->size[i] > end) end = h->address[i] + h->size[i];
    }
    // An empty BSS has no validated address. Counting it can wrap the
    // aligned command-line address to zero for a damaged DOL header.
    if (h->bss_size && h->bss_address + h->bss_size > end) end = h->bss_address + h->bss_size;
    const u32 target = (end + 31) & ~31u;
    return in_mem1(target, length) ? target : 0;
}

static void install_system_call_vector(void) {
    static const u32 handler[] = {
        0x7D30FAA6,  // mfspr r9, HID0
        0x612A0008,  // ori   r10, r9, 8
        0x7D50FBA6,  // mtspr HID0, r10
        0x4C00012C,  // isync
        0x7C0004AC,  // sync
        0x7D30FBA6,  // mtspr HID0, r9
        0x4C000064,  // rfi
    };
    u32* const vector = (u32*)0x80000C00;
    for (unsigned i = 0; i < sizeof(handler) / sizeof(handler[0]); ++i) vector[i] = handler[i];
    DCFlushRange(vector, sizeof(handler));
    ICInvalidateRange(vector, sizeof(handler));
}

void dolboot_run(const u8* dol, void *workspace, const char* argv0) {
    extern const u8 dolboot_trampoline[], dolboot_trampoline_end[];
    enum { ARGV_SPACE = 256 };
    char *const command = (char *)workspace + DOLBOOT_WORKSPACE - ARGV_SPACE;
    const u32 path_length = strnlen(argv0, ARGV_SPACE - 2);
    const u32 length = path_length + 2;
    const u32 target = argv_target(dol, (const DolHeader *)dol, length);
    memcpy(command, argv0, path_length);
    command[path_length] = 0;
    command[path_length + 1] = 0;
    const u32 code_size = (u32)((uintptr_t)dolboot_trampoline_end - (uintptr_t)dolboot_trampoline);
    memcpy(workspace, dolboot_trampoline, code_size);
    DCFlushRange(workspace, code_size);
    ICInvalidateRange(workspace, code_size);
    SYS_ResetSystem(SYS_SHUTDOWN, 0, 0);
    IRQ_Disable();
    install_system_call_vector();
    ((void (*)(const u8 *, const char *, u32, u32))workspace)(dol, command, target, length);
    __builtin_unreachable();
}
