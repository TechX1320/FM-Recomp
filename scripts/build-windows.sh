#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if [[ "${MSYSTEM:-}" != "MINGW64" && "${MSYSTEM:-}" != "UCRT64" ]]; then
  echo "ERROR: Run BUILD_FORBIDDEN_MEMORIES.cmd from Windows Explorer."
  exit 1
fi

PYTHON=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1; then
    PYTHON="$candidate"
    break
  fi
done

missing=0
for cmd in cmake ninja gcc g++ cpp pkg-config sha256sum; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    echo "Missing build tool: $cmd"
    missing=1
  fi
done
if [[ -z "$PYTHON" ]]; then
  echo "Missing build tool: python"
  missing=1
fi
if [[ $missing -ne 0 ]]; then
  cat <<'MSG'

Install the required MSYS2 MinGW64 packages, then run the same CMD again:

pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-SDL2 mingw-w64-x86_64-pkgconf python
MSG
  exit 1
fi

USER="$ROOT/user-files"
mkdir -p "$USER" "$ROOT/bios" "$ROOT/input" "$ROOT/disc"

# Accept a user-owned archive for convenience, but extract only the exact disc
# files needed by the local build. SLUS_014.11 is extracted from the verified
# BIN later, so users only need to supply their BIOS plus BIN/CUE.
if [[ ! -f "$USER/Yu-Gi-Oh! Forbidden Memories (USA).bin" || \
      ! -f "$USER/Yu-Gi-Oh! Forbidden Memories (USA).cue" ]]; then
  "$PYTHON" - "$USER" <<'PY'
import pathlib, sys, zipfile
root = pathlib.Path(sys.argv[1])
wanted = {
    "Yu-Gi-Oh! Forbidden Memories (USA).bin",
    "Yu-Gi-Oh! Forbidden Memories (USA).cue",
}
for zpath in sorted(root.glob("*.zip")):
    try:
        with zipfile.ZipFile(zpath) as z:
            by_base = {pathlib.PurePosixPath(n).name: n for n in z.namelist()}
            matched = wanted & by_base.keys()
            if not matched:
                continue
            print(f"Extracting verified inputs from {zpath.name}...")
            for base in matched:
                out = root / base
                if out.exists():
                    continue
                with z.open(by_base[base]) as src, out.open("wb") as dst:
                    while True:
                        block = src.read(1024 * 1024)
                        if not block:
                            break
                        dst.write(block)
    except zipfile.BadZipFile:
        print(f"Skipping invalid ZIP: {zpath.name}")
PY
fi

find_source() {
  local name="$1"
  shift
  local p
  for p in "$@"; do
    if [[ -f "$p" ]]; then
      printf '%s' "$p"
      return 0
    fi
  done
  return 1
}

BIOS_SRC="$(find_source SCPH1001.BIN \
  "$USER/SCPH1001.BIN" "$ROOT/bios/SCPH1001.BIN" || true)"
CUE_SRC="$(find_source 'Yu-Gi-Oh! Forbidden Memories (USA).cue' \
  "$USER/Yu-Gi-Oh! Forbidden Memories (USA).cue" \
  "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).cue" || true)"
BIN_SRC="$(find_source 'Yu-Gi-Oh! Forbidden Memories (USA).bin' \
  "$USER/Yu-Gi-Oh! Forbidden Memories (USA).bin" \
  "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).bin" || true)"

if [[ -z "$BIOS_SRC" || -z "$CUE_SRC" || -z "$BIN_SRC" ]]; then
  cat <<'MSG'
ERROR: Required user-supplied files are missing.

Place these exact files inside user-files:
  SCPH1001.BIN
  Yu-Gi-Oh! Forbidden Memories (USA).cue
  Yu-Gi-Oh! Forbidden Memories (USA).bin

The BIN and CUE may also be inside a ZIP placed in user-files.
SLUS_014.11 is extracted automatically from the verified BIN.
MSG
  exit 1
fi

REPORT_DIR="$ROOT/input-reports"
mkdir -p "$REPORT_DIR"
EXE_SRC="$ROOT/input/SLUS_014.11"
"$PYTHON" "$ROOT/scripts/inspect-inputs.py" \
  --bios "$BIOS_SRC" \
  --extract-exe "$EXE_SRC" \
  --cue "$CUE_SRC" \
  --bin "$BIN_SRC" \
  --json "$REPORT_DIR/input-verification.json" \
  --markdown "$REPORT_DIR/input-verification.md"

