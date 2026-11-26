// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

enum { APP_INSTALLABLE = 1, APP_LIMIT = 1000 };

typedef struct {
    const char *name;
    const char *author;
    const char *description;
    const char *version;
    const char *license;
    const char *category;
    const char *id;
    const char *page;
    const char *download;
    const char *newer;
    const char *released;
    u32 bytes;
    u16 icon;
    u8 platforms;
    u8 controllers;
    u8 flags;
} App;
