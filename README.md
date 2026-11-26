# Rift Store

Rift Store is a standalone Wii homebrew app. Browse the live [RiftWii catalogue](https://riftwii.com/apps/apps.json), search for homebrew, and install, update, open, or remove apps on an SD card.

## Install

Extract the distribution ZIP to the root of the SD card. Open **Rift Store** in the Homebrew Channel. The executable is `sd:/apps/rift-store/boot.dol`.

Rift Store fetches the catalogue each time it starts. Select **Settings > Refresh catalogue** to check again while the app is open. Additions and updates from [RiftWii/apps](https://github.com/RiftWii/apps) reach the store without a new DOL. If the fetch fails, Rift Store uses the last readable catalogue on the card. Browse and manage installed apps offline. Package downloads and uncached banner artwork require an internet connection.

## Controls

| Wii Remote 1 | GameCube port 1 | Action |
| --- | --- | --- |
| Point the Wii Remote | Control Stick | Move the pointer |
| D-pad | D-pad | Move button focus |
| A / B | A / B | Select / go back |
| − / + | L / R | Change pages |
| + on Categories | R on Categories | My Apps |
| 1 / 2 on Categories | X / Y on Categories | Search / Settings |
| 1 / 2 on an app | X / Y on an app | Source / Remove |
| HOME | Start | RiftWii, Homebrew Channel, or shut down |

Only the first Wii Remote and GameCube port respond to controls. Wii Remotes 2–4 show muted pointers. When the GameCube stick moves the pointer, A selects the control under its fingertip. Press the D-pad to return to button focus. The HOME menu waits for current SD work to finish safely. Shut Down fades to black, then requests full standby power-off.

## Catalogue and installs

- Banners load as their tiles become visible. Browse uses a 3×2 app grid at 4:3 or a 4×2 grid at 16:9, following the Wii's Screen setting. Text and controls sit inside the 5% overscan margin. Search and the Classic/GameCube controller filter use the same catalogue.
- App details list version, author, consoles, controllers, download size, and the project and package URLs. Short project URLs have a QR code. Wii-only packages ask before installation on Wii U in Wii mode.
- My Apps detects existing programs and shared RiftWii receipts. Updates can be installed individually or queued under **Update all**. A manual installation asks before replacement. Formats other than ZIP or DOL link to their project for PC use.
- Packages stream to SD with bounded buffers and are checked against the catalogue's size and SHA-256 before extraction. Installation validates the ZIP path plan, stages the files and receipt, and commits with rollback. Cancel stops the job before commit. Remove preserves changed files and offers to keep or erase remaining app data.
- Installed apps live in `sd:/apps/<id>/`. Install receipts are shared with RiftWii at `sd:/riftwii/apps/<id>.txt`. Rift Store keeps its catalogue and preferences under `sd:/riftwii/rift-store/`.
- **Open app** loads supported Wii DOL and ELF programs. Use the Homebrew Channel for apps that need a loader-specific environment or a boot layout the built-in launcher does not support.

## Music

Place a tag-free stereo MP3 at `sd:/apps/rift-store/music.mp3` for looping background music. The file is streamed from SD. Music and sound effects can be toggled independently in Settings.

## Build

Install devkitPro with devkitPPC, libogc and the PowerPC zlib port. The asset tools need Git, Python 3.12 or newer, and Pillow. Run these in a devkitPro shell with `DEVKITPRO` and `DEVKITPPC` set.

```sh
make -j8
make memory
```

The build produces a Homebrew Channel folder at `build/apps/rift-store`. The build fetches a pinned [RiftWii core revision](https://github.com/KakarottoCake/Riftwii/tree/94990481c5b12b37b9a6768541f67763fe1db6ba) the first time. It can use a parent RiftWii checkout containing that revision. The catalogue and app artwork are runtime data.

`make memory` reports linked code, initialized data, and BSS. The parsed catalogue resides in MEM2 and refresh drops its scratch buffers after use. The 24 recycled RGB5A3 banner slots occupy 288 KiB. Large downloads stream through a 72 KiB receive window plus TLS state. The ZIP directory, install plan and extraction buffers are bounded separately.

`make TEST=1` builds an isolated Dolphin test variant at `build/test/apps/rift-store` with SD fixture support. The default build leaves the fixture paths out. To check a copy of the feed with Rift Store's parser on a host with C++17, run:

```sh
python3 tools/check_live_catalogue.py --riftwii /path/to/Riftwii --catalogue /path/to/apps.json
```

Omit `--catalogue` to check the current published list.

## License

GPL-3.0-or-later. See [NOTICE.md](NOTICE.md) for credits and dependency provenance.
