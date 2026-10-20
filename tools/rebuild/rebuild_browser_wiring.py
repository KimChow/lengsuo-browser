"""Rebuild the browser<->renderer handshake as one properly chained patch.

The patch series in ``patches/`` is an *evolving snapshot*: several patches were
captured from a tree where earlier patches were already applied, so their hunks
carry that earlier content a second time. Replaying the series with
``patch --fuzz=3`` therefore double-applies shared content and silently drops
the hunks whose context no longer matches.

The three upstream files that carry the browser<->renderer handshake overlap
the most, and they are the ones that break:

* ``chrome/browser/chrome_browser_main.cc``            0001 + 0013 + 0015
* ``chrome/browser/chrome_content_browser_client.cc``   0002/0006/0007/0008/0014/0015
* ``content/renderer/renderer_main.cc``                 0007/0008/0014/0015

The damage is not cosmetic:

* ``ProfileBootstrapState::Initialize`` refuses a second call, so the
  duplicated ``InitializeBrowserProfile`` in chrome_browser_main.cc makes the
  browser abort at startup;
* the font passthrough lands inside the ``IS_MAC`` branch of the browser
  client, referring to a ``browser_cmd`` that was never declared there;
* the whole managed-fp passthrough block is lost, so a profile-driven
  fingerprint never reaches the renderer at all.

This script reconstructs the intended final content of exactly those three
files from the pristine upstream blob plus the semantic blocks the patches
carry, and writes the result as one patch that applies to pristine. The three
files are then removed from every other patch, so each upstream file has a
single owner and the series replays without fuzz.

Every edit is an exact-string replacement against a known anchor, so a drifted
anchor fails loudly instead of producing a half-patched tree.

Usage::

    python3 tools/rebuild/rebuild_browser_wiring.py --source <chromium src> \\
        --repository <repo> --out patches/0016-browser-wiring.patch [--strip]
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

BROWSER_MAIN = "chrome/browser/chrome_browser_main.cc"
BROWSER_CLIENT = "chrome/browser/chrome_content_browser_client.cc"
RENDERER_MAIN = "content/renderer/renderer_main.cc"

OWNED_BY_NEW_PATCH = (BROWSER_MAIN, BROWSER_CLIENT, RENDERER_MAIN)


# --- patch reading -------------------------------------------------------

class Hunk:
    """One unified-diff hunk, kept in standard diff order."""

    def __init__(self, target: str, header: str) -> None:
        self.target = target
        self.header = header
        self.old: list[str] = []
        self.new: list[str] = []
        self.added: list[str] = []

    def context(self, line: str) -> None:
        self.old.append(line)
        self.new.append(line)

    def remove(self, line: str) -> None:
        self.old.append(line)

    def add(self, line: str) -> None:
        self.new.append(line)
        self.added.append(line)


def read_hunks(patch_path: pathlib.Path) -> list[Hunk]:
    hunks: list[Hunk] = []
    target: str | None = None
    current: Hunk | None = None
    for raw in patch_path.read_text(
            encoding="utf-8", errors="surrogateescape").splitlines():
        if raw.startswith("diff --git "):
            # A new file starts; the previous hunk is complete.
            current = None
            continue
        if raw.startswith("--- "):
            current = None
            continue
        if raw.startswith("+++ b/"):
            target = raw[6:].strip()
        elif raw.startswith("@@"):
            current = Hunk(target or "", raw)
            hunks.append(current)
        elif current is None:
            continue
        elif raw.startswith("-"):
            current.remove(raw[1:])
        elif raw.startswith("+"):
            current.add(raw[1:])
        elif raw.startswith(" "):
            current.context(raw[1:])
        elif raw.startswith("\\"):
            continue  # "\ No newline at end of file"
    return hunks


def pick(patches: pathlib.Path, name: str, target: str, marker: str,
         context: str | None = None) -> Hunk:
    """The one hunk of ``name`` touching ``target`` whose added lines mention
    ``marker`` and whose header names ``context``."""
    for hunk in read_hunks(patches / name):
        if hunk.target != target:
            continue
        if context is not None and context not in hunk.header:
            continue
        if not any(marker in line for line in hunk.added):
            continue
        return hunk
    raise SystemExit(f"no hunk {marker!r} ({context}) for {target} in {name}")


def added_block(hunk: Hunk, start: str, last: str) -> list[str]:
    """The slice of a hunk's added lines from ``start`` through ``last``."""
    first = next(i for i, line in enumerate(hunk.added) if start in line)
    final = next(i for i, line in enumerate(hunk.added)
                  if i >= first and last in line)
    return hunk.added[first:final + 1]


