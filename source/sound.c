// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sound.h"

#include <asndlib.h>
#include <mp3player.h>
#include <ogc/cache.h>
#include <ogc/lwp_watchdog.h>
#include <stddef.h>
#include <stdio.h>

#include "sound_assets.h"

typedef struct { const s16 *data; s32 bytes; } Sample;

static const Sample samples[] = {
    [CUE_MOVE] = {pcm_move, sizeof(pcm_move)},
    [CUE_OPEN] = {pcm_open, sizeof(pcm_open)},
    [CUE_BACK] = {pcm_back, sizeof(pcm_back)},
    [CUE_PAGE] = {pcm_page, sizeof(pcm_page)},
    [CUE_COMPLETE] = {pcm_complete, sizeof(pcm_complete)},
    [CUE_REMOVE_PROMPT] = {pcm_remove_prompt, sizeof(pcm_remove_prompt)},
    [CUE_REMOVE_DONE] = {pcm_remove_done, sizeof(pcm_remove_done)}
};

static u64 last_move;
static FILE *music_file;
static bool music_started;
static bool effects_enabled = true;
static bool music_enabled = true;

static s32 ReadMusic(void *context, void *buffer, s32 requested) {
    FILE *file = context;
    u8 *out = buffer;
    s32 total = 0;
    while (total < requested) {
        const size_t count = fread(out + total, 1, (size_t)(requested - total), file);
        total += (s32)count;
        if (total == requested || ferror(file) || !feof(file)) break;
        if (fseek(file, 0, SEEK_SET)) break;
        const size_t restart = fread(out + total, 1, (size_t)(requested - total), file);
        if (!restart) break;
        total += (s32)restart;
    }
    return total;
}

void SoundInit(void) {
    // The DSP streams aligned blocks directly from main memory.
    for (unsigned index = 0; index < sizeof(samples) / sizeof(*samples); ++index) {
        DCFlushRange((void *)samples[index].data, samples[index].bytes);
    }
    ASND_Init();
    ASND_Pause(0);
    music_file = fopen("sd:/apps/rift-store/music.mp3", "rb");
    if (music_file) {
        MP3Player_Init();
        MP3Player_Volume(music_enabled ? 52 : 0);
        music_started = MP3Player_PlayFile(music_file, ReadMusic, NULL) == 0;
    }
}

void SoundEnd(void) {
    if (music_started) MP3Player_Stop();
    music_started = false;
    if (music_file) fclose(music_file);
    music_file = NULL;
    ASND_End();
}

void SoundSettings(bool effects, bool music) {
    effects_enabled = effects;
    music_enabled = music;
    if (music_started) MP3Player_Volume(music ? 52 : 0);
}

void SoundPlay(Cue cue) {
    if (!effects_enabled) return;
    if (cue == CUE_MOVE) {
        const u64 now = gettime();
        // A sweep across the grid should not stack a sound on every frame.
        if (last_move && ticks_to_millisecs(now - last_move) < 75) return;
        last_move = now;
    }
    // The libogc MP3 player owns voice zero throughout playback.
    s32 voice = 1;
    while (voice < MAX_SND_VOICES && ASND_StatusVoice(voice) != SND_UNUSED) ++voice;
    if (voice == MAX_SND_VOICES) return;
    const Sample *sample = &samples[cue];
    ASND_SetVoice(voice, VOICE_MONO_16BIT_BE, CUE_SAMPLE_RATE, 0, (void *)sample->data,
                  sample->bytes, 175, 175, NULL);
}
