// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "live.h"
#include "live_platform.hpp"
#include "log.hpp"
#include "store_data.hpp"

#include <fat.h>
#include <gccore.h>
#include <ogc/cache.h>
#include <ogc/lwp.h>
#include <sdcard/wiisd_io.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "entropy.hpp"
#include "httpmem.hpp"
#include "netsock.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/qrcode.hpp"

namespace {
using namespace riftwii;
using namespace riftwii::wii;

constexpr unsigned kIconSlots = 24;
constexpr unsigned kIconWidth = 128, kIconHeight = 48;
alignas(32) std::uint8_t icon_pixels[kIconSlots][kIconWidth * kIconHeight * 2];
bool icon_used[kIconSlots];
appstore::Store store;
std::vector<std::size_t> visible;
bool mounted = false;
bool stopping = false;
std::atomic<bool> space_known{false};
std::atomic<std::uint32_t> space_kib{0};
void (*background_job)(void*) = nullptr;
void* background_arg = nullptr;

void Error(char* out, std::uint32_t bytes, const char* reason) {
    if (bytes) std::snprintf(out, bytes, "%s", reason);
}

std::int32_t PathInfo(const char* path, rwp_path_info* out) {
    struct stat info;
    if (stat(path, &info) != 0 || info.st_size < 0) return -1;
    out->is_directory = S_ISDIR(info.st_mode);
    out->size = out->is_directory ? 0 : static_cast<std::uint64_t>(info.st_size);
    return 0;
}

std::int32_t Read(const char* path, std::uint32_t offset, void* data, std::uint32_t size) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return -1;
    const bool positioned = std::fseek(file, offset, SEEK_SET) == 0;
    const std::size_t got = positioned ? std::fread(data, 1, size, file) : 0;
    const bool ok = positioned && !std::ferror(file);
    std::fclose(file);
    return ok ? static_cast<std::int32_t>(got) : -1;
}

std::int32_t MakeDirectory(const char* path) {
    if (mkdir(path, 0777) == 0) return 0;
    rwp_path_info info;
    return PathInfo(path, &info) == 0 && info.is_directory ? 0 : -1;
}

std::int32_t FreeKiB(std::uint32_t* out) {
    struct statvfs info;
    if (!mounted || statvfs("sd:/", &info) != 0) return -1;
    const std::uint64_t free = (static_cast<std::uint64_t>(info.f_bavail) * info.f_frsize) / 1024;
    *out = static_cast<std::uint32_t>(std::min<std::uint64_t>(free, UINT32_MAX));
    return 0;
}

void CheckStorage(void*) {
    std::uint32_t value;
    if (FreeKiB(&value) != 0) return;
    space_kib.store(value, std::memory_order_relaxed);
    space_known.store(true, std::memory_order_release);
}

std::uint32_t CreateTexture(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height) {
    if (width != kIconWidth || height != kIconHeight) return 0;
    unsigned slot = 0;
    while (slot < kIconSlots && icon_used[slot]) ++slot;
    if (slot == kIconSlots) return 0;

    std::uint8_t* output = icon_pixels[slot];
    for (unsigned y = 0; y < height; y += 4) {
        for (unsigned x = 0; x < width; x += 4) {
            for (unsigned row = 0; row < 4; ++row) {
                for (unsigned col = 0; col < 4; ++col) {
                    const std::uint8_t* p = rgba + ((y + row) * width + x + col) * 4;
                    const unsigned texel = p[3] == 255
                        ? 0x8000 | (p[0] >> 3) << 10 | (p[1] >> 3) << 5 | (p[2] >> 3)
                        : (p[3] >> 5) << 12 | (p[0] >> 4) << 8 | (p[1] >> 4) << 4 | (p[2] >> 4);
                    *output++ = texel >> 8;
                    *output++ = texel & 255;
                }
            }
        }
    }
    DCFlushRange(icon_pixels[slot], sizeof(icon_pixels[slot]));
    GX_InvalidateTexAll();
    icon_used[slot] = true;
    return slot + 1;
}

void FreeTexture(std::uint32_t handle) {
    if (handle && handle <= kIconSlots) icon_used[handle - 1] = false;
}

std::int32_t NetReady(char* error, std::uint32_t error_bytes) {
#ifdef RIFT_STORE_TEST
    rwp_path_info fixture;
    if (PathInfo("sd:/riftwii/rift-store/test-package", &fixture) == 0) return 1;
#endif
    std::string reason;
    if (NetStart(reason, true)) return 1;
    Error(error, error_bytes, reason.c_str());
    return 0;
}

