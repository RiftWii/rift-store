// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>

typedef enum { EXIT_RIFTWII, EXIT_HBC, EXIT_POWER, EXIT_CANCEL } ExitChoice;

void SystemExitInit(void);
bool SystemPowerRequested(void);
bool SystemIsVWii(void);
const char *SystemOpenApp(const char *id);
// A successful handoff does not return. A failed launch returns a message.
const char *SystemExitRun(ExitChoice choice);