echo "Verified Sony SCPH-1001 SHA-256 71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3."
echo "Verified the Redump-matching US BIN/CUE and extracted SLUS_014.11."

copy_input() {
  local src="$1" dst="$2" src_abs dst_abs
  src_abs="$(cd "$(dirname "$src")" && pwd)/$(basename "$src")"
  dst_abs="$(cd "$(dirname "$dst")" && pwd)/$(basename "$dst")"
  if [[ "$src_abs" != "$dst_abs" ]]; then
    cp -f "$src" "$dst"
  fi
}
copy_input "$BIOS_SRC" "$ROOT/bios/SCPH1001.BIN"
copy_input "$CUE_SRC" "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).cue"
copy_input "$BIN_SRC" "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).bin"

TOOLS_BUILD="$ROOT/build-tools-windows"
echo
echo "=== Building current PSXRecomp tools ==="
cmake -S "$ROOT/psxrecomp/recompiler" -B "$TOOLS_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$TOOLS_BUILD" --target psxrecomp-game psxrecomp-bios -j2

# Regenerate the BIOS from the user's exact local BIOS. The source package does
# not carry a BIOS image or BIOS-derived generated C.
mkdir -p "$ROOT/psxrecomp/bios"
copy_input "$ROOT/bios/SCPH1001.BIN" "$ROOT/psxrecomp/bios/SCPH1001.BIN"
PSXRECOMP_BIOS_BUILD="$TOOLS_BUILD" \
PSXRECOMP_BIOS_ROM="$ROOT/bios/SCPH1001.BIN" \
PSXRECOMP_BIOS_OUT="$ROOT/psxrecomp/generated" \
  bash "$ROOT/psxrecomp/tools/regen_bios.sh"

# Regenerate when requested, when generated files are absent, or when a newly
# runtime-proven indirect target is not present in an older generated dispatch.
# This last case lets a small source hotfix safely upgrade an existing tree.
GAME_RECOMP="$TOOLS_BUILD/psxrecomp-game.exe"
[[ -f "$GAME_RECOMP" ]] || GAME_RECOMP="$TOOLS_BUILD/psxrecomp-game"
required_game_entries=(0x80049000 0x80012CD4 0x8005C1F4 0x80033DB0)
game_regen_needed=0
if [[ "${FM_REGENERATE_GAME:-0}" == "1" || \
      ! -f "$ROOT/generated/SLUS_014.11_full.c" || \
      ! -f "$ROOT/generated/SLUS_014.11_dispatch.c" ]]; then
  game_regen_needed=1
else
  for required_entry in "${required_game_entries[@]}"; do
    if ! grep -q "{${required_entry}u," "$ROOT/generated/SLUS_014.11_dispatch.c"; then
      echo "Generated dispatch predates required entry ${required_entry}; scheduling regeneration."
      game_regen_needed=1
      break
    fi
  done
fi
if [[ $game_regen_needed -ne 0 ]]; then
  echo
  echo "=== Regenerating Forbidden Memories static translation ==="
  "$GAME_RECOMP" --config "$ROOT/game.toml"
fi

# Runtime regression guards. Traces proved these entries are reached through
# continuations or callback tables and therefore must remain statically emitted.
for required_entry in "${required_game_entries[@]}"; do
  if ! grep -q "{${required_entry}u," "$ROOT/generated/SLUS_014.11_dispatch.c"; then
    echo "ERROR: generated game dispatch is missing required entry ${required_entry}."
    echo "Run with FM_REGENERATE_GAME=1 or use a clean source tree."
    exit 1
  fi
done

apv2_guard_failed=0
if ! grep -q 'PSX_CD_APV2_STOCK' "$ROOT/psxrecomp/runtime/src/cdrom.c"; then
  echo "ERROR: v0.1.8 APv2 stock controller support is missing from cdrom.c."
  apv2_guard_failed=1
fi
if ! grep -q 'PSX_FM_APV2_BYPASS' "$ROOT/psxrecomp/runtime/src/cdrom.c"; then
  echo "ERROR: v0.1.8 signature-gated APv2 bypass support is missing from cdrom.c."
  apv2_guard_failed=1