#ifdef RIFT_STORE_TEST
constexpr const char* kListFixture = "sd:/riftwii/rift-store/test-catalogue.json";
constexpr const char* kPackageFixture = "sd:/riftwii/rift-store/test-package";
constexpr const char* kOfflineFixture = "sd:/riftwii/rift-store/test-offline";

int TestDownload(const char* path, std::uint32_t max_bytes, std::uint8_t* digest,
                 void (*progress)(std::uint32_t, void*), void* arg) {
    FILE* source = std::fopen(kPackageFixture, "rb");
    if (!source) return -1;
    FILE* target = std::fopen(path, "wb");
    Sha256 sha;
    std::uint8_t block[4096];
    std::uint32_t total = 0;
    bool ok = target != nullptr;
    while (ok) {
        const std::size_t count = std::fread(block, 1, sizeof(block), source);
        if (!count) break;
        if (count > max_bytes - total || NetCancelRequested() || std::fwrite(block, 1, count, target) != count) {
            ok = false;
            break;
        }
        sha.update(block, count);
        total += count;
        if (progress) progress(total, arg);
    }
    ok = ok && !std::ferror(source);
    std::fclose(source);
    if (target && std::fclose(target) != 0) ok = false;
    if (!ok) {
        std::remove(path);
        return 0;
    }
    const auto checksum = sha.finish();
    std::copy(checksum.begin(), checksum.end(), digest);
    return 1;
}
#endif

std::int32_t HttpGet(const char* url, std::uint8_t* buffer, std::uint32_t capacity, std::uint32_t* length,
                     char* error, std::uint32_t error_bytes) {
    const bool catalogue = std::strcmp(url, kAppListUrl) == 0;
    const std::string_view address(url);
    constexpr std::string_view legacy = "https://riftwii.github.io/apps/icons/";
    constexpr std::string_view canonical = "https://riftwii.com/apps/icons/";
    std::string request;
    if (catalogue) request = "https://riftwii.com/apps/apps.json";
    else if (address.compare(0, legacy.size(), legacy) == 0) request = std::string(canonical) + (url + legacy.size());
    else if (address.compare(0, canonical.size(), canonical) == 0) request = url;
    else {
        Error(error, error_bytes, "The icon address is outside RiftWii's catalogue.");
        return 0;
    }
#ifdef RIFT_STORE_TEST
    rwp_path_info info;
    if (PathInfo(kOfflineFixture, &info) == 0) {
        Error(error, error_bytes, "Offline test fixture.");
        return 0;
    }
    if (catalogue && PathInfo(kListFixture, &info) == 0) {
        const std::int32_t got = Read(kListFixture, 0, buffer, capacity);
        if (got <= 0) return 0;
        *length = static_cast<std::uint32_t>(got);
        return 1;
    }
#endif
    if (!NetReady(error, error_bytes)) return 0;
    MemoryGet get;
    get.buffer = buffer;
    get.capacity = capacity;
    get.max_body = catalogue ? kAppListMaxBytes : kAppIconMaxBytes;
    get.user_agent = "RiftStore";
    std::string reason;
    if (!HttpGetInMemory(request, get, reason, 12000, nullptr, HttpRedirects::SameHost)) {
        Error(error, error_bytes, reason.c_str());
        return 0;
    }
    std::memmove(buffer, buffer + get.body_offset, get.body_length);
    *length = static_cast<std::uint32_t>(get.body_length);
    return 1;
}

std::int32_t Download(const char* url, const char* path, std::uint32_t max_bytes, std::uint8_t* digest,
                      void (*progress)(std::uint32_t, void*), void* arg, char* error, std::uint32_t error_bytes) {
#ifdef RIFT_STORE_TEST
    const int test = TestDownload(path, max_bytes, digest, progress, arg);
    if (test >= 0) {
        if (!test) Error(error, error_bytes, "The local test package could not be copied.");
        return test;
    }
#endif
    FileDownload result;
    std::string reason;
    const HttpProgress report = [progress, arg](std::size_t done) {
        if (progress) progress(static_cast<std::uint32_t>(done), arg);
    };
    if (!HttpDownloadToFile(url, path, max_bytes, result, reason, 15000, report)) {
        Error(error, error_bytes, reason.c_str());
        return 0;
    }
    std::copy(result.digest.begin(), result.digest.end(), digest);
    return 1;
}

