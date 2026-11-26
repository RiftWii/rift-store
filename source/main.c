// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "draw.h"
#include "live.h"
#include "sound.h"
#include "store.h"
#include "system_exit.h"

#include <math.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiiuse/wpad.h>

enum { HOME_COLUMNS = 4, APP_ROWS = 2, SAFE = 32 };
enum { SCREEN_FADE_MS = 150, PAGE_SLIDE_MS = 190, FOCUS_PULSE_MS = 170 };
enum { SHUTDOWN_FADE_MS = 420, HINT_TOP = 430, HINT_HEIGHT = 34, HINT_GAP = 8,
       HINT_KEY_SIZE = 16, HINT_LABEL_SIZE = 17 };
enum { INPUT_UP = 1, INPUT_DOWN = 2, INPUT_LEFT = 4, INPUT_RIGHT = 8,
       INPUT_A = 16, INPUT_B = 32, INPUT_PLUS = 64, INPUT_MINUS = 128, INPUT_HOME = 256,
       INPUT_ONE = 512, INPUT_TWO = 1024 };
enum { PREF_EFFECTS = 1, PREF_MUSIC = 2, PREF_LOW_MOTION = 4, PREF_GAMECUBE = 8 };
typedef enum { HOME, LIST, DETAILS, SEARCH, SETTINGS, SOURCE, DOWNLOADS } Screen;
typedef enum { BY_CATEGORY, MY_APPS, SEARCH_RESULTS } ListMode;
typedef enum { NONE, REPLACE, WII_ONLY, REMOVE, ERASE_DATA, UPDATE_ALL } Confirmation;
typedef enum { IDLE, INSTALLING, REMOVING } Operation;
typedef enum { BUTTON_FOCUS, WII_POINTER, GC_POINTER } CursorMode;

typedef struct { const char *key, *name; } Category;
static const Category categories[] = {
    {"games", "Games"},
    {"emulators", "Emulators"},
    {"loaders", "Loaders"},
    {"system", "System tools"},
    {"gametools", "Game tools"},
    {"media", "Media"},
    {"wiimenu", "Wii Menu"},
    {"controllers", "Controllers"},
    {"creative", "Creative"},
    {"toys", "Demos and toys"},
    {"internet", "Internet"},
    {"online", "Online services"},
    {"more", "More"}
};
enum { MORE_CATEGORY = sizeof(categories) / sizeof(*categories) - 1 };

static const u32 ink = 0x192f3dff;
static const u32 sub = 0x3b5260ff;
static const u32 focus = 0x159dcfff;
static const u32 white = 0xffffffff;

typedef struct {
    Screen screen;
    Screen downloads_back;
    ListMode list_mode;
    unsigned home_cursor, list_cursor, list_count, app;
    unsigned list[APP_LIMIT];
    u16 counts[MORE_CATEGORY + 1];
    u8 members[APP_LIMIT];
    unsigned visible_categories[MORE_CATEGORY + 1], visible_count;
    Operation job;
    Confirmation confirmation;
    bool confirm_choice;
    bool updates_only, vWii, queued, retry;
    unsigned generation, frame, keyboard, settings, matches, job_app, search_resume;
    unsigned queue_at, queue_count;
    u16 queue[APP_LIMIT];
    u8 preferences;
    char query[33], message[192];
    u8 qr[33 * 33];
    unsigned qr_size;
    CursorMode cursor;
    bool ir_present, gc_pointer_started;
    unsigned connected_remotes;
    float pointer_x, pointer_y;
    float ir_x, ir_y, gc_pointer_x, gc_pointer_y;
    u32 last_input;
    u32 now, screen_started, focus_started, page_started;
    int slide_direction;
    bool exit_open, shutting_down, exit_pending;
    u32 shutdown_started;
    ExitChoice exit_focus;
    const char *exit_status;
} Store;

static App AppAt(unsigned index) {
    App app = {0};
    LiveRead(index, &app);
    return app;
}

static void Message(Store *store, const char *text) {
    snprintf(store->message, sizeof(store->message), "%s", text ? text : "");
}

static void Center(const char *text, float x, float baseline, float size, float limit, u32 color) {
    const float width = TextWidth(text, size);
    Text(text, x - (width < limit ? width : limit) / 2, baseline, size, limit, color);
}

static void TileLabel(const char *text, float x, float baseline, float size, float limit) {
    if (TextWidth(text, size) > limit) {
        char first[96];
        const char *split = NULL;
        for (const char *space = strchr(text, ' '); space; space = strchr(space + 1, ' ')) {
            const size_t length = space - text;
            if (length >= sizeof(first)) break;
            memcpy(first, text, length);
            first[length] = '\0';
            if (TextWidth(first, size) > limit) break;
            split = space;
        }
        if (split) {
            first[split - text] = '\0';
            Center(first, x, baseline - 9, size, limit, ink);
            while (*split == ' ') ++split;
            Center(split, x, baseline + 9, size, limit, ink);
            return;
        }
    }
    Center(text, x, baseline, size, limit, ink);
}

static void Right(const char *text, float right, float baseline, float size, u32 color) {
    const float width = TextWidth(text, size);
    Text(text, right - width, baseline, size, width + 1, color);
}

static void Count(char *out, size_t size, unsigned count) {
    snprintf(out, size, "%u %s", count, count == 1 ? "app" : "apps");
}

static float AnimationRemaining(u32 now, u32 started, unsigned duration) {
    const u32 elapsed = now - started;
    if (elapsed >= duration) return 0;
    const float remaining = 1 - (float)elapsed / duration;
    return remaining * remaining;
}

static void DrawBackdrop(u32 now) {
    const float width = ScreenWidth();
    const float drift = sinf((now % 9000) * (6.2831853f / 9000)) * .18f;
    Gradient(0, 0, width, 480, 0xfcfefeff, 0xe4f0f5ff);
    Wave(81, 9, 28, 2.5f + drift, 0xf7ffffff, 0xcbeaf4ff);
    Wave(78, 13, 20, 2.2f + drift, 0xe5f8fcff, 0xa9e0f1ff);
    Wave(75, 14, 11, 2.1f + drift, 0xfeffffff, 0xc4ebf7ff);
    Wave(431, 10, 76, 0.5f, 0x90cbdeff, 0xd3edf5ff);
    Wave(434, 10, 76, 0.5f, 0xfcffffff, 0xb3d6e2ff);
}

static void DrawHeader(void) {
    const float width = ScreenWidth();
    Brand(32, 18);
    Text("Wii homebrew", 96, 85, 17, 165, sub);
    Panel(width - 193, 33, 161, 32, 16, 0x9bbfceff, 0x81a8baff);
    Panel(width - 191, 35, 157, 28, 14, white, 0xe4f4faff);
    const char *status = LiveCatalogueState() == CATALOGUE_LOADING ? "Connecting…" :
                         LiveCatalogueState() == CATALOGUE_FAILED ? "Unavailable" :
                         LiveOffline() ? "Offline catalogue" : "Online catalogue";
    Center(status, width - 112.5f, 55, 17, 149, sub);
}

static float HintWidth(const char *button, const char *label) {
    return TextWidth(button, HINT_KEY_SIZE) + TextWidth(label, HINT_LABEL_SIZE) + 40;
}

static float Hint(float x, const char *button, const char *label) {
    const float key_width = TextWidth(button, HINT_KEY_SIZE) + 18;
    const float width = HintWidth(button, label);
    const float key_x = x + 6;
    Panel(x, HINT_TOP + 2, width, HINT_HEIGHT, 17, 0x7899a842, 0x7899a842);
    Panel(x, HINT_TOP, width, HINT_HEIGHT, 17, 0x90b4c3ff, 0x739babff);
    Panel(x + 2, HINT_TOP + 2, width - 4, HINT_HEIGHT - 4, 15, white, 0xe3f2f7ff);
    Panel(key_x, HINT_TOP + 5, key_width, 24, 12, 0xa7dfefff, 0x81c8dfff);
    Center(button, key_x + key_width / 2, HINT_TOP + 23, HINT_KEY_SIZE, key_width - 5, ink);
    Text(label, key_x + key_width + 6, HINT_TOP + 23, HINT_LABEL_SIZE, width - key_width - 15, ink);
    return x + width + HINT_GAP;
}

static void Card(float x, float y, float width, float height, bool selected, float pulse) {
    if (selected) {
        const float glow = 2 + 2 * pulse;
        Panel(x - glow, y - glow, width + 2 * glow, height + 2 * glow, 14 + glow, 0xa3ecfcff, 0xc9f2fcff);
    }
    Panel(x, y + 3, width, height, 14, 0x7b98a847, 0x7b98a847);
    Panel(x, y, width, height, 14, selected ? 0x41c2e9ff : 0x9bb7c4ff, selected ? focus : 0x89a8b7ff);
    const float rim = selected ? 3 : 2;
    Panel(x + rim, y + rim, width - rim * 2, height - rim * 2, 14 - rim, white, selected ? 0xdaf4fcff : 0xe8f3f7ff);
}

static float HomeTileWidth(void) { return (ScreenWidth() - SAFE * 2 - 30) / (float)HOME_COLUMNS; }
static float HomeTileHeight(const Store *store) { return store->visible_count > 12 ? 58 : 82; }