fi
if ! grep -q '0x1040023Au' "$ROOT/psxrecomp/runtime/src/cdrom.c"; then
  echo "ERROR: v0.1.8 APv2 overlay signature is missing from cdrom.c."
  apv2_guard_failed=1
fi
# crash_trace.c stores the JSON key inside a C string, so the source text is
# escaped as "apv2". Check a stable telemetry field instead of the rendered
# JSON spelling; the original v0.1.8 guard incorrectly failed every clean tree.
if ! grep -q 'apv2_probe_play_acks' "$ROOT/psxrecomp/runtime/src/crash_trace.c"; then
  echo "ERROR: v0.1.8 APv2 telemetry is missing from crash_trace.c."
  apv2_guard_failed=1
fi
if [[ $apv2_guard_failed -ne 0 ]]; then
  echo "Use a clean source tree; do not merge incompatible older runtime files into this build."
  exit 1
fi
echo "Verified v0.1.8 APv2 runtime and telemetry changes."
echo "Verified v0.1.9 intro DMA callback seed guard."
echo "Verified v0.1.10 first-3D handler seed guard."

spu_sweep_guard_failed=0
if [[ ! -f "$ROOT/psxrecomp/runtime/include/spu_sweep.h" || \
      ! -f "$ROOT/psxrecomp/runtime/src/spu_sweep.c" ]]; then
  echo "ERROR: v0.1.11 SPU volume-sweep source files are missing."
  spu_sweep_guard_failed=1
fi
if ! grep -q 'spu_sweep.c' "$ROOT/psxrecomp/runtime/runtime.cmake"; then
  echo "ERROR: v0.1.11 SPU sweep source is not registered in runtime.cmake."
  spu_sweep_guard_failed=1
fi
if ! grep -q 'spu_sweep_clock' "$ROOT/psxrecomp/runtime/src/spu.c"; then
  echo "ERROR: v0.1.11 SPU sweep mixer integration is missing from spu.c."
  spu_sweep_guard_failed=1
fi
if ! grep -q 'sweep_active_channels' "$ROOT/psxrecomp/runtime/src/crash_trace.c"; then
  echo "ERROR: v0.1.11 SPU sweep report telemetry is missing."
  spu_sweep_guard_failed=1
fi
if [[ $spu_sweep_guard_failed -ne 0 ]]; then
  echo "Use a clean source tree or restore the v0.1.11 SPU sweep files."
  exit 1
fi
bash "$ROOT/scripts/test-spu-sweep.sh"
echo "Verified v0.1.11 SPU volume-sweep runtime and unit test."

BUILD="$ROOT/build-windows"
DIST="$ROOT/dist-windows"
BUILD_TYPE="${FM_BUILD_TYPE:-RelWithDebInfo}"

echo
echo "=== Building ForbiddenMemories.exe ($BUILD_TYPE) ==="
cmake -S "$ROOT" -B "$BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DPSX_STATIC_RUNTIME=ON \
  -DPSXRECOMP_SKIP_BIOS_STALE_CHECK=OFF \
  -DFM_DISABLE_AUDIO=OFF
cmake --build "$BUILD" --target psx-runtime -j2

mkdir -p "$DIST/bios" "$DIST/input" "$DIST/disc" "$DIST/saves"
cp -f "$BUILD/ForbiddenMemories.exe" "$DIST/ForbiddenMemories.exe"
for cfg in game.toml game-opengl.toml game-authentic-crt.toml game-enhanced.toml game-hle-diagnostic.toml; do
  cp -f "$ROOT/$cfg" "$DIST/$cfg"
done
cp -f "$ROOT/bios/SCPH1001.BIN" "$DIST/bios/SCPH1001.BIN"
cp -f "$ROOT/input/SLUS_014.11" "$DIST/input/SLUS_014.11"
cp -f "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).cue" "$DIST/disc/Yu-Gi-Oh! Forbidden Memories (USA).cue"
cp -f "$ROOT/disc/Yu-Gi-Oh! Forbidden Memories (USA).bin" "$DIST/disc/Yu-Gi-Oh! Forbidden Memories (USA).bin"
cp -f "$REPORT_DIR/input-verification.json" "$DIST/input-verification.json"
cp -f "$ROOT/README.md" "$DIST/README.md"
cp -f "$ROOT/docs/DEVELOPMENT_HISTORY.md" "$DIST/DEVELOPMENT_HISTORY.md"

