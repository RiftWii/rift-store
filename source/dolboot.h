// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The store stages RiftWii's DOL and its own handoff code in MEM2 so the
// incoming program can replace the store in MEM1.
#pragma once

#include <gccore.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads the file whole into memory from `alloc` (32-byte aligned), which
// must not be where the program lands. NULL if it cannot; memory taken for
// a read that came up short goes back through `release`.
u8* dolboot_read(const char* path, u32* size, void* (*alloc)(u32 size), void (*release)(void* data));

// Whether the DOL's sections fit its file and land in MEM1.
bool dolboot_valid(const u8* dol, u32 size);

enum { DOLBOOT_WORKSPACE = 1024 };

// Both dol and the 32-byte-aligned workspace must be in MEM2. The caller
// has already validated the DOL and closed its files.
void dolboot_run(const u8* dol, void *workspace, const char* argv0) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