void RunBackground() { background_job(background_arg); }

std::int32_t StartBackground(void (*job)(void*), void* arg) {
    if (stopping || NetBackgroundBusy()) return -1;
    background_job = job;
    background_arg = arg;
    return NetRunInBackground(RunBackground, true, false) ? 0 : -1;
}

std::uint32_t BackgroundBusy() { return NetBackgroundBusy(); }
const char* English(const char* text) { return text; }
std::uintptr_t CurrentThread() { return LWP_GetSelf(); }
} // namespace

namespace riftwii::wii {
// The launcher is quiesced before handoff and libfat serializes the card.
CardWriteHold::CardWriteHold() = default;
CardWriteHold::~CardWriteHold() = default;

void logf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}
} // namespace riftwii::wii

const rwp_host& StandaloneHost() {
    static const rwp_host host = [] {
        rwp_host h{};
        h.size = sizeof(h);
        h.log = riftwii::wii::logf;
        h.translate = English;
        h.file_read = Read;
        h.path_info = PathInfo;
        h.make_directory = MakeDirectory;
        h.remove_path = std::remove;
        h.rename_path = std::rename;
        h.texture_create = CreateTexture;
        h.texture_free = FreeTexture;
        h.sd_free_kib = FreeKiB;
        h.net_ready = NetReady;
        h.http_get = HttpGet;
        h.http_download = Download;
        h.background_start = StartBackground;
        h.background_running = BackgroundBusy;
        h.background_cancel = NetCancelBackground;
        return h;
    }();
    return host;
}

void* StoreMem2Low() { return SYS_GetArena2Lo(); }

void* ReserveStoreMem2(std::size_t size) {
    const auto low = (reinterpret_cast<std::uintptr_t>(StoreMem2Low()) + 31) & ~std::uintptr_t(31);
    const auto high = reinterpret_cast<std::uintptr_t>(SYS_GetArena2Hi());
    if (low > high || size > high - low) return nullptr;
    SYS_SetArena2Lo(reinterpret_cast<void*>(low + size));
    return reinterpret_cast<void*>(low);
}

void ReleaseStoreMem2(void* begin, void* end) {
    if (StoreMem2Low() == end) SYS_SetArena2Lo(begin);
}

void LiveInit() {
    mounted = fatMountSimple("sd", &__io_wiisd);
    set_thread_identity(CurrentThread);
    EntropyStart();
    if (!try_reserve(visible, 8) || !store.InitMemory()) {
        store.list_error = "There is not enough memory to start the catalogue.";
        store.list_state = appstore::ListState::Failed;
        return;
    }
    store.StartList();
}

void LiveRefresh() {
    if (!mounted) mounted = fatMountSimple("sd", &__io_wiisd);
    store.StartList();
}

void LivePump(u32 frame) { store.Pump(frame); }
bool LiveOffline() { return LiveCatalogueState() == CATALOGUE_READY && store.offline; }
const char* LiveCatalogueError() { return store.list_error.c_str(); }
unsigned LiveGeneration() { return store.generation; }
unsigned LiveCount() { return LiveCatalogueState() == CATALOGUE_READY ? store.apps.size() : 0; }

CatalogueState LiveCatalogueState() {
    switch (store.list_state) {
        case appstore::ListState::Ready: return CATALOGUE_READY;
        case appstore::ListState::Failed: return CATALOGUE_FAILED;
        default: return CATALOGUE_LOADING;
    }
}

bool LiveRead(unsigned index, App* out) {
    if (index >= LiveCount()) return false;
    const auto& app = store.apps[index];
    const auto& item = *app.listing;
    *out = {item.name, item.author, item.description, item.version, item.license, item.category,
            item.id, item.page, item.url, item.newer, item.released, item.size, static_cast<u16>(app.texture),
            item.platforms, item.controllers, static_cast<u8>(app_installable(item) ? APP_INSTALLABLE : 0)};
    return true;
}

bool LiveIconLoading(unsigned index) {
    if (index >= LiveCount() || LiveOffline()) return false;
    const auto& app = store.apps[index];
    return app.listing->icon[0] && !app.texture && !app.icon_failed;
}