# --- application ----------------------------------------------------------

def apply_hunk(text: str, hunk: Hunk, what: str) -> str:
    old = "\n".join(hunk.old)
    new = "\n".join(hunk.new)
    if text.count(old) != 1:
        raise SystemExit(
            f"hunk for {what} matched {text.count(old)} times:\n{old}")
    return text.replace(old, new, 1)


def splice(text: str, anchor: str, replacement: str, what: str) -> str:
    if text.count(anchor) != 1:
        raise SystemExit(
            f"anchor for {what} matched {text.count(anchor)} times:\n{anchor}")
    return text.replace(anchor, replacement, 1)


def upstream(source: pathlib.Path, path: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(source), "show", f"HEAD:{path}"],
        capture_output=True, text=True, check=True)
    return result.stdout


def build_browser_main(source: pathlib.Path, patches: pathlib.Path) -> str:
    text = upstream(source, BROWSER_MAIN)
    # 0013 is based on the pristine blob and carries both the bootstrap and the
    # official-defaults additions, so it alone owns the file.
    body_added: list[str] = []
    for hunk in read_hunks(patches / "0013-official-feature-defaults.patch"):
        if hunk.target == BROWSER_MAIN:
            text = apply_hunk(text, hunk, "browser_main 0013")
            if any("InitializeBrowserProfile" in line for line in hunk.added):
                body_added = hunk.added
    l3 = added_block(
        pick(patches, "0015-l3-storage.patch", BROWSER_MAIN, "InitializeBrowserL3"),
        "// Storage-layer pins (L3).", "}")
    # Anchor on the block 0013 just added: its exact text is known, so the
    # insertion point cannot drift.
    anchor = "\n".join(body_added) + "\n"
    text = splice(text, anchor, anchor + "\n".join(l3) + "\n",
                  "browser_main L3 bootstrap")
    return text


def build_browser_client(source: pathlib.Path, patches: pathlib.Path) -> str:
    text = upstream(source, BROWSER_CLIENT)
    # 0002 owns the include and the touch preferences; 0006 owns the user agent;
    # 0008 owns the renderer passthrough, the locale and the language blocks.
    for name in ("0002-bootstrap-chrome_content_browser_client.patch",
                 "0006-browser-ua-catalog.patch"):
        for hunk in read_hunks(patches / name):
            if hunk.target == BROWSER_CLIENT:
                text = apply_hunk(text, hunk, f"browser_client {name}")
    for hunk in read_hunks(patches / "0008-fp-overrides.patch"):
        if hunk.target != BROWSER_CLIENT:
            continue
        if any("browser_bootstrap.h" in line or
               "ApplyProfileTouchPreferences" in line or
               "BrowserUa()" in line for line in hunk.added):
            continue  # already owned by 0002 / 0006
        text = apply_hunk(text, hunk, "browser_client 0008")
    # 0014 adds the font passthrough; it is based on 0008's output.
    for hunk in read_hunks(patches / "0014-font-metrics.patch"):
        if hunk.target == BROWSER_CLIENT:
            text = apply_hunk(text, hunk, "browser_client 0014")
    l3 = added_block(
        pick(patches, "0015-l3-storage.patch", BROWSER_CLIENT, "managed-l3"),
        "// Storage-layer pins (L3).", "    }")
    text = splice(
        text,
        "    }\n  }\n  crash_keys::AppendStringAnnotationsCommandLineSwitch(command_line);\n",
        "    }\n" + "\n".join(l3) + "\n  }\n"
        + "  crash_keys::AppendStringAnnotationsCommandLineSwitch(command_line);\n",
        "browser_client L3 forwarding")
    return text


