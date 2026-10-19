"""Wire the official feature-default alignment into browser startup.

Insertion point matters: `PreEarlyInitialization` runs *before* field trials are
loaded (the comment right below the hook says so), so a switch appended here is
still honoured. Appending later would be silently ignored.

Kept as a script because the patch touches a 9k-line upstream file and has to be
reproducible on the build host, which has no editor session.
"""

from pathlib import Path

SRC = Path(
    "/home/ubuntu/lengsuo-browser-dev/src/chrome/browser/chrome_browser_main.cc")

INCLUDE_ANCHOR = '#include "lengsuo/integration/browser_bootstrap.h"\n'
INCLUDE_ADD = '#include "lengsuo/integration/official_defaults.h"\n'

OLD = """  if (!lengsuo::InitializeBrowserProfile(*base::CommandLine::ForCurrentProcess())) {
    LOG(ERROR) << "Lengsuo profile bootstrap rejected";
    return CHROME_RESULT_CODE_INVALID_CMDLINE_URL;
  }
"""

NEW = """  // Align feature defaults with shipped Chrome. Must happen before field
  // trials are loaded below, or the appended switch is ignored.
  lengsuo::ApplyOfficialFeatureDefaults(base::CommandLine::ForCurrentProcess());

  if (!lengsuo::InitializeBrowserProfile(*base::CommandLine::ForCurrentProcess())) {
    LOG(ERROR) << "Lengsuo profile bootstrap rejected";
    return CHROME_RESULT_CODE_INVALID_CMDLINE_URL;
  }
"""


def main() -> int:
    text = SRC.read_text(encoding="utf-8")

    if INCLUDE_ADD in text:
        print("include already present")
    else:
        assert INCLUDE_ANCHOR in text, "include anchor missing"
        text = text.replace(INCLUDE_ANCHOR, INCLUDE_ANCHOR + INCLUDE_ADD, 1)
        print("include inserted")

    if "ApplyOfficialFeatureDefaults" in text:
        print("call already present")
    else:
        assert text.count(OLD) == 1, "startup anchor not unique"
        text = text.replace(OLD, NEW, 1)
        print("call inserted")

    SRC.write_bytes(text.encode("utf-8"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
