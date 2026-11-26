// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>

#include "store.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { CATALOGUE_LOADING, CATALOGUE_READY, CATALOGUE_FAILED } CatalogueState;
typedef enum { APP_GET, APP_INSTALLED, APP_UPDATE, APP_ON_CARD, APP_PC_ONLY } InstallState;
typedef enum { LIVE_DOWNLOAD, LIVE_VERIFY, LIVE_EXTRACT, LIVE_COMMIT, LIVE_DONE, LIVE_FAILED, LIVE_CANCELLED } JobStage;
typedef struct { JobStage stage; u32 done, total; } JobProgress;
typedef struct {
    bool ok, removed, saves_left;
    unsigned index;
    const char *message;
} JobResult;

void LiveInit(void);
void LiveRefresh(void);
void LivePump(u32 frame);
CatalogueState LiveCatalogueState(void);
bool LiveOffline(void);
const char *LiveCatalogueError(void);
unsigned LiveGeneration(void);
unsigned LiveCount(void);
bool LiveRead(unsigned index, App *app);
bool LiveIconLoading(unsigned index);
bool LiveMatches(unsigned index, const char *query);
unsigned LiveFind(const char *id);
InstallState LiveInstallState(unsigned index);
unsigned LiveUpdateCount(void);
void LiveWantIcons(const unsigned *indices, unsigned count, u32 frame);
const u8 *LiveIconPixels(unsigned handle);
bool LiveStartInstall(unsigned index);
bool LiveStartRemove(unsigned index, bool erase_data);
bool LiveTakeResult(JobResult *out);
JobProgress LiveProgress(void);
void LiveCancelJob(void);
void LiveStop(void);
void LiveResume(void);
bool LiveBusy(void);
void LiveCloseNetwork(void);
bool LiveHasSD(void);
bool LiveCheckStorage(void);
bool LiveFreeKiB(u32 *result);
bool LiveSavePreferences(u8 flags);
u8 LiveLoadPreferences(void);
bool LiveQR(const char *url, u8 *modules, unsigned *size);

#ifdef __cplusplus
}
#endif