def build_renderer_main(source: pathlib.Path, patches: pathlib.Path) -> str:
    text = upstream(source, RENDERER_MAIN)
    for hunk in read_hunks(patches / "0008-fp-overrides.patch"):
        if hunk.target == RENDERER_MAIN:
            text = apply_hunk(text, hunk, "renderer_main 0008")
    for hunk in read_hunks(patches / "0014-font-metrics.patch"):
        if hunk.target == RENDERER_MAIN:
            text = apply_hunk(text, hunk, "renderer_main 0014")
    l3 = added_block(
        pick(patches, "0015-l3-storage.patch", RENDERER_MAIN, "managed-l3"),
        "// Storage-layer pins (L3). Transitional transport:", "    }")
    # The block closes with "  }"; keep it so the inserted code is balanced.
    l3.append("  }")
    # The include hunk is applied as a hunk: its anchor is chosen so it does
    # not collide with the lengsuo includes 0008 adds.
    include_hunk = pick(patches, "0015-l3-storage.patch", RENDERER_MAIN,
                        "l3_runtime.h")
    text = apply_hunk(text, include_hunk, "renderer_main 0015 includes")
    # The L3 block goes before the local "command_line" alias: patches 0007 and
    # 0008 both insert their own blocks right after that alias, and a later
    # block anchored at the same place is skipped by GNU patch.
    text = splice(
        text,
        "  const base::CommandLine& command_line = *parameters.command_line;\n",
        "\n".join(l3) + "\n"
        + "  const base::CommandLine& command_line = *parameters.command_line;\n",
        "renderer_main L3 bootstrap")
    return text


# --- patch rewriting -----------------------------------------------------

def strip_files(patch_path: pathlib.Path, targets: tuple[str, ...]) -> str:
    """Remove every hunk that touches one of ``targets``."""
    lines = patch_path.read_text(encoding="utf-8",
                                 errors="surrogateescape").splitlines(True)
    out: list[str] = []
    dropping = False
    for line in lines:
        if line.startswith("diff --git "):
            dropping = False
        elif line.startswith("+++ b/"):
            dropping = line[6:].strip() in targets
        if dropping:
            continue
        out.append(line)
    return "".join(out)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=pathlib.Path, required=True)
    parser.add_argument("--repository", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--strip", action="store_true",
                        help="remove the three files from every other patch")
    args = parser.parse_args(argv)

    patches = args.repository / "patches"
    targets = {
        BROWSER_MAIN: build_browser_main(args.source, patches),
        BROWSER_CLIENT: build_browser_client(args.source, patches),
        RENDERER_MAIN: build_renderer_main(args.source, patches),
    }

    work = args.out.parent / ".rebuild-wiring"
    subprocess.run(["rm", "-rf", str(work)], check=True)
    work.mkdir(parents=True)
    for target in targets:
        path = work / target
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(upstream(args.source, target), encoding="utf-8")
    subprocess.run(["git", "init", "-q"], cwd=work, check=True)
    subprocess.run(["git", "config", "core.autocrlf", "false"], cwd=work, check=True)
    subprocess.run(["git", "add", "-A", "."], cwd=work, check=True)
    subprocess.run(["git", "-c", "user.email=w@l", "-c", "user.name=w",
                    "commit", "-qm", "base"], cwd=work, check=True)
    for target, content in targets.items():
        (work / target).write_text(content, encoding="utf-8")
    patch = subprocess.run(["git", "diff", "--no-color"], cwd=work,
                           capture_output=True, text=True, check=True).stdout
    args.out.write_text(patch, encoding="utf-8")
    print(f"wrote {args.out} ({len(patch.splitlines())} lines)")

    if args.strip:
        for other in sorted(patches.glob("*.patch")):
            if other.name == args.out.name:
                continue
            stripped = strip_files(other, OWNED_BY_NEW_PATCH)
            if stripped != other.read_text(encoding="utf-8",
                                          errors="surrogateescape"):
                other.write_text(stripped, encoding="utf-8")
                print(f"stripped {other.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