static void DrawHome(const Store *store) {
    char count[32];
    Count(count, sizeof(count), LiveCount());
    Text("Categories", SAFE, 139, 26, 270, ink);
    Right(count, ScreenWidth() - SAFE, 138, 18, sub);
    const CatalogueState state = LiveCatalogueState();
    if (state != CATALOGUE_READY || !store->visible_count) {
        const float center = ScreenWidth() / 2.0f;
        Card(SAFE, 155, ScreenWidth() - SAFE * 2, 245, false, 0);
        if (state == CATALOGUE_LOADING) {
            LoadingRing(center, 240, 21, store->now);
            Center("Getting the catalogue…", center, 302, 22, ScreenWidth() - 100, ink);
        } else if (state == CATALOGUE_FAILED) {
            Center("The catalogue could not be loaded.", center, 213, 22, ScreenWidth() - 100, ink);
            Paragraph(LiveCatalogueError(), SAFE + 22, 248, 18, ScreenWidth() - SAFE * 2 - 44, 3, sub);
            Hint(SAFE + 6, "A", "Retry");
        } else {
            Center("No apps match your controller filter.", center, 256, 21, ScreenWidth() - 100, ink);
        }
        Hint(ScreenWidth() - SAFE - HintWidth("2", "Settings"), "2", "Settings");
        return;
    }
    const float width = HomeTileWidth();
    const float height = HomeTileHeight(store);
    const float pulse = store->preferences & PREF_LOW_MOTION ? 0 :
                        AnimationRemaining(store->now, store->focus_started, FOCUS_PULSE_MS);
    for (unsigned index = 0; index < store->visible_count; ++index) {
        const unsigned category = store->visible_categories[index];
        const float x = SAFE + index % HOME_COLUMNS * (width + 10);
        const float y = 153 + index / HOME_COLUMNS * (height + 8);
        Card(x, y, width, height, index == store->home_cursor, pulse);
        snprintf(count, sizeof(count), "%u", store->counts[category]);
        if (height < 60) {
            CategoryIcon(category, x + 9, y + 10, 38);
            TileLabel(categories[category].name, x + (width + 42) / 2, y + 26, 16, width - 55);
            Right(count, x + width - 11, y + 51, 16, sub);
        } else {
            const float badge = 38;
            CategoryIcon(category, x + (width - badge) / 2, y + 5, badge);
            TileLabel(categories[category].name, x + width / 2, y + 66, 18, width - 16);
            Right(count, x + width - 11, y + 24, 16, sub);
        }
    }
    float x = Hint(SAFE + 6, "A", "Open category");
    const unsigned updates = LiveUpdateCount();
    char library[28] = "My Apps";
    if (updates) snprintf(library, sizeof(library), "My Apps (%u)", updates);
    x = Hint(x, "+", library);
    x = Hint(x, "1", "Search");
    Hint(x, "2", "Settings");
}

static const char *AppStateLabel(unsigned app) {
    static const char *const labels[] = {"", "Installed", "Update available", "Installed outside Rift Store", "Get on a PC"};
    return labels[LiveInstallState(app)];
}

static const char *PrimaryAction(unsigned app) {
    static const char *const labels[] = {"Install to SD", "Open app", "Update on SD", "Replace app", ""};
    return labels[LiveInstallState(app)];
}

static bool CanRemove(unsigned app) {
    const InstallState state = LiveInstallState(app);
    return state == APP_INSTALLED || state == APP_UPDATE || state == APP_ON_CARD;
}

static const char *MaskLabel(char *text, size_t size, u8 mask, const char *const *names, unsigned count) {
    size_t used = 0;
    text[0] = '\0';
    for (unsigned index = 0; index < count; ++index) {
        if (!(mask & (1u << index))) continue;
        const int length = snprintf(text + used, size - used, "%s%s", used ? ", " : "", names[index]);
        if (length < 0 || (size_t)length >= size - used) break;
        used += length;
    }
    return text[0] ? text : "Unspecified";
}

typedef struct {
    unsigned columns, per_page;
    float left, width;
} AppGrid;

enum {
    GRID_TOP = 152, GRID_HEIGHT = 106, GRID_GAP = 12,
    PAGE_ARROW_WIDTH = 39, PAGE_ARROW_HEIGHT = 68,
    PAGE_ARROW_TOP = GRID_TOP + (APP_ROWS * GRID_HEIGHT + GRID_GAP - PAGE_ARROW_HEIGHT) / 2
};

static AppGrid GridLayout(unsigned count) {
    const unsigned columns = ScreenWidth() > 640 ? 4 : 3;
    const unsigned per_page = columns * APP_ROWS;
    const float left = count > per_page ? SAFE + PAGE_ARROW_WIDTH + 9 : SAFE;
    const float width = (ScreenWidth() - left * 2 - (columns - 1) * GRID_GAP) / columns;
    return (AppGrid){columns, per_page, left, width};
}

static void GridTile(const Store *store, AppGrid grid, unsigned slot, float *x, float *y) {
    const float slide = store->preferences & PREF_LOW_MOTION ? 0 :
                        9 * store->slide_direction * AnimationRemaining(store->now, store->page_started, PAGE_SLIDE_MS);
    *x = grid.left + (slot % grid.columns) * (grid.width + GRID_GAP) + slide;
    *y = GRID_TOP + (slot / grid.columns) * (GRID_HEIGHT + GRID_GAP);
}

static void Banner(const Store *store, unsigned index, float x, float y, float width) {
    enum { BORDER = 2, RADIUS = 8 };
    const float height = width * 3 / 8;
    const float inner_width = width - 2 * BORDER;
    const float inner_height = height - 2 * BORDER;
    Panel(x, y, width, height, RADIUS, 0xa2becaff, 0xa2becaff);
    Panel(x + BORDER, y + BORDER, inner_width, inner_height, RADIUS - BORDER, 0xe4edf0ff, 0xf6fcfdff);
    const App app = AppAt(index);
    if (app.icon) {
        AppIcon(app.icon, x + BORDER, y + BORDER, inner_width, inner_height, RADIUS - BORDER);
    } else {
        const float size = height * .63f;
        CategoryIcon(store->members[index], x + (width - size) / 2, y + (height - size) / 2, size);
    }
}

static float PageArrowX(bool forward) {
    return forward ? ScreenWidth() - SAFE - PAGE_ARROW_WIDTH : SAFE;
}

static bool OverPageArrow(const Store *store, bool forward) {
    const float left = PageArrowX(forward);
    return store->pointer_x >= left && store->pointer_x < left + PAGE_ARROW_WIDTH &&
           store->pointer_y >= PAGE_ARROW_TOP && store->pointer_y < PAGE_ARROW_TOP + PAGE_ARROW_HEIGHT;
}

static void PageArrow(const Store *store, bool forward, bool available) {
    const float x = PageArrowX(forward);
    const float y = PAGE_ARROW_TOP;
    const bool hovered = available && store->cursor != BUTTON_FOCUS && OverPageArrow(store, forward);
    if (hovered) Panel(x - 2, y - 2, PAGE_ARROW_WIDTH + 4, PAGE_ARROW_HEIGHT + 4, 15, 0x8ee1f8ff, 0xc9f2fcff);
    Panel(x, y + 2, PAGE_ARROW_WIDTH, PAGE_ARROW_HEIGHT, 13, 0x7b98a847, 0x7b98a847);
    Panel(x, y, PAGE_ARROW_WIDTH, PAGE_ARROW_HEIGHT, 13, available ? 0x6bcce9ff : 0xc2d7dfff,
          available ? 0x1698c7ff : 0xa8c3ceff);
    Panel(x + 1, y + 1, PAGE_ARROW_WIDTH - 2, PAGE_ARROW_HEIGHT - 2, 12, white,
          hovered ? 0xb6ebfbff : available ? 0xd3f2fbff : 0xe6f1f5ff);
    const float center_x = x + PAGE_ARROW_WIDTH / 2.0f;
    const float center_y = y + PAGE_ARROW_HEIGHT / 2.0f;
    const float direction = forward ? 1 : -1;
    const float point_x = center_x + direction * 11;
    const float back_x = center_x - direction * 8;
    const u32 arrow = available ? 0x079dceff : 0x9cbfcdff;
    const u32 edge = available ? 0x066a93ff : 0x81a7b8ff;
    Triangle(point_x + direction, center_y, back_x - direction, center_y - 15, back_x - direction, center_y + 15, edge);
    Triangle(point_x - direction, center_y, back_x, center_y - 13, back_x, center_y + 13, arrow);
}

