"""Check whether --disable-features actually reaches the renderer process.

Nested shell quoting kept mangling this, so it lives in a file. Prints the
resolved switch value for the browser and one renderer.
"""

import os
import pathlib
import subprocess
import time

CHROME = ("/home/ubuntu/lengsuo-browser-dev/src/out/lengsuo/chrome")
PAGE = "file:///tmp/fpc/tests/fp-lab.html"
PROFILE = "/tmp/fp-featurecheck"


def switch_values(pid: str, name: str) -> list[str]:
    raw = pathlib.Path("/proc/%s/cmdline" % pid).read_bytes()
    out = []
    parts = raw.split(b"\0")
    for i, part in enumerate(parts):
        text = part.decode(errors="replace")
        if text.startswith("--" + name + "="):
            out.append(text.split("=", 1)[1])
        elif text == "--" + name and i + 1 < len(parts):
            out.append(parts[i + 1].decode(errors="replace"))
    return out


def pids(match: str) -> list[str]:
    result = subprocess.run(["pgrep", "-f", match], capture_output=True, text=True)
    return [p for p in result.stdout.split() if p]


def main() -> int:
    subprocess.run(["rm", "-rf", PROFILE], check=False)
    env = dict(os.environ, LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
    proc = subprocess.Popen(
        [CHROME, "--headless=new", "--disable-gpu", "--no-sandbox",
         "--user-data-dir=" + PROFILE, PAGE],
        env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(8)

    browser = pids("fp-featurecheck")
    renderer = pids("type=renderer")
    print("browser pids:", browser[:2], "renderer pids:", renderer[:2])
    if browser:
        print("browser disable-features:", switch_values(browser[0], "disable-features"))
    if renderer:
        print("renderer disable-features:", switch_values(renderer[0], "disable-features"))

    proc.terminate()
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.kill()
    for pid in browser + renderer:
        subprocess.run(["kill", "-9", pid], capture_output=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
