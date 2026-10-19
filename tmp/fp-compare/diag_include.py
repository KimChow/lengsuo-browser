"""Add a temporary diagnostic log for accept_languages propagation.

Kept as a file (not a heredoc) because quoting an include line through nested
shell + Python layers mangles it. Removed again once the cause is known.
"""

from pathlib import Path

SRC = Path(
    "/home/ubuntu/lengsuo-browser-dev/src/third_party/blink/renderer/core/"
    "exported/web_view_impl.cc")

ANCHOR = '#include "base/debug/crash_logging.h"\n'
LINE = '#include "base/logging.h"\n'


def main() -> int:
    text = SRC.read_text(encoding="utf-8")
    if LINE in text:
        print("logging include already present")
    else:
        assert ANCHOR in text, "include anchor missing"
        text = text.replace(ANCHOR, ANCHOR + LINE, 1)
        print("logging include inserted")
    SRC.write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