static void DrawList(const Store *store) {
    const Category *category = &categories[store->visible_categories[store->home_cursor]];
    const char *title = store->list_mode == SEARCH_RESULTS ? "Search results" : store->list_mode == MY_APPS ?
                        (store->updates_only ? "Updates" : "My Apps") : category->name;
    Text(title, SAFE, 139, 26, 280, ink);
    char count[40];
    Count(count, sizeof(count), store->list_count);
    Right(count, ScreenWidth() - SAFE, 138, 18, sub);
    const AppGrid grid = GridLayout(store->list_count);
    const unsigned first = store->list_cursor / grid.per_page * grid.per_page;
    const float pulse = store->preferences & PREF_LOW_MOTION ? 0 :
                        AnimationRemaining(store->now, store->focus_started, FOCUS_PULSE_MS);
    if (!store->list_count) {
        Card(SAFE, 155, ScreenWidth() - SAFE * 2, 130, false, 0);
        const char *empty = store->list_mode == SEARCH_RESULTS ? "No apps match your search." :
                            store->updates_only ? "No updates available." : "No apps in My Apps.";
        Text(empty, SAFE + 18, 206, 22, 480, ink);
        Text(store->list_mode == SEARCH_RESULTS ? store->query : "Press B to go back.", SAFE + 18, 234, 18, 500, sub);
    }
    for (unsigned slot = 0; slot < grid.per_page && first + slot < store->list_count; ++slot) {
        const unsigned app = store->list[first + slot];
        float x, y;
        GridTile(store, grid, slot, &x, &y);
        Card(x, y, grid.width, GRID_HEIGHT, first + slot == store->list_cursor, pulse);
        const float banner_width = fminf(grid.width - 18, 149);
        Banner(store, app, x + (grid.width - banner_width) / 2, y + 7, banner_width);
        TileLabel(AppAt(app).name, x + grid.width / 2, y + 88, 18, grid.width - 10);
    }
    if (store->list_count) {
        const unsigned index = store->list[store->list_cursor];
        const App app = AppAt(index);
        const bool has_pages = store->list_count > grid.per_page;
        const float description_width = ScreenWidth() - SAFE * 2 - (has_pages ? 127 : 0);
        const char *state = AppStateLabel(index);
        if (*state) snprintf(count, sizeof(count), "%s", state);
        else snprintf(count, sizeof(count), "%s", app.author);
        Text(count, SAFE + 3, 397, 18, description_width, ink);
        Text(app.description, SAFE + 3, 419, 18, description_width, sub);
        if (has_pages) {
            snprintf(count, sizeof(count), "Page %u / %u", first / grid.per_page + 1,
                     (store->list_count + grid.per_page - 1) / grid.per_page);
            Right(count, ScreenWidth() - SAFE, 419, 18, sub);
            PageArrow(store, false, first > 0);
            PageArrow(store, true, first + grid.per_page < store->list_count);
        }
    }
    float x = Hint(SAFE + 6, "B", store->list_mode == SEARCH_RESULTS ? "Edit search" : "Categories");
    if (store->list_mode == MY_APPS) {
        x = Hint(x, "2", store->updates_only ? "All installed" : "Updates only");
        if (LiveUpdateCount()) Hint(x, "1", "Update all");
    } else {
        if (store->list_count) x = Hint(x, "A", "App details");
        if (store->list_count > grid.per_page) Hint(x, "- / +", "Change page");
    }
}

static void Fact(const char *name, const char *value, float x, float y, float width) {
    Text(name, x, y, 16, width, sub);
    Text(value, x, y + 22, 18, width, ink);
}

static void ActionButton(const char *text, float x, float width, bool active) {
    Panel(x, 361, width, 38, 19, active ? 0x68cbe8ff : 0xaec8d3ff, active ? 0x058ab4ff : 0x8aabb7ff);
    Panel(x + 2, 363, width - 4, 34, 17, white, active ? 0xbceeffff : 0xeaf2f6ff);
    Center(text, x + width / 2, 387, 19, width - 12, ink);
}

static void DrawInstallProgress(const Store *store, float x, float width) {
    static const char *const stages[] = {"Downloading package", "Verifying package", "Unpacking files",
                                        "Finishing on SD", "Completed", "Failed", "Cancelled"};
    static const char *const steps[] = {"Download", "Verify", "Unpack", "Write"};
    const JobProgress progress = LiveProgress();
    const unsigned stage = progress.stage;
    const unsigned percentage = progress.total ? (unsigned)((u64)progress.done * 100u / progress.total) : 0;
    char progress_label[64];
    const unsigned percent = percentage > 100 ? 100 : percentage;
    if (stage == LIVE_DOWNLOAD)
        snprintf(progress_label, sizeof(progress_label), "%u / %u KiB  ·  %u%%",
                 progress.done / 1024, (progress.total + 1023) / 1024, percent);
    else if (stage == LIVE_EXTRACT)
        snprintf(progress_label, sizeof(progress_label), "%u / %u files", progress.done, progress.total);
    else
        snprintf(progress_label, sizeof(progress_label), "%u%%", percent);

    const float left = x + 17;
    const float inner_width = width - 34;
    Panel(x, 264, width, 96, 11, 0x9cbecbff, 0x9cbecbff);
    Panel(x + 2, 266, width - 4, 92, 9, 0xf9fdffff, 0xeaf7fcff);
    const bool connecting = stage == LIVE_DOWNLOAD && !progress.done;
    Text(connecting ? "Connecting…" : stages[stage], left, 292, 21,
         inner_width - (connecting ? 0 : TextWidth(progress_label, 18) + 14), ink);
    if (!connecting) Right(progress_label, left + inner_width, 292, 18, 0x076682ff);

    Panel(left, 302, inner_width, 14, 7, 0xb3ccd8ff, 0xd7e9f0ff);
    if (connecting) {
        const float travel = inner_width - 58;
        const float offset = store->preferences & PREF_LOW_MOTION ? travel / 2 :
                             travel * (0.5f - cosf((store->now % 1200) * (6.2831853f / 1200)) / 2);
        Panel(left + 1 + offset, 303, 56, 12, 6, 0x46bbdeff, 0x0786b0ff);
    } else if (percentage) {
        const float filled = (inner_width - 2) * (percentage > 100 ? 100 : percentage) / 100;
        Panel(left + 1, 303, filled, 12, filled < 12 ? filled / 2 : 6, 0x46bbdeff, 0x0786b0ff);
    }

    for (unsigned step = 0; step < 4; ++step) {
        const float label_width = TextWidth(steps[step], 17);
        const float center = left + (step + .5f) * inner_width / 4;
        const float dot = center - (label_width + 18) / 2;
        const u32 mark = step <= stage ? 0x087998ff : 0x87a8b6ff;
        Panel(dot, 334, 10, 10, 5, mark, mark);
        if (step == stage) Panel(dot + 3, 337, 4, 4, 2, white, white);
        Text(steps[step], dot + 18, 346, 17, label_width + 1, step == stage ? ink : sub);
    }
}

static void DrawDetails(const Store *store) {
    const App current = AppAt(store->app);
    const App *app = &current;
    const unsigned category = store->members[store->app];
    const float width = ScreenWidth() - SAFE * 2;
    const bool active = store->job && store->job_app == store->app;
    Card(SAFE, 123, width, active ? 253 : 231, false, 0);
    Banner(store, store->app, SAFE + 17, 145, 128);
    if (LiveIconLoading(store->app)) LoadingRing(SAFE + 153, 175, 7, store->now);
    const float header_x = SAFE + 156;
    const float header_width = width - 173;
    Text(categories[category].name, header_x, 150, 18, header_width, sub);
    Text(app->name, header_x, 182, 32, header_width, ink);
    char line[144];
    snprintf(line, sizeof(line), "%s%s%s", app->author, app->author[0] && app->version[0] ? " · " : "", app->version);
    Text(line, header_x, 205, 18, header_width, sub);
    Paragraph(app->description, SAFE + 17, 232, 18, width - 34, 2, ink);
    if (active) {
        if (store->job == INSTALLING) {
            DrawInstallProgress(store, SAFE + 17, width - 34);
            if (LiveProgress().stage < LIVE_COMMIT) Hint(SAFE + 6, "B", "Cancel");
        } else {
            LoadingRing(ScreenWidth() / 2, 294, 16, store->now);
            Center("Removing files…", ScreenWidth() / 2, 342, 20, width - 40, ink);
        }
        Hint(ScreenWidth() - SAFE - HintWidth("2", "Downloads"), "2", "Downloads");
        return;
    }
    Box(SAFE + 17, 263, width - 34, 2, 0x9abbc8ff);

    static const char *const platform_names[] = {"Wii", "vWii", "Wii mini"};
    static const char *const pad_names[] = {"Wii Remote", "Nunchuk", "Classic", "GameCube", "USB keyboard"};
    const float left = SAFE + 17;
    const float middle = left + (width - 34) * .31f;
    const float right = left + (width - 34) * .82f;
    Fact("CONSOLES", MaskLabel(line, sizeof(line), app->platforms, platform_names, 3), left, 282, middle - left - 10);
    Text("CONTROLLERS", middle, 282, 16, right - middle - 10, sub);
    Paragraph(MaskLabel(line, sizeof(line), app->controllers, pad_names, 5), middle, 304, 18, right - middle - 10, 2, ink);
    if (app->bytes >= 1048576) snprintf(line, sizeof(line), "%.1f MiB", app->bytes / 1048576.0);
    else snprintf(line, sizeof(line), "%u KiB", (app->bytes + 1023) / 1024);
    Fact("DOWNLOAD", app->bytes ? line : "Unknown", right, 282, ScreenWidth() - SAFE - right - 10);
    if (app->license[0] || app->released[0]) {
        snprintf(line, sizeof(line), "%s%s%s%s", app->license[0] ? "License · " : "", app->license,
                 app->license[0] && app->released[0] ? "   ·   " : "", app->released);
        Text(line, left, 346, 16, width - 34, sub);
    }

    const char *action = PrimaryAction(store->app);
    if (*action) ActionButton(action, SAFE, 145, true);
    else Text("Get on a PC", SAFE, 387, 20, 145, ink);
    if (CanRemove(store->app)) ActionButton("Remove", SAFE + 157, 112, false);
    ActionButton("Source", SAFE + 281, 112, false);
    const bool has_newer = app->newer[0] && LiveFind(app->newer) < LiveCount();
    if (has_newer) ActionButton("Newer app", SAFE + 405, 124, false);
    if (store->message[0]) Text(store->message, SAFE, 420, 17, width, sub);
    else if (store->vWii && !(app->platforms & 2)) Text("Wii only", SAFE, 420, 18, width, sub);
    else if (!LiveHasSD()) Text("Insert an SD card, then refresh in Settings.", SAFE, 420, 17, width, sub);
    float x = Hint(SAFE + 6, "B", "Back");
    if (*action) x = Hint(x, "A", action);
    x = Hint(x, "1", "Source");
    if (CanRemove(store->app)) x = Hint(x, "2", "Remove");
    if (has_newer) Hint(x, "+", "Newer app");
}