bool LiveMatches(unsigned index, const char* query) {
    if (index >= LiveCount()) return false;
    const auto& listing = *store.apps[index].listing;
    if (app_matches(listing, query)) return true;
    const std::string_view description(listing.description);
    std::string_view words(query);
    const auto fold = [](char ch) { return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch; };
    while (!words.empty()) {
        const auto end = words.find(' ');
        const auto word = words.substr(0, end);
        if (!word.empty() && std::search(description.begin(), description.end(), word.begin(), word.end(),
                [&](char a, char b) { return fold(a) == fold(b); }) == description.end()) return false;
        if (end == std::string_view::npos) break;
        words.remove_prefix(end + 1);
    }
    return true;
}

unsigned LiveFind(const char* id) { return store.Find(id); }
unsigned LiveUpdateCount() { return LiveCount() ? store.UpdateCount() : 0; }

InstallState LiveInstallState(unsigned index) {
    if (index >= LiveCount()) return APP_GET;
    switch (store.apps[index].state) {
        case AppState::Installed: return APP_INSTALLED;
        case AppState::Update: return APP_UPDATE;
        case AppState::OnCard: return APP_ON_CARD;
        case AppState::PcOnly: return APP_PC_ONLY;
        default: return APP_GET;
    }
}

void LiveWantIcons(const unsigned* indices, unsigned count, u32 frame) {
    if (!LiveCount() || LiveOffline() || stopping) return;
    visible.clear();
    for (unsigned i = 0; i < count && i < 8; ++i) visible.push_back(indices[i]);
    store.WantIcons(visible, frame);
}

const u8* LiveIconPixels(unsigned handle) {
    return handle && handle <= kIconSlots && icon_used[handle - 1] ? icon_pixels[handle - 1] : nullptr;
}

bool LiveStartInstall(unsigned index) { return mounted && store.StartInstall(index); }
bool LiveStartRemove(unsigned index, bool erase_data) {
    return mounted && (erase_data ? store.StartRemoveData(index) : store.StartRemove(index));
}

bool LiveTakeResult(JobResult* out) {
    if (!store.TakeFinished()) return false;
    *out = {store.finished_ok, store.finished_remove, !store.finished_leftovers.empty(),
            static_cast<unsigned>(store.finished_index), store.finished_error.c_str()};
    return true;
}

JobProgress LiveProgress() {
    const auto sample = store.progress.Read();
    return {static_cast<JobStage>(sample.stage), sample.done, sample.total};
}

void LiveCancelJob() { store.CancelInstall(); }
bool LiveBusy() { return store.Busy() || NetBackgroundBusy(); }
void LiveStop() {
    stopping = true;
    store.Shutdown();
    NetCancelBackground();
}
void LiveResume() {
    stopping = false;
    store.Resume();
}
void LiveCloseNetwork() { NetStopForLaunch(); }
bool LiveHasSD() { return mounted; }

bool LiveCheckStorage() {
    if (LiveBusy()) return false;
    if (!mounted) mounted = fatMountSimple("sd", &__io_wiisd);
    space_known.store(false, std::memory_order_relaxed);
    return mounted && StartBackground(CheckStorage, nullptr) == 0;
}

bool LiveFreeKiB(u32* result) {
    if (!space_known.load(std::memory_order_acquire)) return false;
    *result = space_kib.load(std::memory_order_relaxed);
    return true;
}

constexpr const char* kPreferences = "sd:/riftwii/rift-store/preferences";

bool LiveSavePreferences(u8 flags) {
    if (!mounted || MakeDirectory("sd:/riftwii") || MakeDirectory("sd:/riftwii/rift-store")) return false;
    const char* pending = "sd:/riftwii/rift-store/preferences.tmp";
    FILE* file = std::fopen(pending, "wb");
    if (!file) return false;
    const bool written = std::fputc(flags, file) != EOF;
    if (std::fclose(file) != 0 || !written) {
        std::remove(pending);
        return false;
    }
    // libfat does not replace a destination on rename.
    if (std::rename(pending, kPreferences) == 0) return true;
    if (std::remove(kPreferences) == 0 && std::rename(pending, kPreferences) == 0) return true;
    std::remove(pending);
    return false;
}

u8 LiveLoadPreferences() {
    u8 flags;
    return Read(kPreferences, 0, &flags, 1) == 1 && (flags & 0xf0) == 0 ? flags : 3;
}

bool LiveQR(const char* url, u8* modules, unsigned* size) {
    QrCode qr;
    if (!make_qr(url, qr)) return false;
    std::copy(qr.dark.begin(), qr.dark.end(), modules);
    *size = static_cast<unsigned>(qr.size);
    return true;
}
