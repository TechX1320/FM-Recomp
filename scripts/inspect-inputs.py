#!/usr/bin/env python3
"""Verify and inventory the exact Forbidden Memories inputs.

No third-party modules are required. The script validates hashes, parses the
PS-X EXE header, reads SYSTEM.CNF and the ISO9660/XA directory directly from the
raw MODE2/2352 BIN, and confirms the standalone executable matches the copy on
disc.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import sys
import zlib
from pathlib import Path
from typing import Any

SECTOR_SIZE = 2352
USER_DATA_OFFSET = 24
USER_DATA_SIZE = 2048

EXPECTED = {
    "bios": {
        "size": 524288,
        "md5": "924e392ed05558ffdb115408c263dccf",
        "sha1": "10155d8d6e6e832d6ea66db9bc098321fb5e8ebf",
        "sha256": "71af94d1e47a68c11e8fdb9f8368040601514a42a5a399cda48c7d3bff1e99d3",
        "crc32": "37157331",
    },
    "exe": {
        "size": 1902592,
        "md5": "dab1b3c9a6b8a56558b5ca8f807339c3",
        "sha1": "84747e64f6da8e764206ec203e489acf8c9dcf7d",
        "sha256": "84a54ed74f3d0edd6d81380839f7e4ef5bfb21ecea18be9a062bd6bfa5a45c88",
        "crc32": "B6176A00",
    },
    "bin": {
        "size": 517872768,
        "md5": "c20bcde742e15c82d09bda1d6c6d62f2",
        "sha1": "d5785a41900a10968d4a28a390666c4b9879b796",
        "sha256": "6e22494a45bf50fa2d239cd3819a57163a5f9b91e0365babc3e101509b5c3a7c",
        "crc32": "73E4C771",
    },
    "cue": {
        "size": 100,
        "md5": "2e71b1f51a2b432525f7568ea54685cc",
        "sha1": "ec2cd0458c22ac4aa7c2ecb4aa81a5ca3c7751ec",
        "sha256": "72467fb3a7ea6053a1925e33b7be716a95f4e99ebb7c66a767b1a551d8bd3dda",
        "crc32": "BCEED6FA",
    },
}


def hashes(path: Path) -> dict[str, Any]:
    hs = {name: hashlib.new(name) for name in ("md5", "sha1", "sha256")}
    crc = 0
    size = 0
    with path.open("rb") as f:
        while True:
            block = f.read(8 * 1024 * 1024)
            if not block:
                break
            size += len(block)
            for h in hs.values():
                h.update(block)
            crc = zlib.crc32(block, crc)
    return {
        "path": str(path),
        "size": size,
        "md5": hs["md5"].hexdigest(),
        "sha1": hs["sha1"].hexdigest(),
        "sha256": hs["sha256"].hexdigest(),
        "crc32": f"{crc & 0xFFFFFFFF:08X}",
    }


def verify(label: str, got: dict[str, Any], expected: dict[str, Any], errors: list[str]) -> None:
    for key, value in expected.items():
        if got.get(key) != value:
            errors.append(f"{label}: {key} mismatch: expected {value}, found {got.get(key)}")


def parse_exe(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if data[:8] != b"PS-X EXE":
        raise ValueError("not a PS-X EXE")
    names = (
        "entry_pc", "initial_gp", "text_address", "text_size",
        "data_address", "data_size", "bss_address", "bss_size",
        "stack_address", "stack_size",
    )
    vals = struct.unpack_from("<10I", data, 0x10)
    out = dict(zip(names, vals))
    out["payload_size"] = len(data) - 0x800
    out["loaded_end_exclusive"] = out["text_address"] + out["text_size"]
    out["header_region_text"] = data[0x4C:0x800].split(b"\0", 1)[0].decode("ascii", "replace")
    return out


def parse_cue(path: Path) -> dict[str, Any]:
    text = path.read_text(encoding="ascii", errors="replace")
    files = re.findall(r'^FILE\s+"([^"]+)"\s+(\S+)', text, flags=re.MULTILINE | re.IGNORECASE)
    tracks = re.findall(r'^\s*TRACK\s+(\d+)\s+(\S+)', text, flags=re.MULTILINE | re.IGNORECASE)
    indexes = re.findall(r'^\s*INDEX\s+(\d+)\s+(\d\d:\d\d:\d\d)', text, flags=re.MULTILINE | re.IGNORECASE)
    return {"text": text, "files": files, "tracks": tracks, "indexes": indexes}


class RawMode2Disc:
    def __init__(self, path: Path):
        self.path = path
        self.f = path.open("rb")

    def close(self) -> None:
        self.f.close()

    def sector(self, lba: int) -> bytes:
        self.f.seek(lba * SECTOR_SIZE)
        data = self.f.read(SECTOR_SIZE)
        if len(data) != SECTOR_SIZE:
            raise EOFError(f"short sector at LBA {lba}")
        return data

    def user2048(self, lba: int) -> bytes:
        sec = self.sector(lba)
        if sec[15] != 2:
            raise ValueError(f"LBA {lba} is not Mode 2")
        return sec[USER_DATA_OFFSET:USER_DATA_OFFSET + USER_DATA_SIZE]


def parse_dir_record(buf: bytes, offset: int) -> tuple[dict[str, Any] | None, int]:
    length = buf[offset]
    if length == 0:
        return None, offset + 1
    rec = buf[offset:offset + length]
    extent = struct.unpack_from("<I", rec, 2)[0]
    size = struct.unpack_from("<I", rec, 10)[0]
    flags = rec[25]
    name_len = rec[32]
    raw_name = rec[33:33 + name_len]
    if raw_name == b"\x00":
        name = "."
    elif raw_name == b"\x01":
        name = ".."
    else:
        name = raw_name.decode("ascii", "replace")
    return {
        "record_length": length,
        "extent_lba": extent,
        "size": size,
        "flags": flags,
        "name": name,
        "is_dir": bool(flags & 2),
    }, offset + length


def read_iso_file(disc: RawMode2Disc, extent: int, size: int) -> bytes:
    blocks = math.ceil(size / USER_DATA_SIZE)
    return b"".join(disc.user2048(extent + i) for i in range(blocks))[:size]


def parse_disc(path: Path, extract_exe_path: Path | None = None) -> dict[str, Any]:
    disc = RawMode2Disc(path)
    try:
        pvd = disc.user2048(16)
        if pvd[0] != 1 or pvd[1:6] != b"CD001" or pvd[6] != 1:
            raise ValueError("missing ISO9660 primary volume descriptor at LBA 16")
        volume_space = struct.unpack_from("<I", pvd, 80)[0]
        block_size = struct.unpack_from("<H", pvd, 128)[0]
        root, _ = parse_dir_record(pvd, 156)
        if not root:
            raise ValueError("missing root directory record")

        entries: list[dict[str, Any]] = []
        seen_dirs: set[tuple[int, int]] = set()

        def walk(prefix: str, extent: int, size: int) -> None:
            key = (extent, size)
            if key in seen_dirs:
                return
            seen_dirs.add(key)
            data = read_iso_file(disc, extent, size)
            off = 0
            while off < len(data):
                if data[off] == 0:
                    off = ((off // USER_DATA_SIZE) + 1) * USER_DATA_SIZE
                    continue
                rec, next_off = parse_dir_record(data, off)
                off = next_off
                if not rec or rec["name"] in (".", ".."):
                    continue
                clean_name = rec["name"].split(";", 1)[0]
                full = f"{prefix}/{clean_name}".lstrip("/")
                rec["path"] = full
                rec["sector_count"] = math.ceil(rec["size"] / USER_DATA_SIZE) if rec["size"] else 0

                form1 = form2 = xa_audio = xa_video = xa_data = xa_realtime = 0
                channels: dict[str, int] = {}
                file_numbers: dict[str, int] = {}
                for i in range(rec["sector_count"]):
                    sec = disc.sector(rec["extent_lba"] + i)
                    if sec[15] != 2:
                        continue
                    file_no, channel, submode = sec[16], sec[17], sec[18]
                    file_numbers[str(file_no)] = file_numbers.get(str(file_no), 0) + 1
                    channels[str(channel)] = channels.get(str(channel), 0) + 1
                    if submode & 0x20:
                        form2 += 1
                    else:
                        form1 += 1
                    xa_audio += bool(submode & 0x04)
                    xa_video += bool(submode & 0x02)
                    xa_data += bool(submode & 0x08)
                    xa_realtime += bool(submode & 0x40)
                rec.update({
                    "form1_sectors": form1,
                    "form2_sectors": form2,
                    "xa_audio_flag_sectors": int(xa_audio),
                    "xa_video_flag_sectors": int(xa_video),
                    "xa_data_flag_sectors": int(xa_data),
                    "xa_realtime_sectors": int(xa_realtime),
                    "xa_channels": channels,
                    "xa_file_numbers": file_numbers,
                })
                entries.append(rec)
                if rec["is_dir"]:
                    walk(full, rec["extent_lba"], rec["size"])

        walk("", root["extent_lba"], root["size"])
        entries.sort(key=lambda e: e["extent_lba"])
        by_path = {e["path"].upper(): e for e in entries}
        system_entry = by_path.get("SYSTEM.CNF")
        exe_entry = by_path.get("SLUS_014.11")
        system_cnf = read_iso_file(disc, system_entry["extent_lba"], system_entry["size"]).decode(
            "ascii", "replace"
        ) if system_entry else ""
        disc_exe = read_iso_file(disc, exe_entry["extent_lba"], exe_entry["size"]) if exe_entry else b""
        if extract_exe_path is not None:
            if not disc_exe:
                raise ValueError("SLUS_014.11 was not found in the disc image")
            extract_exe_path.parent.mkdir(parents=True, exist_ok=True)
            extract_exe_path.write_bytes(disc_exe)
        total_sectors = path.stat().st_size // SECTOR_SIZE
        return {
            "sector_size": SECTOR_SIZE,
            "total_sectors": total_sectors,
            "raw_duration_msf": {
                "minutes": total_sectors // (75 * 60),
                "seconds": (total_sectors % (75 * 60)) // 75,
                "frames": total_sectors % 75,
            },
            "system_id": pvd[8:40].decode("ascii", "replace").rstrip(" "),
            "volume_id": pvd[40:72].decode("ascii", "replace").rstrip(" "),
            "volume_space_sectors": volume_space,
            "logical_block_size": block_size,
            "root_extent_lba": root["extent_lba"],
            "entries": entries,
            "system_cnf": system_cnf,
            "disc_exe_sha256": hashlib.sha256(disc_exe).hexdigest() if disc_exe else None,
            "last_allocated_end_lba": max((e["extent_lba"] + e["sector_count"] for e in entries), default=0),
        }
    finally:
        disc.close()


def hx(value: int) -> str:
    return f"0x{value:08X}"


def markdown_report(result: dict[str, Any]) -> str:
    exe = result["exe_header"]
    disc = result["disc_layout"]
    lines = [
        "# Forbidden Memories Input Verification",
        "",
        f"Status: **{'PASS' if result['ok'] else 'FAIL'}**",
        "",
        "## Identity",
        "",
        "- Game: Yu-Gi-Oh! Forbidden Memories",
        "- Region: North America / NTSC-U",
        "- Serial: SLUS-01411 (`SLUS_014.11`)",
        "- Disc: one `MODE2/2352` data track; no separate CD-DA tracks",
        "",
        "## PS-X EXE",
        "",
        f"- Entry point: `{hx(exe['entry_pc'])}`",
        f"- Load address: `{hx(exe['text_address'])}`",
        f"- Loaded size: `0x{exe['text_size']:X}` ({exe['text_size']} bytes)",
        f"- Loaded range: `{hx(exe['text_address'])}` to `{hx(exe['loaded_end_exclusive'])}` (end exclusive)",
        f"- Initial GP: `{hx(exe['initial_gp'])}`",
        f"- EXE header stack: `{hx(exe['stack_address'])}`",
        f"- Declared data segment: `{hx(exe['data_address'])}` / `0x{exe['data_size']:X}` bytes",
        f"- Declared BSS segment: `{hx(exe['bss_address'])}` / `0x{exe['bss_size']:X}` bytes",
        "",
        "## Disc Boot Configuration",
        "",
        "```text",
        disc["system_cnf"].rstrip(),
        "```",
        "",
        "The `SYSTEM.CNF` stack (`0x801FFF00`) is 0xF0 bytes below the EXE header stack (`0x801FFFF0`). Both are recorded; neither is silently substituted.",
        "",
        "## Disc Layout",
        "",
        f"Raw sectors: {disc['total_sectors']} ({disc['raw_duration_msf']['minutes']:02d}:{disc['raw_duration_msf']['seconds']:02d}:{disc['raw_duration_msf']['frames']:02d})",
        "",
        "| LBA | Size | Sectors | Path |",
        "|---:|---:|---:|---|",
    ]
    for e in disc["entries"]:
        lines.append(f"| {e['extent_lba']} | {e['size']} | {e['sector_count']} | `{e['path']}` |")
    lines += ["", "## Hashes", ""]
    for label in ("bios", "exe", "cue", "bin"):
        h = result["hashes"][label]
        lines += [
            f"### {label.upper()}",
            "",
            f"- Size: `{h['size']}`",
            f"- MD5: `{h['md5']}`",
            f"- SHA-1: `{h['sha1']}`",
            f"- SHA-256: `{h['sha256']}`",
            f"- CRC32: `{h['crc32']}`",
            "",
        ]
    if result["errors"]:
        lines += ["## Errors", ""] + [f"- {e}" for e in result["errors"]] + [""]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bios", required=True, type=Path)
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--extract-exe", type=Path)
    ap.add_argument("--cue", required=True, type=Path)
    ap.add_argument("--bin", required=True, type=Path)
    ap.add_argument("--json", type=Path)
    ap.add_argument("--markdown", type=Path)
    args = ap.parse_args()

    for p in (args.bios, args.cue, args.bin):
        if not p.is_file():
            print(f"ERROR: missing file: {p}", file=sys.stderr)
            return 2

    if args.exe is not None and args.extract_exe is not None:
        print("ERROR: use either --exe or --extract-exe, not both", file=sys.stderr)
        return 2
    if args.exe is None and args.extract_exe is None:
        print("ERROR: one of --exe or --extract-exe is required", file=sys.stderr)
        return 2

    # Parse the disc first so a clean build can extract SLUS_014.11 directly
    # from the user's verified BIN instead of requiring a separate executable.
    try:
        disc = parse_disc(args.bin, args.extract_exe)
    except (OSError, ValueError, EOFError, struct.error) as exc:
        print(f"ERROR: could not parse supported disc image: {exc}", file=sys.stderr)
        return 1
    exe_path = args.exe if args.exe is not None else args.extract_exe
    assert exe_path is not None
    if not exe_path.is_file():
        print(f"ERROR: missing/extraction failed for executable: {exe_path}", file=sys.stderr)
        return 2

    errors: list[str] = []
    found_hashes = {
        "bios": hashes(args.bios),
        "exe": hashes(exe_path),
        "cue": hashes(args.cue),
        "bin": hashes(args.bin),
    }
    for label in ("bios", "exe", "cue", "bin"):
        verify(label, found_hashes[label], EXPECTED[label], errors)

    # The completed Street Sk8er 2 reference used OpenBIOS CRC32 D1AC0608
    # together with a project-specific generated BIOS image and interpreter
    # fallback. The clean Forbidden Memories baseline has not validated that
    # compatibility path, so fail clearly instead of compiling a mismatched
    # ROM with Sony SCPH-1001-derived seed addresses.
    if found_hashes["bios"]["crc32"] == "D1AC0608":
        errors.append(
            "bios: detected OpenBIOS CRC32 D1AC0608. This build "
            "requires Sony SCPH-1001 CRC32 37157331; OpenBIOS support is a "
            "separate compatibility task and is not yet validated."
        )

    exe_header = parse_exe(exe_path)
    cue = parse_cue(args.cue)

    expected_exe = {
        "entry_pc": 0x800129D8,
        "initial_gp": 0,
        "text_address": 0x80010000,
        "text_size": 0x001D0000,
        "data_address": 0,
        "data_size": 0,
        "bss_address": 0,
        "bss_size": 0,
        "stack_address": 0x801FFFF0,
        "payload_size": 0x001D0000,
        "loaded_end_exclusive": 0x801E0000,
    }
    for key, value in expected_exe.items():
        if exe_header.get(key) != value:
            errors.append(f"EXE header {key}: expected {value:#x}, found {exe_header.get(key):#x}")

    if cue["tracks"] != [("01", "MODE2/2352")]:
        errors.append(f"CUE track layout mismatch: {cue['tracks']}")
    if cue["indexes"] != [("01", "00:00:00")]:
        errors.append(f"CUE index layout mismatch: {cue['indexes']}")
    if disc["total_sectors"] != 220184 or disc["volume_space_sectors"] != 220184:
        errors.append("disc sector count/ISO volume size mismatch")
    if disc["disc_exe_sha256"] != found_hashes["exe"]["sha256"]:
        errors.append("standalone SLUS_014.11 does not match the executable inside the BIN")

    cnf_norm = disc["system_cnf"].replace("\r\n", "\n").replace("\r", "\n")
    for required in (
        "BOOT = cdrom:\\SLUS_014.11;1",
        "TCB = 4",
        "EVENT = 16",
        "STACK = 801FFF00",
    ):
        if required not in cnf_norm:
            errors.append(f"SYSTEM.CNF missing expected line: {required}")

    result = {
        "ok": not errors,
        "errors": errors,
        "hashes": found_hashes,
        "exe_header": exe_header,
        "cue": cue,
        "disc_layout": disc,
    }

    text_json = json.dumps(result, indent=2)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(text_json + "\n", encoding="utf-8")
    if args.markdown:
        args.markdown.parent.mkdir(parents=True, exist_ok=True)
        args.markdown.write_text(markdown_report(result) + "\n", encoding="utf-8")

    print("PASS" if result["ok"] else "FAIL")
    print(f"BIN SHA-1: {found_hashes['bin']['sha1']}")
    print(f"EXE entry: {hx(exe_header['entry_pc'])}")
    print(f"EXE load:  {hx(exe_header['text_address'])} - {hx(exe_header['loaded_end_exclusive'])} (exclusive)")
    print(f"Disc:      {disc['total_sectors']} sectors, {len(disc['entries'])} filesystem entries")
    if errors:
        for e in errors:
            print(f"ERROR: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