enum { KEY_COLUMNS = 8, KEY_COUNT = 40, SETTINGS_ROWS = 7 };

static const char keyboard_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static float KeyboardWidth(void) { return (ScreenWidth() - SAFE * 2 - 7 * 7) / (float)KEY_COLUMNS; }
static float KeyboardX(unsigned key) { return SAFE + key % KEY_COLUMNS * (KeyboardWidth() + 7); }
static float KeyboardY(unsigned key) { return 170 + key / KEY_COLUMNS * 49; }
static float SettingsY(unsigned row) { return 151 + row * 38; }

static void DrawSearch(const Store *store) {
    const float width = ScreenWidth() - 2 * SAFE;
    Text("Search", SAFE, 129, 26, 200, ink);
    char count[24];
    snprintf(count, sizeof(count), "%u matches", store->matches);
    Right(count, ScreenWidth() - SAFE, 129, 18, sub);
    Text(store->query[0] ? store->query : "Type an app name or author", SAFE, 157, 20, width, sub);
    static const char *const special[] = {"Space", "Delete", "Clear", "Results"};
    for (unsigned key = 0; key < KEY_COUNT; ++key) {
        const float x = KeyboardX(key);
        const float y = KeyboardY(key);
        const float w = KeyboardWidth();
        Card(x, y, w, 42, key == store->keyboard, 0);
        char letter[2] = {0};
        if (key < sizeof(keyboard_letters) - 1) letter[0] = keyboard_letters[key];
        const bool special_key = !letter[0];
        const char *label = special_key ? special[key - (sizeof(keyboard_letters) - 1)] : letter;
        Center(label, x + w / 2, y + 28, special_key ? 16 : 22, w - 8, ink);
    }
    float x = Hint(SAFE + 6, "B", "Back");
    x = Hint(x, "A", "Select key");
    Hint(x, "+", "Show results");
}

static void DrawSettings(const Store *store) {
    const float width = ScreenWidth() - 2 * SAFE;
    static const char *const labels[] = {
        "Refresh catalogue", "Classic or GameCube only", "Sound effects", "Music",
        "Reduce motion", "Downloads", "Storage"
    };
    char disk[48];
    snprintf(disk, sizeof(disk), "%s", LiveHasSD() ? "SD card" : "SD card unavailable");
    u32 free_kib;
    if (LiveFreeKiB(&free_kib)) snprintf(disk, sizeof(disk), "SD card · %.0f MiB free", free_kib / 1024.0);
    const char *const values[] = {
        LiveCatalogueState() == CATALOGUE_LOADING ? "Checking…" : LiveOffline() ? "Offline" : "",
        store->preferences & PREF_GAMECUBE ? "On" : "Off",
        store->preferences & PREF_EFFECTS ? "On" : "Off",
        store->preferences & PREF_MUSIC ? "On" : "Off",
        store->preferences & PREF_LOW_MOTION ? "On" : "Off",
        store->job ? "In progress" : "",
        disk
    };
    Text("Settings", SAFE, 138, 26, 240, ink);
    for (unsigned row = 0; row < SETTINGS_ROWS; ++row) {
        const float y = SettingsY(row);
        Card(SAFE, y, width, 32, row == store->settings, 0);
        Text(labels[row], SAFE + 15, y + 22, 19, width - 180, ink);
        Right(values[row], ScreenWidth() - SAFE - 15, y + 22, 17, sub);
    }
    if (store->message[0]) Text(store->message, SAFE + 4, 420, 16, width - 8, sub);
    const float x = Hint(SAFE + 6, "B", "Back");
    Hint(x, "A", "Select");
}

static void DrawSource(const Store *store) {
    const float width = ScreenWidth() - 2 * SAFE;
    const App app = AppAt(store->app);
    Text("App source", SAFE, 138, 26, 240, ink);
    Card(SAFE, 153, width, 259, false, 0);
    float x = SAFE + 18;
    if (store->qr_size) {
        const float module = 4;
        const float y = 185;
        const float qr_width = (store->qr_size + 8) * module;
        Box(x, y, qr_width, qr_width, white);
        for (unsigned row = 0; row < store->qr_size; ++row) {
            for (unsigned col = 0; col < store->qr_size; ++col) {
                if (store->qr[row * store->qr_size + col])
                    Box(x + (col + 4) * module, y + (row + 4) * module, module, module, 0x182d3aff);
            }
        }
        x += qr_width + 18;
    }
    const float limit = ScreenWidth() - SAFE - 16 - x;
    Text(app.name, x, 181, 23, limit, ink);
    Fact("PROJECT", app.page[0] ? "" : "No project link listed", x, 214, limit);
    if (app.page[0]) Paragraph(app.page, x, 237, 17, limit, 3, ink);
    Text("PACKAGE", x, 314, 16, limit, sub);
    Paragraph(app.download, x, 338, 17, limit, 3, ink);
    Hint(SAFE + 6, "B", "App details");
}

static void DrawDownloads(const Store *store) {
    const float width = ScreenWidth() - 2 * SAFE;
    Text("Downloads", SAFE, 138, 26, 260, ink);
    Card(SAFE, 154, width, 249, false, 0);
    if (store->job) {
        const App app = AppAt(store->job_app);
        Text(app.name, SAFE + 18, 193, 24, width - 36, ink);
        if (store->queued) {
            char queue[60];
            snprintf(queue, sizeof(queue), "Updating %u of %u apps", store->queue_at, store->queue_count);
            Text(queue, SAFE + 18, 223, 18, width - 36, sub);
        }
        if (store->job == INSTALLING) DrawInstallProgress(store, SAFE + 17, width - 34);
        else Text("Removing files…", SAFE + 18, 279, 20, width - 36, ink);
    } else {
        Text(store->message[0] ? "Last operation" : "No active downloads", SAFE + 18, 193, 24, width - 36, ink);
        if (store->message[0]) Paragraph(store->message, SAFE + 18, 227, 19, width - 36, 3, sub);
    }
    const float x = Hint(SAFE + 6, "B", "Back");
    if (store->job == INSTALLING && LiveProgress().stage < LIVE_COMMIT) Hint(x, "A", "Cancel");
    if (!store->job && store->retry) Hint(x, "A", "Retry");
}

enum { QUESTION_WIDTH = 516, QUESTION_Y = 138, QUESTION_HEIGHT = 205, QUESTION_BUTTON = 206 };

static float QuestionX(bool proceed) {
    return (ScreenWidth() - QUESTION_WIDTH) / 2.0f + (proceed ? QUESTION_WIDTH - QUESTION_BUTTON - 39 : 39);
}

static void DrawConfirmation(const Store *store) {
    const char *title = "";
    const char *description = "";
    const char *safe = "Cancel";
    const char *proceed = "Continue";
    char summary[108];
    switch (store->confirmation) {
        case REPLACE:
            title = "Replace this app?";
            description = "Rift Store will replace the existing app on the SD card.";
            proceed = "Replace app";
            break;
        case WII_ONLY:
            title = "This app is marked Wii only";
            description = "The catalogue does not list support for Wii Mode on Wii U.";
            break;
        case REMOVE:
            title = "Remove this app?";
            description = "The store will remove its installed files and check what data is left.";
            proceed = "Remove app";
            break;
        case ERASE_DATA:
            title = "Keep saves and settings?";
            description = "The app was removed. You can keep its data or erase it from the SD card.";
            safe = "Keep data";
            proceed = "Erase data";
            break;
        case UPDATE_ALL: {
            title = "Update all apps?";
            unsigned count = 0;
            u64 bytes = 0;
            for (unsigned i = 0; i < LiveCount(); ++i) {
                if (LiveInstallState(i) != APP_UPDATE || (store->vWii && !(AppAt(i).platforms & 2))) continue;
                ++count;
                bytes += AppAt(i).bytes;
            }
            snprintf(summary, sizeof(summary), "%u updates · %.1f MiB to download · SD card", count, bytes / 1048576.0);
            description = summary;
            proceed = "Update all";
            break;
        }
        case NONE: return;
    }
    const float left = (ScreenWidth() - QUESTION_WIDTH) / 2.0f;
    Card(left, QUESTION_Y, QUESTION_WIDTH, QUESTION_HEIGHT, false, 0);
    Center(title, ScreenWidth() / 2.0f, 181, 28, QUESTION_WIDTH - 40, ink);
    Paragraph(description, left + 34, 219, 19, QUESTION_WIDTH - 68, 2, sub);
    for (unsigned i = 0; i < 2; ++i) {
        const float x = QuestionX(i != 0);
        Card(x, 273, QUESTION_BUTTON, 45, store->confirm_choice == (i != 0), 0);
        Center(i ? proceed : safe, x + QUESTION_BUTTON / 2, 302, 20, QUESTION_BUTTON - 20, ink);
    }
}