# Copy dynamic MinGW dependencies when static linkage leaves any behind.
MINGW_BIN="$(dirname "$(command -v gcc)")"
for dll in SDL2.dll libgcc_s_seh-1.dll libgcc_s_sjlj-1.dll libstdc++-6.dll libwinpthread-1.dll; do
  if [[ -f "$MINGW_BIN/$dll" ]]; then
    cp -f "$MINGW_BIN/$dll" "$DIST/$dll"
  fi
done

cat > "$DIST/RUN_FORBIDDEN_MEMORIES.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_FM_APV2_BYPASS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Primary proof test. Applies the established Forbidden Memories APv2 branch
rem patch only after a three-word live-RAM signature matches exactly.
set "PSX_FM_APV2_BYPASS=1"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_FM_APV2_STOCK.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Faithful controller test for the APv2 sequence: acknowledge AP probe Play,
rem implement Test 19h/04h reset and Test 19h/05h clean-console counters.
set "PSX_CD_APV2_STOCK=1"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_CD_INIT_COMPAT.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Test a shorter libcd Init (0x0A) completion delay. This is an opt-in
rem compatibility experiment; the normal launcher keeps authentic timing.
set "PSX_CD_INIT_DELAY=100000"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD


cat > "$DIST/RUN_CD_READTOC_COMPAT.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Forbidden Memories retries ReadTOC after 53 NTSC frames. The generic
rem 30,000,000-cycle completion lands about 0.15 frame too late. This keeps
rem authentic Init timing and moves only ReadTOC safely inside that deadline.
set "PSX_CD_READTOC_DELAY=28000000"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_CD_STARTUP_COMPAT.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Combined Forbidden Memories CD startup compatibility test.
rem The short Init preserves the 53-frame ReadTOC window seen in v0.1.4;
rem the 28M ReadTOC response then completes inside that window.
set "PSX_CD_INIT_DELAY=100000"
set "PSX_CD_READTOC_DELAY=28000000"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD


cat > "$DIST/RUN_CD_STARTUP_EARLY_TOC_PROOF.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem No-runtime-rebuild timing proof. The v0.1.6 report showed that 28M still
rem reached the game's retry boundary. This gives ReadTOC a wide margin while
rem leaving the legacy pending-command model unchanged.
set "PSX_CD_INIT_DELAY=100000"
set "PSX_CD_READTOC_DELAY=24000000"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_CD_PIPELINE_COMPAT.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
rem Primary v0.1.7 test. Drive work keeps advancing while an earlier CD
rem response waits to be acknowledged; only response presentation is serialized.
set "PSX_CD_INIT_DELAY=100000"
set "PSX_CD_READTOC_DELAY=26000000"
set "PSX_CD_PENDING_TIME_CONTINUES=1"
ForbiddenMemories.exe --game game.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_FM_APV2_BYPASS_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second signature-gated APv2 bypass proof...
set "PSX_FM_APV2_BYPASS=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_FM_APV2_BYPASS='1'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}}"
echo.
echo Preserve psx_last_run_report.json and psx_freeze_heartbeat.json.
pause
CMD

cat > "$DIST/RUN_FM_APV2_STOCK_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second faithful APv2 controller test...
set "PSX_CD_APV2_STOCK=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_CD_APV2_STOCK='1'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}}"
echo.
echo Preserve psx_last_run_report.json and psx_freeze_heartbeat.json.
pause
CMD

cat > "$DIST/RUN_OPENGL_2X.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
ForbiddenMemories.exe --game game-opengl.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_AUTHENTIC_CRT.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
ForbiddenMemories.exe --game game-authentic-crt.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_ENHANCED_PRESENTATION.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
set "PSX_GEOMETRY_CORRECTION=1"
set "PSX_TEXTURE_CORRECTION=1"
ForbiddenMemories.exe --game game-enhanced.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_BIOS_HLE_DIAGNOSTIC.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
ForbiddenMemories.exe --game game-hle-diagnostic.toml --disc "disc\Yu-Gi-Oh! Forbidden Memories (USA).cue"
CMD

cat > "$DIST/RUN_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second faithful-baseline diagnostic run...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}}"
echo.
echo Diagnostic run ended. Preserve any psx_*.json and overlay_captures.json files.
pause
CMD

