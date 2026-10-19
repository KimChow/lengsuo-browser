"""Find where the renderer's own --disable-features value is assembled."""

import pathlib
import subprocess

SRC = pathlib.Path("/home/ubuntu/lengsuo-browser-dev/src")


def grep(pattern: str, *paths: str) -> list[str]:
    result = subprocess.run(
        ["grep", "-rn", pattern, *paths],
        capture_output=True, text=True)
    return [line for line in result.stdout.splitlines() if line]


def main() -> int:
    print("=== who builds renderer --disable-features ===")
    for line in grep("disable-features", str(SRC / "content/browser"))[:12]:
        print(line[:200])

    print("\n=== FeatureList handoff to renderer ===")
    for line in grep("GetFeatureList|feature_list", str(SRC / "content/browser/renderer_host"))[:12]:
        print(line[:200])

    print("\n=== RenderProcessHostImpl command line assembly ===")
    for line in grep("AppendSwitchASCII.*features|kDisableFeatures",
                     str(SRC / "content/browser/renderer_host"),
                     str(SRC / "content/public/common"))[:12]:
        print(line[:200])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