enum { EXIT_MENU_WIDTH = 520, EXIT_MENU_Y = 120, EXIT_MENU_HEIGHT = 248,
       EXIT_BUTTON_WIDTH = 214, EXIT_BUTTON_HEIGHT = 48, EXIT_BUTTON_GAP = 16 };

static void ExitButtonPosition(ExitChoice choice, float *x, float *y) {
    const float margin = (EXIT_MENU_WIDTH - 2 * EXIT_BUTTON_WIDTH - EXIT_BUTTON_GAP) / 2.0f;
    *x = (ScreenWidth() - EXIT_MENU_WIDTH) / 2.0f + margin + (choice % 2) * (EXIT_BUTTON_WIDTH + EXIT_BUTTON_GAP);
    *y = 203 + (choice / 2) * (EXIT_BUTTON_HEIGHT + 10);
}

static void DrawExitMenu(const Store *store) {
    static const char *const names[] = {"RiftWii", "Homebrew Channel", "Shut Down", "Back to Store"};
    const float center = ScreenWidth() / 2.0f;
    const float left = center - EXIT_MENU_WIDTH / 2.0f;
    Panel(left, EXIT_MENU_Y + 5, EXIT_MENU_WIDTH, EXIT_MENU_HEIGHT, 23, 0x21384f74, 0x21384f74);
    Panel(left, EXIT_MENU_Y, EXIT_MENU_WIDTH, EXIT_MENU_HEIGHT, 22, 0x88d8eeff, 0x4896b2ff);
    Panel(left + 2, EXIT_MENU_Y + 2, EXIT_MENU_WIDTH - 4, EXIT_MENU_HEIGHT - 4, 20, white, 0xe6f5faff);
    Center("HOME Menu", center, 159, 30, 270, ink);
    Box(left + 33, 168, EXIT_MENU_WIDTH - 66, 2, 0x98bdccff);
    Center(store->exit_status ? store->exit_status : "Where would you like to go?", center, 193, 18,
           EXIT_MENU_WIDTH - 64, sub);
    for (ExitChoice choice = EXIT_RIFTWII; choice <= EXIT_CANCEL; ++choice) {
        float x, y;
        ExitButtonPosition(choice, &x, &y);
        Card(x, y, EXIT_BUTTON_WIDTH, EXIT_BUTTON_HEIGHT, choice == store->exit_focus, 0);
        Center(names[choice], x + EXIT_BUTTON_WIDTH / 2, y + 31, 21, EXIT_BUTTON_WIDTH - 20, ink);
    }
    Center("A Select   ·   B Back", center, 342, 18, 270, sub);
}

static const WPADData *InfraredData(const Store *store, s32 channel) {
    if (!(store->connected_remotes & (1u << channel))) return NULL;
    const WPADData *data = WPAD_Data(channel);
    return data && data->ir.valid ? data : NULL;
}

static void DrawOtherPointers(const Store *store) {
    static const u32 tint[] = {0xc9a5acaa, 0xa6c3b3aa, 0xcac29eaa};
    for (s32 channel = WPAD_CHAN_1; channel <= WPAD_CHAN_3; ++channel) {
        const WPADData *data = InfraredData(store, channel);
        if (data) {
            float x = data->ir.x;
            float y = data->ir.y;
            ScreenToContent(&x, &y);
            DrawPointer(x, y, tint[channel - WPAD_CHAN_1]);
        }
    }
}

static void Render(const Store *store) {
    DrawContentSpace(false);
    DrawBackdrop(store->preferences & PREF_LOW_MOTION ? 0 : store->now);
    DrawContentSpace(true);
    DrawHeader();
    switch (store->screen) {
        case HOME: DrawHome(store); break;
        case LIST: DrawList(store); break;
        case DETAILS: DrawDetails(store); break;
        case SEARCH: DrawSearch(store); break;
        case SETTINGS: DrawSettings(store); break;
        case SOURCE: DrawSource(store); break;
        case DOWNLOADS: DrawDownloads(store); break;
    }
    if (!(store->preferences & PREF_LOW_MOTION)) {
        const float fade = AnimationRemaining(store->now, store->screen_started, SCREEN_FADE_MS);
        if (fade > 0) Box(0, 120, ScreenWidth(), 300, 0xf6fcff00 | (u32)(145 * fade));
    }
    if (store->exit_open || store->confirmation != NONE) {
        DrawContentSpace(false);
        Box(0, 0, ScreenWidth(), 480, 0x19344880);
        DrawContentSpace(true);
        if (store->exit_open) DrawExitMenu(store);
        else DrawConfirmation(store);
    }
    DrawOtherPointers(store);
    if (store->cursor != BUTTON_FOCUS) DrawPointer(store->pointer_x, store->pointer_y, white);
    if (store->shutting_down) {
        const float fade = 1 - AnimationRemaining(store->now, store->shutdown_started, SHUTDOWN_FADE_MS);
        DrawContentSpace(false);
        Box(0, 0, ScreenWidth(), 480, (u32)(255 * fade));
    }
    DrawEnd();
}

static bool MatchesController(const Store *store, const App *app) {
    return !(store->preferences & PREF_GAMECUBE) || (app->controllers & (4 | 8));
}

static void PopulateCategories(Store *store) {
    memset(store->counts, 0, sizeof(store->counts));
    store->visible_count = 0;
    for (unsigned index = 0; index < LiveCount(); ++index) {
        const App app = AppAt(index);
        unsigned category = 0;
        while (category < MORE_CATEGORY && strcmp(app.category, categories[category].key)) ++category;
        store->members[index] = category;
        if (MatchesController(store, &app)) ++store->counts[category];
    }
    for (unsigned category = 0; category <= MORE_CATEGORY; ++category) {
        if (store->counts[category]) store->visible_categories[store->visible_count++] = category;
    }
    if (store->home_cursor >= store->visible_count) store->home_cursor = 0;
}

static void PopulateList(Store *store) {
    const unsigned category = store->visible_categories[store->home_cursor];
    store->list_count = 0;
    if (store->list_mode == MY_APPS) {
        for (unsigned app = 0; app < LiveCount(); ++app) {
            if (LiveInstallState(app) == APP_UPDATE) store->list[store->list_count++] = app;
        }
    }
    if (store->list_mode != MY_APPS || !store->updates_only) {
        for (unsigned index = 0; index < LiveCount(); ++index) {
            const App app = AppAt(index);
            const InstallState state = LiveInstallState(index);
            bool include = false;
            if (store->list_mode == MY_APPS) include = state == APP_INSTALLED || state == APP_ON_CARD;
            else if (MatchesController(store, &app)) {
                include = store->list_mode == SEARCH_RESULTS ? LiveMatches(index, store->query)
                                                           : store->members[index] == category;
            }
            if (include) store->list[store->list_count++] = index;
        }
    }
    if (!store->list_count) store->list_cursor = 0;
    else if (store->list_cursor >= store->list_count) store->list_cursor = store->list_count - 1;
}

static void OpenList(Store *store, ListMode mode) {
    store->screen = LIST;
    store->list_mode = mode;
    store->list_cursor = 0;
    PopulateList(store);
}

static void OpenDetails(Store *store) {
    if (!store->list_count) return;
    store->app = store->list[store->list_cursor];
    store->confirmation = NONE;
    Message(store, "");
    store->screen = DETAILS;
}

static void CountMatches(Store *store) {
    store->matches = 0;
    for (unsigned index = 0; index < LiveCount(); ++index) {
        const App app = AppAt(index);
        if (MatchesController(store, &app) && LiveMatches(index, store->query)) ++store->matches;
    }
}

static void Ask(Store *store, Confirmation confirmation) {
    store->confirmation = confirmation;
    store->confirm_choice = false;
    SoundPlay(confirmation == REMOVE || confirmation == ERASE_DATA ? CUE_REMOVE_PROMPT : CUE_OPEN);
}

static bool Compatible(const Store *store, unsigned index) {
    return !store->vWii || (AppAt(index).platforms & 2);
}

static bool BeginJob(Store *store, unsigned index, Operation job, bool erase) {
    if (!LiveHasSD()) {
        Message(store, "Insert an SD card and refresh the catalogue.");
        return false;
    }
    if (store->job || !(job == INSTALLING ? LiveStartInstall(index) : LiveStartRemove(index, erase))) {
        Message(store, "The store is busy. Try again in a moment.");
        return false;
    }
    store->job = job;
    store->job_app = index;
    store->retry = false;
    Message(store, "");
    SoundPlay(CUE_OPEN);
    return true;
}

static void OpenDownloads(Store *store) {
    store->downloads_back = store->screen;
    store->screen = DOWNLOADS;
}

