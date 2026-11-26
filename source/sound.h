// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>

typedef enum { CUE_MOVE, CUE_OPEN, CUE_BACK, CUE_PAGE, CUE_COMPLETE, CUE_REMOVE_PROMPT, CUE_REMOVE_DONE } Cue;

void SoundInit(void);
void SoundEnd(void);
void SoundPlay(Cue cue);
void SoundSettings(bool effects, bool music);