cat > "$DIST/RUN_CD_INIT_COMPAT_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second CD Init compatibility diagnostic run...
set "PSX_CD_INIT_DELAY=100000"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_CD_INIT_DELAY='100000'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}}"
echo.
echo Diagnostic run ended. Preserve psx_last_run_report.json.
pause
CMD


cat > "$DIST/RUN_CD_READTOC_COMPAT_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second ReadTOC compatibility diagnostic run...
set "PSX_CD_READTOC_DELAY=28000000"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_CD_READTOC_DELAY='28000000'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}}"
echo.
echo Diagnostic run ended. Preserve psx_last_run_report.json.
pause
CMD

cat > "$DIST/RUN_CD_STARTUP_COMPAT_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second combined CD startup compatibility run...
set "PSX_CD_INIT_DELAY=100000"
set "PSX_CD_READTOC_DELAY=28000000"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_CD_INIT_DELAY='100000'; $env:PSX_CD_READTOC_DELAY='28000000'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}"
echo.
echo Diagnostic run ended. Preserve psx_last_run_report.json.
pause
CMD


cat > "$DIST/RUN_CD_PIPELINE_COMPAT_BOUNDED_90_SECONDS.cmd" <<'CMD'
@echo off
cd /d "%~dp0"
echo Starting a 90-second CD pipeline compatibility run...
set "PSX_CD_INIT_DELAY=100000"
set "PSX_CD_READTOC_DELAY=26000000"
set "PSX_CD_PENDING_TIME_CONTINUES=1"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$env:PSX_CD_INIT_DELAY='100000'; $env:PSX_CD_READTOC_DELAY='26000000'; $env:PSX_CD_PENDING_TIME_CONTINUES='1'; $p=Start-Process -FilePath '.\ForbiddenMemories.exe' -ArgumentList '--game','game.toml','--disc','disc\Yu-Gi-Oh! Forbidden Memories (USA).cue' -PassThru; if(-not $p.WaitForExit(90000)){[void]$p.CloseMainWindow(); if(-not $p.WaitForExit(5000)){$p.Kill()}"
echo.
echo Diagnostic run ended. Preserve psx_last_run_report.json.
pause
CMD

cat > "$DIST/README_RUNTIME.txt" <<'TXT'
Yu-Gi-Oh! Forbidden Memories Recompiled - v0.1.11 SPU Volume Sweep

Start with RUN_FM_APV2_BYPASS.cmd.

v0.1.9 adds the runtime-proven MDEC-out DMA completion callback at 0x8005C1F4 to the static game dispatch. This is the first function reached when the intro movie starts after the APv2 bypass.

v0.1.10 adds the runtime-proven 3D scene command-handler entry at 0x80033DB0. The recompiler already emitted the body at 0x80033DB8; this seed preserves the two original setup instructions and falls through into that body.

v0.1.11 implements the PlayStation SPU voice and main-volume sweep envelopes. Forbidden Memories programs these modes for short sound effects; the older runtime misread sweep rate bits as tiny fixed gains. Fixed-volume mixing remains gain-equivalent to v0.1.10. Normal-close reports now include an spu_state section with key-on and sweep telemetry.

The Konami-logo loop is the game's APv2 anti-modchip routine, not a ReadTOC
retry. The routine intentionally seeks to its calculated 24:26:00 AP probe position, sends Play, then performs
Test 19h/04h and 19h/05h. The old runtime returned INT5 for that AP probe Play,
which reset the protection state before the SCEx tests were reached.

RUN_FM_APV2_BYPASS.cmd:
  Exact three-word signature gate at 80168184/88/8C.
  Patches only 1040023A -> 1000023A at 80168188.
  Marks the page dirty so patched live RAM executes through the interpreter.

RUN_FM_APV2_STOCK.cmd:
  Acknowledges the deliberate AP probe Play without host CD-DA.
  Implements Test 19h/04h counter reset and Test 19h/05h clean-console result.

The generic launcher and all strict unknown-dispatch fail-fast behavior remain.
The previous CD timing launchers are retained only as historical diagnostics.
TXT
echo
echo "Build complete: $DIST"
echo "Start with RUN_FM_APV2_BYPASS.cmd and test menu, dialog, 3D navigation, and duel sound effects"
