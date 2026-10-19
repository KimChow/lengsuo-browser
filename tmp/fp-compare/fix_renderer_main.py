"""Add the fingerprint init block that the hunk replay dropped from renderer_main.

Same root cause as the switch block: the newer patch's context no longer matched
after an older patch had already rewritten this function, so the hunk was
reported as failed and skipped.
"""

import pathlib

SRC = pathlib.Path("/home/ubuntu/lengsuo-browser-dev/src")
TARGET = SRC / "content/renderer/renderer_main.cc"

INCLUDE_ANCHOR = "#include \"mojo/public/cpp/bindings/lib/test_random_mojo_delays.h\""
INCLUDE_ADD = (
    '#include "lengsuo/native/canvas_apply.h"\n'
    '#include "lengsuo/native/fp_overrides.h"\n'
)

INIT_ANCHOR = '''  if (command_line.HasSwitch("lengsuo-managed-language")) {'''
INIT_ADD = '''  if (command_line.HasSwitch("lengsuo-managed-fp")) {
    lengsuo::fp::InitFromSwitch(
        command_line.GetSwitchValueASCII("lengsuo-managed-fp"));
  }
'''


def main() -> int:
    text = TARGET.read_text(encoding="utf-8")
    changed = False

    if '#include "lengsuo/native/fp_overrides.h"' not in text:
        if INCLUDE_ANCHOR not in text:
            print("FAILED: include anchor missing")
            return 1
        text = text.replace(INCLUDE_ANCHOR,
                            INCLUDE_ANCHOR + "\n\n" + INCLUDE_ADD.rstrip("\n"), 1)
        changed = True

    if 'lengsuo-managed-fp' not in text:
        if INIT_ANCHOR not in text:
            print("FAILED: init anchor missing")
            return 1
        text = text.replace(INIT_ANCHOR, INIT_ADD + INIT_ANCHOR, 1)
        changed = True

    if changed:
        TARGET.write_bytes(text.encode("utf-8"))

    check = TARGET.read_text(encoding="utf-8")
    ok = ('lengsuo/native/fp_overrides.h' in check
          and 'lengsuo-managed-fp' in check
          and 'lengsuo/native/canvas_apply.h' in check)
    print("ok: renderer_main init restored" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
