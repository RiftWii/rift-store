// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <ogc/lwp_queue.h>
#include <wiiuse/wpad.h>
#include <wiiuse/wiiuse.h>

// Use RiftWii's command-queue sizing outside libogc's 64 KiB kernel heap.
// The stock heap can run out before it allocates the fourth remote's queue.
enum { REMOTE_COMMANDS = 256 };
static struct cmd_blk_t commands[WPAD_MAX_DEVICES][REMOTE_COMMANDS] ATTRIBUTE_ALIGN(32);

int __real_wiiuse_init_cmd_queue(struct wiimote_t *remote);

int __wrap_wiiuse_init_cmd_queue(struct wiimote_t *remote) {
    const int slot = remote->unid;
    if (slot < 0 || slot >= WPAD_MAX_DEVICES) return __real_wiiuse_init_cmd_queue(remote);
    __lwp_queue_initialize((lwp_queue *)&remote->cmdq, commands[slot], REMOTE_COMMANDS, sizeof(struct cmd_blk_t));
    return 0;
}