static void BeginQueue(Store *store) {
    store->queue_count = 0;
    store->queue_at = 0;
    for (unsigned index = 0; index < LiveCount(); ++index) {
        if (LiveInstallState(index) == APP_UPDATE && Compatible(store, index))
            store->queue[store->queue_count++] = (u16)index;
    }
    if (!store->queue_count) {
        Message(store, "No compatible updates are queued.");
        OpenDownloads(store);
        return;
    }
    store->queued = BeginJob(store, store->queue[store->queue_at++], INSTALLING, false);
    OpenDownloads(store);
}

static void CancelJob(Store *store) {
    if (store->job != INSTALLING) return;
    if (LiveProgress().stage >= LIVE_COMMIT && LiveProgress().stage < LIVE_DONE) return;
    store->queued = false;
    LiveCancelJob();
}

static void ConfirmationInput(Store *store, unsigned keys) {
    if (keys & INPUT_LEFT) store->confirm_choice = false;
    if (keys & INPUT_RIGHT) store->confirm_choice = true;
    if (!(keys & (INPUT_A | INPUT_B))) return;
    const Confirmation confirmation = store->confirmation;
    store->confirmation = NONE;
    if ((keys & INPUT_B) || !store->confirm_choice) {
        if (confirmation == ERASE_DATA) Message(store, "App removed. Saves and settings kept.");
        SoundPlay(CUE_BACK);
        return;
    }
    switch (confirmation) {
        case UPDATE_ALL: BeginQueue(store); break;
        case REMOVE: BeginJob(store, store->app, REMOVING, false); break;
        case ERASE_DATA: BeginJob(store, store->app, REMOVING, true); break;
        case REPLACE:
            if (!Compatible(store, store->app)) Ask(store, WII_ONLY);
            else BeginJob(store, store->app, INSTALLING, false);
            break;
        case WII_ONLY: BeginJob(store, store->app, INSTALLING, false); break;
        case NONE: break;
    }
}

static void EditSearch(Store *store, unsigned keys) {
    if (keys & INPUT_B) {
        store->screen = HOME;
        return;
    }
    if ((keys & INPUT_LEFT) && store->keyboard % KEY_COLUMNS) --store->keyboard;
    if ((keys & INPUT_RIGHT) && store->keyboard % KEY_COLUMNS < KEY_COLUMNS - 1) ++store->keyboard;
    if ((keys & INPUT_UP) && store->keyboard >= KEY_COLUMNS) store->keyboard -= KEY_COLUMNS;
    if ((keys & INPUT_DOWN) && store->keyboard < KEY_COUNT - KEY_COLUMNS) store->keyboard += KEY_COLUMNS;
    if ((keys & INPUT_PLUS) || ((keys & INPUT_A) && store->keyboard == KEY_COUNT - 1)) {
        OpenList(store, SEARCH_RESULTS);
        if (store->search_resume < store->list_count) store->list_cursor = store->search_resume;
        return;
    }
    if (!(keys & INPUT_A)) return;
    store->search_resume = 0;
    const size_t length = strlen(store->query);
    switch (store->keyboard) {
        case 37: if (length) store->query[length - 1] = '\0'; break;
        case 38: store->query[0] = '\0'; break;
        default:
            if (length + 1 < sizeof(store->query)) {
                store->query[length] = store->keyboard == 36 ? ' ' : keyboard_letters[store->keyboard];
                store->query[length + 1] = '\0';
            }
            break;
    }
    CountMatches(store);
    SoundPlay(CUE_MOVE);
}

static void RefreshCatalogue(Store *store) {
    if (store->job || LiveCatalogueState() == CATALOGUE_LOADING) return;
    store->screen = HOME;
    store->visible_count = 0;
    store->generation = (unsigned)-1;
    store->retry = false;
    LiveRefresh();
}

static void ChangeSetting(Store *store, unsigned keys) {
    if (keys & INPUT_B) {
        store->screen = HOME;
        return;
    }
    if ((keys & INPUT_UP) && store->settings) --store->settings;
    if ((keys & INPUT_DOWN) && store->settings + 1 < SETTINGS_ROWS) ++store->settings;
    if (!(keys & INPUT_A)) return;
    static const u8 flags[] = {0, PREF_GAMECUBE, PREF_EFFECTS, PREF_MUSIC, PREF_LOW_MOTION};
    if (store->settings == 0) {
        RefreshCatalogue(store);
    } else if (store->settings == 6) {
        Message(store, LiveCheckStorage() ? "" : "Could not check the SD card right now.");
    } else if (store->settings == 5) {
        OpenDownloads(store);
    } else {
        store->preferences ^= flags[store->settings];
        SoundSettings(store->preferences & PREF_EFFECTS, store->preferences & PREF_MUSIC);
        PopulateCategories(store);
        Message(store, LiveSavePreferences(store->preferences) ? "" : "Preferences apply for this session.");
    }
}

static void ApplyInput(Store *store, unsigned keys) {
    if (store->screen == HOME) {
        if (keys & INPUT_TWO) {
            store->screen = SETTINGS;
            return;
        }
        if (LiveCatalogueState() != CATALOGUE_READY) {
            if (keys & INPUT_A) RefreshCatalogue(store);
            return;
        }
        int next = (int)store->home_cursor;
        if ((keys & INPUT_RIGHT) && next % HOME_COLUMNS < HOME_COLUMNS - 1) ++next;
        if ((keys & INPUT_LEFT) && next % HOME_COLUMNS > 0) --next;
        if (keys & INPUT_UP) next -= HOME_COLUMNS;
        if (keys & INPUT_DOWN) next += HOME_COLUMNS;
        if (next >= 0 && next < (int)store->visible_count) store->home_cursor = next;
        if (keys & INPUT_PLUS) OpenList(store, MY_APPS);
        else if (keys & INPUT_ONE) {
            CountMatches(store);
            store->screen = SEARCH;
        } else if ((keys & INPUT_A) && store->visible_count) OpenList(store, BY_CATEGORY);
    } else if (store->screen == LIST) {
        if (keys & INPUT_B) {
            if (store->list_mode == SEARCH_RESULTS) store->search_resume = store->list_cursor;
            store->screen = store->list_mode == SEARCH_RESULTS ? SEARCH : HOME;
            return;
        }
        if (store->list_mode == MY_APPS && (keys & INPUT_TWO)) {
            store->updates_only = !store->updates_only;
            store->list_cursor = 0;
            PopulateList(store);
        }
        if (store->list_mode == MY_APPS && !store->job && (keys & INPUT_ONE) && LiveUpdateCount()) {
            Ask(store, UPDATE_ALL);
            return;
        }
        if (store->list_count) {
            const AppGrid grid = GridLayout(store->list_count);
            const unsigned first = store->list_cursor / grid.per_page * grid.per_page;
            const unsigned slot = store->list_cursor % grid.per_page;
            const unsigned column = slot % grid.columns;
            unsigned next = store->list_cursor;
            if ((keys & INPUT_MINUS) && first > 0) next = first - grid.per_page;
            else if ((keys & INPUT_PLUS) && first + grid.per_page < store->list_count) next = first + grid.per_page;
            else if ((keys & INPUT_LEFT) && column > 0) --next;
            else if ((keys & INPUT_RIGHT) && column + 1 < grid.columns) ++next;
            else if ((keys & INPUT_UP) && slot >= grid.columns) next -= grid.columns;
            else if ((keys & INPUT_DOWN) && slot < grid.columns && first + grid.columns < store->list_count) {
                next += grid.columns;
                if (next >= store->list_count) next = store->list_count - 1;
            }
            if (next < store->list_count) store->list_cursor = next;
        }
        if (keys & INPUT_A) OpenDetails(store);
    } else if (store->screen == DETAILS) {
        const App app = AppAt(store->app);
        const bool active = store->job && store->job_app == store->app;
        if (keys & INPUT_B) {
            if (active) CancelJob(store);
            else {
                PopulateList(store);
                store->screen = LIST;
            }
        } else if (active) {
            if (keys & INPUT_TWO) OpenDownloads(store);
        } else if (keys & INPUT_ONE) {
            store->qr_size = 0;
            if (app.page[0]) LiveQR(app.page, store->qr, &store->qr_size);
            store->screen = SOURCE;
        } else if ((keys & INPUT_PLUS) && app.newer[0]) {
            const unsigned newer = LiveFind(app.newer);
            if (newer < LiveCount()) {
                store->app = newer;
                Message(store, "");
            }
        } else if ((keys & INPUT_TWO) && !store->job && CanRemove(store->app)) {
            Ask(store, REMOVE);
        } else if ((keys & INPUT_A) && !store->job && (app.flags & APP_INSTALLABLE)) {
            switch (LiveInstallState(store->app)) {
                case APP_INSTALLED:
                    LiveCloseNetwork();
                    Message(store, SystemOpenApp(app.id));
                    break;
                case APP_ON_CARD: Ask(store, REPLACE); break;
                case APP_UPDATE:
                case APP_GET:
                    if (!Compatible(store, store->app)) Ask(store, WII_ONLY);
                    else BeginJob(store, store->app, INSTALLING, false);
                    break;
                case APP_PC_ONLY: break;
            }
        }
    } else if (store->screen == SEARCH) {
        EditSearch(store, keys);
    } else if (store->screen == SETTINGS) {
        ChangeSetting(store, keys);
    } else if (store->screen == SOURCE) {
        if (keys & INPUT_B) store->screen = DETAILS;
    } else if (store->screen == DOWNLOADS) {
        if (keys & INPUT_A) {
            if (store->job) CancelJob(store);
            else if (store->retry) BeginJob(store, store->job_app, INSTALLING, false);
        }
        if (keys & INPUT_B) store->screen = store->downloads_back;
    }
}

