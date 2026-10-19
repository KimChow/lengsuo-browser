"""Deliver the renderer switch and finish the accept-language alignment.

What the investigation established, in order:

1. The renderer does receive `accept_languages=[en-US,en]` (proved by a temporary
   log in web_view_impl.cc).
2. `NavigatorLanguage::EnsureUpdatedLanguage` parses both entries, then truncates
   to one because `kReduceAcceptLanguage` is enabled (proved by a temporary log
   there).
3. Chromium 155 defaults that feature on for this build, while Chrome 144 has it
   off - that is the whole difference.
4. Appending `--disable-features` in the browser process does *not* reach the
   renderer: the child process does not re-init its feature list from its own
   command line.
5. Blink provides a dedicated switch for exactly this,
   `blink::switches::kDisableReduceAcceptLanguage`
   ("--disable-reduce-accept-language"), read straight from the renderer's own
   command line - but it is missing from the renderer switch allow-list.

So the fix is two small pieces: allow-list the switch, and append it at startup
next to the feature default. Both are needed; either alone is inert.
"""

from pathlib import Path

SRC = Path("/home/ubuntu/lengsuo-browser-dev/src")

HOST = SRC / "content/browser/renderer_host/render_process_host_impl.cc"
MAIN = SRC / "chrome/browser/chrome_browser_main.cc"

# --- 1. Allow-list the renderer switch -------------------------------------
HOST_OLD = """      switches::kDisablePresentationAPI,
"""
HOST_NEW = """      switches::kDisablePresentationAPI,
      // Blink reads this from the renderer's own command line to keep
      // navigator.languages at full length. Without it in this list the browser
      // cannot relay the flag, and the renderer silently truncates to one entry.
      blink::switches::kDisableReduceAcceptLanguage,
"""

# --- 2. Drop the temporary diagnostic --------------------------------------
DIAG_OLD = """  lengsuo::ApplyOfficialFeatureDefaults(base::CommandLine::ForCurrentProcess());
  LOG(ERROR) << "LQDIAG applied disable-features=["
             << base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
                    "disable-features") << "]";
"""
DIAG_NEW = """  lengsuo::ApplyOfficialFeatureDefaults(base::CommandLine::ForCurrentProcess());
"""


def patch(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        print("%s: nothing to do (already applied or anchor absent)" % label)
        return
    path.write_bytes(text.replace(old, new, 1).encode("utf-8"))
    print("%s: patched" % label)


def main() -> int:
    patch(HOST, HOST_OLD, HOST_NEW, "renderer allow-list")
    patch(MAIN, DIAG_OLD, DIAG_NEW, "startup diagnostic")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
