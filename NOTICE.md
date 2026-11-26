# Credits and provenance

Rift Store source is available under GPL-3.0-or-later.

- The catalogue parser and SD installer are adapted from `src/net/applist.cpp` and `plugins/appstore/storedata.cpp` at RiftWii revision [94990481](https://github.com/KakarottoCake/Riftwii/tree/94990481c5b12b37b9a6768541f67763fe1db6ba), GPL-3.0-or-later. The build reuses that revision's install planner, ZIP and PNG readers, networking, and TLS integration. The app handoff follows RiftWii's channel DOL loader.
- BearSSL 0.6 comes from RiftWii's pinned `vendor-bearssl` tree under its MIT license. The build retains the upstream license with the fetched source.
- The M PLUS Rounded 1c font is RiftWii's subset of the M+ Fonts Project font. See [the SIL Open Font License](assets/OFL.txt).
- Audio uses Hermes's ASND mixer and libogc's libmad MP3 player. See [the ASND license](LICENSE-ASND.txt).
- The app uses RiftWii's Rift Store mark. Homebrew banners come from the live catalogue and remain the artwork of their respective projects.