static unsigned PointerAction(Store *store) {
    const float x = store->pointer_x;
    const float y = store->pointer_y;
    if (store->exit_open) {
        for (ExitChoice choice = EXIT_RIFTWII; choice <= EXIT_CANCEL; ++choice) {
            float left, top;
            ExitButtonPosition(choice, &left, &top);
            if (x >= left && x < left + EXIT_BUTTON_WIDTH && y >= top && y < top + EXIT_BUTTON_HEIGHT) {
                store->exit_focus = choice;
                return INPUT_A;
            }
        }
        return 0;
    }
    if (store->confirmation != NONE) {
        if (y >= 273 && y < 318) {
            for (unsigned i = 0; i < 2; ++i) {
                const float left = QuestionX(i != 0);
                if (x >= left && x < left + QUESTION_BUTTON) {
                    store->confirm_choice = i != 0;
                    return INPUT_A;
                }
            }
        }
        return 0;
    }
    if (y >= HINT_TOP && y < HINT_TOP + HINT_HEIGHT) {
        const float start = SAFE + 6;
        if (store->screen == HOME) {
            if (LiveCatalogueState() != CATALOGUE_READY || !store->visible_count) {
                if (x >= ScreenWidth() - SAFE - HintWidth("2", "Settings") && x < ScreenWidth() - SAFE)
                    return INPUT_TWO;
                if (LiveCatalogueState() == CATALOGUE_FAILED && x >= start && x < start + HintWidth("A", "Retry"))
                    return INPUT_A;
                return 0;
            }
            char label[28] = "My Apps";
            const unsigned updates = LiveUpdateCount();
            if (updates) snprintf(label, sizeof(label), "My Apps (%u)", updates);
            const float library = start + HintWidth("A", "Open category") + HINT_GAP;
            const float search = library + HintWidth("+", label) + HINT_GAP;
            const float settings = search + HintWidth("1", "Search") + HINT_GAP;
            if (x >= library && x < search - HINT_GAP) return INPUT_PLUS;
            if (x >= search && x < settings - HINT_GAP) return INPUT_ONE;
            if (x >= settings && x < settings + HintWidth("2", "Settings")) return INPUT_TWO;
        } else {
            const char *label = "Back";
            if (store->screen == LIST) label = store->list_mode == SEARCH_RESULTS ? "Edit search" : "Categories";
            if (store->screen == SOURCE) label = "App details";
            if (store->screen == DETAILS && store->job && store->job_app == store->app) {
                if (x >= ScreenWidth() - SAFE - HintWidth("2", "Downloads") && x < ScreenWidth() - SAFE)
                    return INPUT_TWO;
                if (store->job == REMOVING || LiveProgress().stage >= LIVE_COMMIT) return 0;
                label = "Cancel";
            }
            if (x >= start && x < start + HintWidth("B", label)) return INPUT_B;
            float next = start + HintWidth("B", label) + HINT_GAP;
            if (store->screen == LIST && store->list_mode == MY_APPS) {
                const char *filter = store->updates_only ? "All installed" : "Updates only";
                if (x >= next && x < next + HintWidth("2", filter)) return INPUT_TWO;
                next += HintWidth("2", filter) + HINT_GAP;
                if (LiveUpdateCount() && x >= next && x < next + HintWidth("1", "Update all")) return INPUT_ONE;
            } else if (store->screen == SEARCH) {
                next += HintWidth("A", "Select key") + HINT_GAP;
                if (x >= next && x < next + HintWidth("+", "Show results")) return INPUT_PLUS;
            } else if (store->screen == DOWNLOADS && store->job == INSTALLING && LiveProgress().stage < LIVE_COMMIT) {
                if (x >= next && x < next + HintWidth("A", "Cancel")) return INPUT_A;
            } else if (store->screen == DOWNLOADS && !store->job && store->retry) {
                if (x >= next && x < next + HintWidth("A", "Retry")) return INPUT_A;
            }
        }
    }
    if (store->screen == HOME) {
        const float width = HomeTileWidth();
        const float height = HomeTileHeight(store);
        for (unsigned index = 0; index < store->visible_count; ++index) {
            const float left = SAFE + index % HOME_COLUMNS * (width + 10);
            const float top = 153 + index / HOME_COLUMNS * (height + 8);
            if (x >= left && x < left + width && y >= top && y < top + height) {
                store->home_cursor = index;
                return INPUT_A;
            }
        }
    } else if (store->screen == LIST) {
        const AppGrid grid = GridLayout(store->list_count);
        const unsigned first = store->list_cursor / grid.per_page * grid.per_page;
        if (first && OverPageArrow(store, false)) return INPUT_MINUS;
        if (first + grid.per_page < store->list_count && OverPageArrow(store, true)) return INPUT_PLUS;
        for (unsigned slot = 0; slot < grid.per_page && first + slot < store->list_count; ++slot) {
            float left, top;
            GridTile(store, grid, slot, &left, &top);
            if (x >= left && x < left + grid.width && y >= top && y < top + GRID_HEIGHT) {
                store->list_cursor = first + slot;
                return INPUT_A;
            }
        }
    } else if (store->screen == DETAILS && !(store->job && store->job_app == store->app) && y >= 361 && y < 399) {
        if (x >= SAFE && x < SAFE + 145 && *PrimaryAction(store->app)) return INPUT_A;
        if (x >= SAFE + 157 && x < SAFE + 269 && CanRemove(store->app)) return INPUT_TWO;
        if (x >= SAFE + 281 && x < SAFE + 393) return INPUT_ONE;
        const App app = AppAt(store->app);
        if (x >= SAFE + 405 && x < SAFE + 529 && app.newer[0] && LiveFind(app.newer) < LiveCount()) return INPUT_PLUS;
    } else if (store->screen == SEARCH) {
        for (unsigned key = 0; key < KEY_COUNT; ++key) {
            const float left = KeyboardX(key);
            const float top = KeyboardY(key);
            if (x >= left && x < left + KeyboardWidth() && y >= top && y < top + 42) {
                store->keyboard = key;
                return INPUT_A;
            }
        }
    } else if (store->screen == SETTINGS && x >= SAFE && x < ScreenWidth() - SAFE) {
        for (unsigned row = 0; row < SETTINGS_ROWS; ++row) {
            const float top = SettingsY(row);
            if (y >= top && y < top + 32) {
                store->settings = row;
                return INPUT_A;
            }
        }
    }
    return 0;
}

static bool MoveGcPointer(Store *store, u32 elapsed) {
    const float dx = PAD_StickX(0);
    const float dy = -PAD_StickY(0);
    const float length_squared = dx * dx + dy * dy;
    if (length_squared <= 16 * 16) return false;
    const float length = sqrtf(length_squared);
    const float strength = fminf(1, (length - 16) / 64);
    const float speed = 70 + 830 * strength * strength;
    const float movement = speed * elapsed / (1000 * length);

    if (!store->gc_pointer_started) {
        store->gc_pointer_x = store->cursor == WII_POINTER ? store->pointer_x : ScreenWidth() / 2.0f;
        store->gc_pointer_y = store->cursor == WII_POINTER ? store->pointer_y : 240;
        store->gc_pointer_started = true;
    }
    // Clamp the fingertip to the visible title-safe frame, in layout units.
    float left = ScreenWidth() * .05f;
    float top = 24;
    ScreenToContent(&left, &top);
    store->gc_pointer_x = fminf(ScreenWidth() - left, fmaxf(left, store->gc_pointer_x + dx * movement));
    store->gc_pointer_y = fminf(480 - top, fmaxf(top, store->gc_pointer_y + dy * movement));
    return true;
}

