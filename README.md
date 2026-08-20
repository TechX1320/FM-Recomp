# Yu-Gi-Oh! Forbidden Memories Recompiled

A Windows x64 recompilation project for the North American PlayStation release of **Yu-Gi-Oh! Forbidden Memories** (`SLUS-01411`). It uses the bundled PSXRecomp framework to translate the original PS1 executable to native code and run it against a PlayStation hardware runtime.

**Current release:** `v0.1.11` — SPU volume-sweep work. The v0.1.10 baseline has been verified through menus, 3D scenes, story progression, and a complete NPC duel. The v0.1.11 SPU sweep implementation passes its unit/structural checks, but still needs broad real-game audio confirmation.

> This repository does **not** include a PlayStation BIOS, the game executable, the game disc image, or copyrighted game assets. You must provide your own legally obtained dumps.

## Fast setup

### 1. Install the Windows build tools

Install [MSYS2](https://www.msys2.org/) to its default location:

```text
C:\msys64
```

Open **MSYS2 MinGW64** once and install:

```bash
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkgconf python
```

### 2. Add your own BIOS and game dump

Put these three files in `user-files/`:

```text
SCPH1001.BIN
Yu-Gi-Oh! Forbidden Memories (USA).cue
Yu-Gi-Oh! Forbidden Memories (USA).bin
```

The BIN and CUE may instead be inside a ZIP placed in `user-files/`. The BIOS must remain a separate file.

You **do not** need to provide `SLUS_014.11` separately. The build extracts it directly from the verified BIN.

### 3. Build

Double-click:

```text
BUILD_FORBIDDEN_MEMORIES.cmd
```

The build process will:

1. locate the user-supplied BIOS and disc files;
2. verify their hashes and disc layout;
3. extract and verify `SLUS_014.11` from the disc;
4. regenerate BIOS/game translated source as needed;
5. build `ForbiddenMemories.exe`;
6. create a ready-to-run `dist-windows/` folder.

**Do not upload or redistribute `dist-windows/`.** It contains copies of your locally supplied BIOS and game disc. The folder is ignored by Git for this reason.

### 4. Run

Start with the currently established playable path:

```text
dist-windows\RUN_FM_APV2_BYPASS.cmd
```

Other launchers in `dist-windows/` are diagnostic or presentation variants. For first testing, use the APv2 bypass launcher above.

## Exact required dumps

The verifier intentionally accepts one known North American BIOS revision and one exact USA disc revision. This keeps reverse-engineering results reproducible.

### PlayStation BIOS — SCPH-1001 / NTSC-U

| Property | Required value |
|---|---|
| Filename | `SCPH1001.BIN` |
| Size | `524288` bytes |
| CRC32 | `37157331` |
| MD5 | `924e392ed05558ffdb115408c263dccf` |
| SHA-1 | `10155d8d6e6e832d6ea66db9bc098321fb5e8ebf` |
| SHA-256 | `71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3` |

A file named `SCPH1001.BIN` with CRC32 `D1AC0608` is OpenBIOS, not the Sony SCPH-1001 ROM required by this build, and is rejected.

### Yu-Gi-Oh! Forbidden Memories (USA) disc

| File | Size | CRC32 | SHA-1 | SHA-256 |
|---|---:|---|---|---|
| `Yu-Gi-Oh! Forbidden Memories (USA).bin` | `517872768` | `73E4C771` | `d5785a41900a10968d4a28a390666c4b9879b796` | `6e22494a45bf50fa2d239cd3819a57163a5f9b91e0365babc3e101509b5c3a7c` |
| `Yu-Gi-Oh! Forbidden Memories (USA).cue` | `100` | `BCEED6FA` | `ec2cd0458c22ac4aa7c2ecb4aa81a5ca3c7751ec` | `72467fb3a7ea6053a1925e33b7be716a95f4e99ebb7c66a767b1a551d8bd3dda` |

The extracted executable is also verified automatically:

| File | Size | CRC32 | SHA-1 | SHA-256 |
|---|---:|---|---|---|
| `SLUS_014.11` | `1902592` | `B6176A00` | `84747e64f6da8e764206ec203e489acf8c9dcf7d` | `84a54ed74f3d0edd6d81380839f7e4ef5bfb21ecea18be9a062bd6bfa5a45c88` |

### Verify a file manually on Windows

PowerShell can calculate a SHA-256 without extra software:

```powershell
Get-FileHash .\SCPH1001.BIN -Algorithm SHA256
Get-FileHash '.\Yu-Gi-Oh! Forbidden Memories (USA).bin' -Algorithm SHA256
Get-FileHash '.\Yu-Gi-Oh! Forbidden Memories (USA).cue' -Algorithm SHA256
```

## Controls

Default keyboard mapping inherited from PSXRecomp:

| PlayStation | Keyboard |
|---|---|
| D-Pad | Arrow keys |
| Cross | `X` |
| Square | `Z` |
| Circle | `S` |
| Triangle | `A` |
| L1 / R1 | `Q` / `W` |
| L2 / R2 | `E` / `R` |
| Start | `Enter` |
| Select | Right Shift |
| Fullscreen | `F11` or `Alt+Enter` |

Xbox-style controllers are supported through SDL2 using the normal A/B/X/Y, shoulders, triggers, D-pad/left-stick, Menu and View mappings.

## Project layout

```text
bios/          local verified BIOS copy created during build; ignored by Git
input/         extracted SLUS executable created during build; ignored by Git
disc/          local verified BIN/CUE copies created during build; ignored by Git
generated/     generated Forbidden Memories C/dispatch output; ignored except metadata
psxrecomp/     bundled PSXRecomp framework snapshot
scripts/       input verification, build, and regression scripts
seeds/         game-specific static discovery seeds
user-files/    place your BIOS and disc dump here; contents are ignored by Git
```

Generated game code and BIOS-derived generated code are intentionally not distributed. A clean clone rebuilds them from your own verified inputs.

For this game repository you can clone normally; the current build uses the bundled PSXRecomp snapshot and has the optional PSXRecomp launcher disabled, so its RmlUi/FreeType launcher submodules are not required for the standard Forbidden Memories build.

## Publishing this source to GitHub

The ZIP is arranged so a normal `git add .` does not stage your BIOS, game disc, generated code, build output, saves, or runtime reports. No Git LFS setup is required for the included source tree.

Typical first push:

```bash
git init
git add .
git commit -m "Initial public source release"
git branch -M main
git remote add origin <your-github-repository-url>
git push -u origin main
```

Before every release, confirm that `user-files/`, `bios/`, `disc/`, `input/`, `dist-windows/`, and local generated output are not being uploaded. The included GitHub Actions sanity check also rejects the exact BIOS/game runtime input filenames if they are ever committed.

## Current development status

The project remains a bring-up/recompilation project rather than a completed commercial-quality port.

- Playable baseline: v0.1.10 reached and completed a full NPC duel.
- Strict unknown-dispatch fail-fast remains enabled so missing static coverage is visible instead of being silently hidden.
- The APv2 compatibility bypass is currently the established route past the affected startup path.
- v0.1.11 adds PlayStation SPU voice/main volume-sweep modeling to address missing short sound effects. Unit validation is complete; broad gameplay confirmation is still pending.
- Software rendering is the first-line baseline. OpenGL, CRT, enhanced presentation and HLE diagnostic presets are included separately.

A condensed milestone history is available in [`docs/DEVELOPMENT_HISTORY.md`](docs/DEVELOPMENT_HISTORY.md).

## Troubleshooting

**`MSYS2 was not found at C:\msys64`**  
Install MSYS2 in the default folder. The current convenience build script expects that location.

**`Required user-supplied files are missing`**  
Make sure the BIOS, BIN and CUE use the exact filenames shown above and are in `user-files/`. The BIN/CUE can also be inside a ZIP in that directory.

**Hash mismatch**  
The project supports the exact NTSC-U dump listed above. A different region, revision, modified image, bad dump or incorrect BIOS will be rejected before compilation.

**The build succeeds but the game stops/crashes**  
Preserve the generated `psx_*.json`, crash/freeze report files, and note which `RUN_*.cmd` launcher you used. Those reports are the most useful inputs for a bug report.

## Legal and licensing

Yu-Gi-Oh! Forbidden Memories is © Konami. PlayStation and the PlayStation BIOS are property of Sony Interactive Entertainment. This project is an independent reverse-engineering/recompilation effort and is not affiliated with or endorsed by Konami or Sony.

No BIOS, game disc image, extracted game executable, or copyrighted game assets are distributed by this repository. Do not open issues asking where to download those files.

The bundled PSXRecomp framework is licensed under **PolyForm Noncommercial 1.0.0**; see [`psxrecomp/LICENSE`](psxrecomp/LICENSE). Third-party components retain their own licenses. See [`LICENSE`](LICENSE) and [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Because PolyForm Noncommercial restricts commercial use, this combined repository is **source-available rather than OSI-approved open source** unless/until the relevant upstream licensing permits otherwise.
