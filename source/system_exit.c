// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_exit.h"
#include "dolboot.h"
#include "live.h"
#include "sound.h"

#include <fat.h>
#include <ogc/es.h>
#include <ogc/wiilaunch.h>
#include <sdcard/wiisd_io.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <wiiuse/wpad.h>

static const u64 riftwii_titles[] = {0x0001000155465457ull, 0x0001000152465457ull}; // UFTW, RFTW
static const u64 hbc_titles[] = {
    0x000100014c554c5aull, // LULZ
    0x000100014f484243ull, // OHBC
    0x000100014a4f4449ull, // JODI
    0x0001000148415858ull, // HAXX
    0x00010001af1bf516ull
};
static const char riftwii_path[] = "sd:/apps/riftwii/boot.dol";
static atomic_bool power_requested;

static void RequestPower(void) {
    atomic_store_explicit(&power_requested, true, memory_order_relaxed);
}

static void RemotePower(s32 channel) {
    if (channel == WPAD_CHAN_0) RequestPower();
}

void SystemExitInit(void) {
    SYS_SetPowerCallback(RequestPower);
    WPAD_SetPowerButtonCallback(RemotePower);
}

bool SystemPowerRequested(void) {
    return atomic_load_explicit(&power_requested, memory_order_relaxed);
}

bool SystemIsVWii(void) {
    u32 contents = 0;
    return ES_GetTitleContentsCount(0x0000000100000200ull, &contents) >= 0 && contents > 0;
}

static bool ChannelPresent(u64 title) {
    u32 tickets = 0;
    u32 tmd_size = 0;
    return ES_GetNumTicketViews(title, &tickets) >= 0 && tickets != 0 &&
           ES_GetTMDViewSize(title, &tmd_size) >= 0 && tmd_size >= sizeof(tmd_view);
}

static bool TryChannels(const u64 *titles, size_t count) {
    bool installed = false;
    bool audio_stopped = false;
    for (size_t index = 0; index < count; ++index) {
        const u64 title = titles[index];
        if (!ChannelPresent(title)) continue;
        installed = true;
        if (WII_Initialize() < 0) continue;
        if (!audio_stopped) {
            SoundEnd();
            audio_stopped = true;
        }
        WII_LaunchTitle(title);
    }
    // A failed launch leaves the player in the menu with its audio restored.
    if (audio_stopped) SoundInit();
    return installed;
}

static void *ReserveMem2(u32 size) {
    const uintptr_t low = ((uintptr_t)SYS_GetArena2Lo() + 31) & ~(uintptr_t)31;
    const uintptr_t high = (uintptr_t)SYS_GetArena2Hi();
    const u32 rounded = (size + 31) & ~31u;
    if (low > high || rounded > high - low) return NULL;
    SYS_SetArena2Lo((void *)(low + rounded));
    return (void *)low;
}

static void ReleaseMem2(void *data) {
    SYS_SetArena2Lo(data);
}

static const char *LaunchFiles(const char *primary, const char *alternate) {
    const bool mounted_here = !LiveHasSD();
    if (mounted_here && !fatMountSimple("sd", &__io_wiisd)) return "The SD card is not available.";
    void *const arena_before = SYS_GetArena2Lo();
    u32 size = 0;
    const char *path = primary;
    u8 *dol = dolboot_read(path, &size, ReserveMem2, ReleaseMem2);
    if ((!dol || !dolboot_valid(dol, size)) && alternate) {
        SYS_SetArena2Lo(arena_before);
        path = alternate;
        dol = dolboot_read(path, &size, ReserveMem2, ReleaseMem2);
    }
    if (!dol || !dolboot_valid(dol, size)) {
        SYS_SetArena2Lo(arena_before);
        if (mounted_here) {
            fatUnmount("sd:");
            __io_wiisd.shutdown();
        }
        return "This app could not be opened. Check its boot file or use the Homebrew Channel.";
    }
    void *const workspace = ReserveMem2(DOLBOOT_WORKSPACE);
    if (!workspace) {
        SYS_SetArena2Lo(arena_before);
        if (mounted_here) {
            fatUnmount("sd:");
            __io_wiisd.shutdown();
        }
        return "There is not enough memory to open this app.";
    }
    SoundEnd();
    fatUnmount("sd:");
    __io_wiisd.shutdown();
    WPAD_Shutdown();
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    dolboot_run(dol, workspace, path);
}

static const char *StartRiftWii(void) {
    TryChannels(riftwii_titles, sizeof(riftwii_titles) / sizeof(*riftwii_titles));
    return LaunchFiles(riftwii_path, NULL);
}

const char *SystemOpenApp(const char *id) {
    char dol[72];
    char elf[72];
    snprintf(dol, sizeof(dol), "sd:/apps/%s/boot.dol", id);
    snprintf(elf, sizeof(elf), "sd:/apps/%s/boot.elf", id);
    return LaunchFiles(dol, elf);
}

static void Shutdown(void) {
    SoundEnd();
    if (LiveHasSD()) fatUnmount("sd:");
    WPAD_Shutdown();
    VIDEO_SetBlack(true);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    // Standby here is a full power-off, with the WiiConnect24 idle mode off.
    SYS_ResetSystem(SYS_POWEROFF_STANDBY, 0, 0);
    for (;;) {}
}

const char *SystemExitRun(ExitChoice choice) {
    switch (choice) {
        case EXIT_RIFTWII: return StartRiftWii();
        case EXIT_HBC:
            if (TryChannels(hbc_titles, sizeof(hbc_titles) / sizeof(*hbc_titles))) {
                return "The Homebrew Channel could not start.";
            }
            return "The Homebrew Channel was not found on this Wii.";
        case EXIT_POWER: Shutdown(); break;
        case EXIT_CANCEL: break;
    }
    return NULL;
}