static void ConfigureRemote(s32 channel) {
    WPAD_SetDataFormat(channel, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(channel, ScreenWidth(), 480);
}

static void RefreshRemotes(Store *store) {
    WPAD_ScanPads();
    unsigned connected = 0;
    for (s32 channel = WPAD_CHAN_0; channel <= WPAD_CHAN_3; ++channel) {
        u32 expansion = 0;
        if (WPAD_Probe(channel, &expansion) != WPAD_ERR_NONE) continue;
        const unsigned bit = 1u << channel;
        if (!(store->connected_remotes & bit)) {
            // Remotes can join after boot and need their own IR resolution.
            ConfigureRemote(channel);
        }
        connected |= bit;
    }
    store->connected_remotes = connected;
}

static unsigned ReadInput(Store *store) {
    RefreshRemotes(store);
    const bool gc_connected = (PAD_ScanPads() & 1u) != 0;
    // Other controllers are never an input source, even in the HOME menu.
    const u32 remote = (store->connected_remotes & 1u) ? WPAD_ButtonsDown(WPAD_CHAN_0) : 0;
    const u16 pad = gc_connected ? PAD_ButtonsDown(0) : 0;
    const WPADData *data = InfraredData(store, WPAD_CHAN_0);
    const u32 elapsed = store->now - store->last_input;
    store->last_input = store->now;
    unsigned keys = 0;
    if ((remote & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP)) || (pad & PAD_BUTTON_UP)) keys |= INPUT_UP;
    if ((remote & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN)) || (pad & PAD_BUTTON_DOWN)) keys |= INPUT_DOWN;
    if ((remote & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT)) || (pad & PAD_BUTTON_LEFT)) keys |= INPUT_LEFT;
    if ((remote & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) || (pad & PAD_BUTTON_RIGHT)) keys |= INPUT_RIGHT;
    if ((remote & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B)) || (pad & PAD_BUTTON_B)) keys |= INPUT_B;
    if ((remote & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS)) || (pad & PAD_TRIGGER_R)) keys |= INPUT_PLUS;
    if ((remote & (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS)) || (pad & PAD_TRIGGER_L)) keys |= INPUT_MINUS;
    if ((remote & (WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_X)) || (pad & PAD_BUTTON_X)) keys |= INPUT_ONE;
    if ((remote & (WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_Y)) || (pad & PAD_BUTTON_Y)) keys |= INPUT_TWO;
    if ((remote & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) || (pad & PAD_BUTTON_START)) keys |= INPUT_HOME;
    const bool remote_a = remote & WPAD_BUTTON_A;
    bool ir_active = false;
    if (data) {
        const float dx = store->ir_x - data->ir.x;
        const float dy = store->ir_y - data->ir.y;
        ir_active = !store->ir_present || dx * dx + dy * dy >= 16 ||
                    (remote_a && store->cursor == GC_POINTER);
        store->ir_x = data->ir.x;
        store->ir_y = data->ir.y;
    }
    store->ir_present = data != NULL;

    if (keys & (INPUT_UP | INPUT_DOWN | INPUT_LEFT | INPUT_RIGHT)) {
        store->cursor = BUTTON_FOCUS;
    } else if (gc_connected && MoveGcPointer(store, elapsed > 34 ? 34 : elapsed)) {
        store->cursor = GC_POINTER;
    } else if (ir_active && (!(pad & PAD_BUTTON_A) || store->cursor != GC_POINTER)) {
        store->cursor = WII_POINTER;
    } else if ((store->cursor == WII_POINTER && !data) || (store->cursor == GC_POINTER && !gc_connected)) {
        store->cursor = BUTTON_FOCUS;
    }

    if (store->cursor == WII_POINTER) {
        store->pointer_x = store->ir_x;
        store->pointer_y = store->ir_y;
        ScreenToContent(&store->pointer_x, &store->pointer_y);
    } else if (store->cursor == GC_POINTER) {
        store->pointer_x = store->gc_pointer_x;
        store->pointer_y = store->gc_pointer_y;
    }
    const unsigned action = store->cursor == BUTTON_FOCUS ? 0 : PointerAction(store);
    if (remote_a) keys |= store->cursor == WII_POINTER ? action : INPUT_A;
    if (pad & PAD_BUTTON_A) keys |= store->cursor == GC_POINTER ? action : INPUT_A;
    if (remote & WPAD_CLASSIC_BUTTON_A) keys |= INPUT_A;
    return keys;
}

static unsigned FocusedTile(const Store *store) {
    if (store->screen == HOME) return store->home_cursor;
    if (store->screen == SEARCH) return store->keyboard;
    if (store->screen == SETTINGS) return store->settings;
    return store->list_cursor;
}

static void NavigationFeedback(Store *store, Screen previous, unsigned old_tile, unsigned keys) {
    if (store->screen != previous) {
        store->screen_started = store->now;
        store->focus_started = store->now;
        store->slide_direction = 0;
        SoundPlay(keys & INPUT_B ? CUE_BACK : CUE_OPEN);
        return;
    }
    const unsigned new_tile = FocusedTile(store);
    if (store->screen == DETAILS || old_tile == new_tile) return;
    store->focus_started = store->now;
    if (store->screen == LIST) {
        const unsigned per_page = GridLayout(store->list_count).per_page;
        if (old_tile / per_page != new_tile / per_page) {
            store->page_started = store->now;
            store->slide_direction = new_tile > old_tile ? 1 : -1;
            SoundPlay(CUE_PAGE);
            return;
        }
    }
    SoundPlay(CUE_MOVE);
}

static void RunExit(Store *store, ExitChoice choice) {
    static const char *const status[] = {"Opening RiftWii…", "Opening the Homebrew Channel…", "Shutting down…"};
    store->exit_open = true;
    store->exit_focus = choice;
    store->exit_status = status[choice];
    store->exit_pending = true;
    store->queued = false;
    LiveStop();
    if (choice == EXIT_POWER) {
        store->shutting_down = true;
        store->shutdown_started = store->now;
    }
}

static void ExitMenuInput(Store *store, ExitChoice previous, unsigned keys) {
    if (keys & INPUT_HOME) {
        store->exit_open = !store->exit_open;
        store->exit_focus = EXIT_CANCEL;
        store->exit_status = NULL;
        SoundPlay(store->exit_open ? CUE_OPEN : CUE_BACK);
        return;
    }
    if (keys & INPUT_B) {
        store->exit_open = false;
        SoundPlay(CUE_BACK);
        return;
    }
    unsigned next = store->exit_focus;
    if ((keys & INPUT_LEFT) && next % 2) --next;
    if ((keys & INPUT_RIGHT) && !(next % 2)) ++next;
    if ((keys & INPUT_UP) && next >= 2) next -= 2;
    if ((keys & INPUT_DOWN) && next < 2) next += 2;
    store->exit_focus = (ExitChoice)next;
    if (keys & INPUT_A) {
        if (store->exit_focus == EXIT_CANCEL) {
            store->exit_open = false;
            SoundPlay(CUE_BACK);
        } else {
            SoundPlay(CUE_OPEN);
            RunExit(store, store->exit_focus);
        }
    } else if (store->exit_focus != previous) {
        SoundPlay(CUE_MOVE);
    }
}

static void PumpStore(Store *store) {
    LivePump(++store->frame);
    if (LiveCatalogueState() == CATALOGUE_READY && store->generation != LiveGeneration()) {
        store->generation = LiveGeneration();
        PopulateCategories(store);
    }
    JobResult result;
    if (LiveTakeResult(&result)) {
        store->job = IDLE;
        store->retry = !result.ok && !result.removed && LiveProgress().stage != LIVE_CANCELLED;
        if (result.ok) {
            Message(store, result.removed ? "App removed from the SD card." : "App is ready on the SD card.");
            SoundPlay(result.removed ? CUE_REMOVE_DONE : CUE_COMPLETE);
        } else {
            const char *reason = result.message;
            if (LiveProgress().stage == LIVE_CANCELLED && !result.removed) reason = "Install cancelled.";
            Message(store, reason && reason[0] ? reason : "The operation could not finish.");
            SoundPlay(CUE_BACK);
        }
        if (!store->exit_pending && result.ok && result.removed && result.saves_left) {
            store->app = result.index;
            store->screen = DETAILS;
            Ask(store, ERASE_DATA);
        }
        if (store->queued) {
            if (!result.ok || store->queue_at >= store->queue_count) store->queued = false;
            else store->queued = BeginJob(store, store->queue[store->queue_at++], INSTALLING, false);
        }
        if (store->screen == LIST) PopulateList(store);
    }
    if (store->job || store->exit_pending || store->confirmation != NONE) return;
    if (store->screen == DETAILS) {
        LiveWantIcons(&store->app, 1, store->frame);
    } else if (store->screen == LIST) {
        const AppGrid grid = GridLayout(store->list_count);
        const unsigned first = store->list_cursor / grid.per_page * grid.per_page;
        const unsigned remaining = store->list_count - first;
        LiveWantIcons(store->list + first, remaining < grid.per_page ? remaining : grid.per_page, store->frame);
    } else {
        LiveWantIcons(NULL, 0, store->frame);
    }
}

int main(void) {
    if (!DrawInit()) return EXIT_FAILURE;
    WPAD_Init();
    for (s32 channel = WPAD_CHAN_0; channel <= WPAD_CHAN_3; ++channel) ConfigureRemote(channel);
    PAD_Init();
    LiveInit();
    SystemExitInit();

    Store store = {0};
    store.preferences = LiveLoadPreferences();
    SoundSettings(store.preferences & PREF_EFFECTS, store.preferences & PREF_MUSIC);
    SoundInit();
    store.vWii = SystemIsVWii();
    store.screen_started = (u32)ticks_to_millisecs(gettime());
    for (;;) {
        store.now = (u32)ticks_to_millisecs(gettime());
        PumpStore(&store);
        if (store.exit_pending) {
            Render(&store);
            if (!LiveBusy() && (!store.shutting_down || store.now - store.shutdown_started >= SHUTDOWN_FADE_MS)) {
                LiveCloseNetwork();
                store.exit_status = SystemExitRun(store.exit_focus);
                store.exit_pending = false;
                LiveResume();
            }
            continue;
        }
        const Screen previous = store.screen;
        const unsigned old_tile = FocusedTile(&store);
        const ExitChoice old_exit = store.exit_focus;
        const unsigned keys = ReadInput(&store);
        if (SystemPowerRequested()) {
            RunExit(&store, EXIT_POWER);
        } else if (store.exit_open || (keys & INPUT_HOME)) {
            ExitMenuInput(&store, old_exit, keys);
        } else if (store.confirmation != NONE) {
            ConfirmationInput(&store, keys);
        } else {
            ApplyInput(&store, keys);
            NavigationFeedback(&store, previous, old_tile, keys);
        }
        Render(&store);
    }
}
