"""Targeted scan of chrome.dll for the Reference Mojo interface surface.

Stage 1 of the previous scan showed the interface names live in Chromium's
constant string pool, so the bytes next to them are unrelated. This scan
switches to anchors that actually co-locate with a method's name:

  A. every string that looks like a .mojom source path -> the mojom file
     inventory of third_party/superBrowser/src;
  B. the already-known renderer injection entry points
     (AddCanvasFingerprint / AddWebGLFingerprint / getCanvasFingerprintingData /
     getWebGLFingerprintingData) -> their neighbours, which are the sibling
     methods of the same interface;
  C. the "superBrowser.mojom." prefix -> the exact interface-name strings.

Output is a plain text report so it can be pasted into the reference doc.
"""

from __future__ import annotations

import argparse
import collections
import re
import struct
import sys

KNOWN = [
    "AddCanvasFingerprint",
    "AddWebGLFingerprint",
    "getCanvasFingerprintingData",
    "getWebGLFingerprintingData",
]

MOJOM_PATH = re.compile(r"^[A-Za-z0-9_./\\-]{4,200}\.mojom$")
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]{2,47}$")
NOISE_PREFIX = ("k", "kColor", "http", "chrome", "Chrome", "Windows", "Mozilla")


def read_sections(blob: bytes):
    pe_off = struct.unpack_from("<I", blob, 0x3C)[0]
    coff = pe_off + 4
    num_sections = struct.unpack_from("<H", blob, coff + 2)[0]
    opt_size = struct.unpack_from("<H", blob, coff + 16)[0]
    sec_off = coff + 20 + opt_size
    out = []
    for i in range(num_sections):
        base = sec_off + i * 40
        name = blob[base : base + 8].rstrip(b"\x00").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", blob, base + 8)
        out.append((name, rawptr, rawsize, vaddr))
    return out


def section_of(sections, offset: int) -> str:
    for name, rawptr, rawsize, _ in sections:
        if rawptr <= offset < rawptr + rawsize:
            return name
    return "<headers>"


def strings(blob: bytes, min_len: int = 4):
    """Yield (offset, text) for maximal printable ASCII runs."""
    start = None
    for i, byte in enumerate(blob):
        if 0x20 <= byte < 0x7F:
            if start is None:
                start = i
        else:
            if start is not None and i - start >= min_len:
                yield start, buf_text(blob, start, i)
            start = None
    if start is not None and len(blob) - start >= min_len:
        yield start, buf_text(blob, start, len(blob))


def buf_text(blob: bytes, start: int, end: int) -> str:
    return blob[start:end].decode("ascii", "replace")


def plausible_method(name: str) -> bool:
    if not IDENT.match(name):
        return False
    if name.isupper():
        return False
    if name.endswith(".mojom"):
        return False
    if "::" in name or "/" in name or "." in name:
        return False
    if name.startswith(("k", "K")) and name[1:2].isupper():
        return False
    if not (name[0].islower() or name[0].isupper()):
        return False
    # keep only lowerCamelCase or PascalCase
    if not (name[0].islower() and any(c.isupper() for c in name[1:])):
        if not (name[0].isupper() and any(c.islower() for c in name[1:])):
            return False
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("dll")
    ap.add_argument("--window", type=int, default=1536)
    args = ap.parse_args()

    with open(args.dll, "rb") as fh:
        blob = fh.read()
    sections = read_sections(blob)
    print(f"file: {args.dll}  ({len(blob)/1e6:.1f} MB)\n")

    # --- A. .mojom path inventory -------------------------------------------
    print("=== A. .mojom source paths in the binary ===")
    mojom_paths: collections.Counter[str] = collections.Counter()
    all_strings = list(strings(blob, 4))
    for off, text in all_strings:
        if MOJOM_PATH.match(text):
            mojom_paths[text] += 1
    for path, count in sorted(mojom_paths.items()):
        if "super" in path.lower() or "sbipc" in path.lower() or "superBrowser" in path:
            print(f"  {count:3d}  {path}")
    print(f"  (total .mojom paths: {len(mojom_paths)})")

    # --- B. known entry points and their neighbours -------------------------
    print("\n=== B. neighbourhoods of the known renderer entry points ===")
    for anchor in KNOWN:
        needle = anchor.encode("ascii")
        hits = []
        pos = 0
        while True:
            pos = blob.find(needle, pos)
            if pos < 0:
                break
            hits.append(pos)
            pos += 1
        print(f"\n  -- {anchor}: {len(hits)} hits")
        for hit in hits[:4]:
            lo = max(0, hit - args.window)
            hi = min(len(blob), hit + args.window)
            cands: collections.Counter[str] = collections.Counter()
            for off, text in strings(blob[lo:hi], 4):
                if plausible_method(text):
                    cands[text] += 1
            print(f"     hit @0x{hit:08x} ({section_of(sections, hit)})")
            for name, count in cands.most_common(25):
                print(f"        {count:3d}  {name}")

    # --- C. the exact interface name strings --------------------------------
    print("\n=== C. superBrowser.mojom.* interface strings ===")
    prefix = b"superBrowser.mojom."
    pos = 0
    seen: collections.Counter[str] = collections.Counter()
    while True:
        pos = blob.find(prefix, pos)
        if pos < 0:
            break
        end = pos + len(prefix)
        while end < len(blob) and 0x20 <= blob[end] < 0x7F:
            end += 1
        seen[blob[pos:end].decode("ascii")] += 1
        pos += 1
    for name, count in sorted(seen.items()):
        print(f"  {count:3d}  {name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
