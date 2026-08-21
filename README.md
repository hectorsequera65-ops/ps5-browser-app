# PS5 Browser Search

A zero-code "web app" shortcut for jailbroken PS5 consoles. Installing it adds a **★ Browser Search** tile to the **Media** tab that opens a browser search page directly — no homebrew menu required to use it afterwards.

![Platform](https://img.shields.io/badge/platform-PS5-blue) ![License](https://img.shields.io/badge/license-GPL--3.0-green)

This repo builds two distributables from the same assets:

| Artifact | What it is | Who it's for |
|---|---|---|
| `build/IV9999-BRWS00001_00-BROWSERSEARCH001-A0100-V0100.pkg` | Signed debug PKG | Install like any homebrew PKG |
| `build/browser.elf` | Standalone installer payload | Load via your ELF loader (ps5-payload-loader, etaHEN, etc.) |

## How it works

PS5 `param.json` supports a `deeplinkUri` field. When a title's icon is launched, the system opens the browser at that URI instead of starting the executable. This project exploits that:

- **`eboot.bin` is intentionally 0 bytes** — no code ever runs.
- `sce_sys/param.json` declares:
  - `applicationCategoryType: 65536` → the tile appears under **Media**, not Games
  - `deeplinkUri: https://kvnhrt.github.io/ps5-browser-search/`
  - Title ID `BRWS00001`, display name `★ Browser Search`

Tapping the icon simply opens the browser on the search page.

## Requirements

**To build:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) — everything else (PS5 SDK toolchain, .NET 10 SDK, PKG packer) lives inside the image.

**To install:** a PS5 with an active jailbreak (debug PKGs do not install on retail firmware state).

## Building

```sh
docker compose run --rm browser-build
```

That single command compiles both artifacts into `build/`. The first run takes a while (it builds the [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) toolchain from source inside the image); subsequent runs are fast thanks to layer caching.

Individual targets:

```sh
docker compose run --rm browser-build make elf    # browser.elf only
docker compose run --rm browser-build make pkg    # .pkg only
docker compose run --rm browser-build make clean  # remove build/
```

Intermediate PKG staging happens in the container's `/tmp` and is cleaned up automatically — nothing but the final artifacts ever appears in `build/`.

## Installing

### PKG

1. Copy the `.pkg` to the console (USB drive, FTP, or your loader's file transfer).
2. Install it from your homebrew file manager / PKG installer.
3. Launch **★ Browser Search** from the **Media** tab.

To remove it later, use the console's own *Options → Delete* on the tile (deleting files over FTP alone leaves the registration behind).

### ELF

Send `browser.elf` to your payload loader as usual. It registers title `BRWS00001` via `sceAppInstUtil` using the same embedded `param.json` + icon, then shows a completion notification. Run it once; the tile persists across reboots.

If you previously installed an older build under a different title ID (e.g. `NPXS20102`), delete that old tile from the home screen before/after installing this one.

## Customizing

Everything is driven by one file: [`assets/param.json`](assets/param.json). Both the ELF (embedded at compile time) and the PKG (read verbatim by the packer) consume it byte-for-byte.

| Want to change… | Edit |
|---|---|
| Target URL | `deeplinkUri` |
| Tile name | `localizedParameters.*.titleName` |
| Title ID / Content ID | `titleId` / `contentId` (keep them consistent) |
| Icon | replace `assets/icon0.png` — must be **512×512, 32-bit ARGB PNG** or the packer rejects it |

Then rebuild. No other file needs touching.

## Repo layout

```
assets/            param.json + icon0.png (single source of truth)
src/, include/     ELF installer sources (C, ps5-payload-sdk)
tools/pkg-builder/ CLI wrapper that packs sce_sys/ into a PKG (.NET 10)
tools/LibProsperoPKG/  Vendored PKG library (see its LICENSE)
Dockerfile         Self-contained Ubuntu 24.04 image: SDK toolchain + .NET 10
docker-compose.yml browser-build service (mounts the repo at /workspace)
Makefile           elf / pkg / clean targets used inside the container
```

## Notes & troubleshooting

- The packer prints warnings about fake-signing a non-ELF module — expected, since `eboot.bin` is empty by design.
- If the container build fails with MSB3374 (`last-write time` on an `.Up2Date` file), delete leftover `bin/`/`obj/` folders under `tools/` from the **Windows** side and rebuild; the container cannot overwrite them through the bind mount.
- The PKG is debug-signed and targets jailbroken consoles only.

## Credits

- [LibProsperoPKG](https://github.com/SvenGDK/LibProsperoPKG) v2.6.0 by SvenGDK — pure managed PKG building (vendored under `tools/LibProsperoPKG`, GPL-3.0-or-later).
- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) — prospero Clang toolchain and runtime libraries.
- Docker image structure based on the OnionHEN build setup.

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).
