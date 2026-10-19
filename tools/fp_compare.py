"""Diff two fingerprint probe captures, field by field.

The probe (``tests/fp-probe.html``) is deliberately opinion-free: it only
records what the browser reports. Judgement lives here, and it is expressed as
*why* a field may legitimately differ rather than as a pass/fail score:

``version``     version strings that should be identical for the same Chrome
                milestone, on any platform (UA-CH brand list, full version).
``semantics``   WebGL/API values whose *shape* must hold everywhere (a masked
                vendor is ``WebKit``, state parameters are untouched).
``platform``    values that legitimately follow the OS, GPU or display.
``noise``       per-run or environment jitter (fonts installed, canvas hash,
                audio sum, window size, timezone).

Usage::

    python tools/fp_compare.py --a official.json --b ours.json
    python tools/fp_compare.py --a official.json --b ours.json --only version
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

# Dotted-path prefixes and the reason a difference there is (or is not) benign.
CATEGORIES: list[tuple[str, str, str]] = [
    ("version", "basic.userAgent", "UA 版本段应与同里程碑的官方 Chrome 一致"),
    ("version", "basic.appVersion", "同 UA"),
    ("version", "uaDataLow", "低熵 brands 必须与官方同构（含 GREASE 项顺序）"),
    ("version", "uaDataHigh", "高熵 fullVersionList / uaFullVersion 必须一致"),
    ("semantics", "basic.webdriver", "自动化标志必须为 false"),
    ("semantics", "basic.vendor", "navigator.vendor 必须为 Google Inc."),
    ("semantics", "basic.productSub", "必须是 20030107"),
    ("semantics", "webgl1.vendor", "masked VENDOR 必须是 WebKit"),
    ("semantics", "webgl1.renderer", "masked RENDERER 必须是 WebKit WebGL"),
    ("semantics", "webgl1.activeTexture", "状态参数必须透传，不得偏置"),
    ("semantics", "webgl1.viewport", "状态参数必须透传，不得偏置"),
    ("semantics", "webgl2.activeTexture", "同上"),
    ("semantics", "webgl2.viewport", "同上"),
    ("semantics", "chromeObject", "window.chrome 的键集合必须一致"),
    ("semantics", "plugins", "插件列表应一致"),
    ("semantics", "mimeTypes", "MIME 列表应一致"),
    ("platform", "basic.platform", "navigator.platform 跟随操作系统"),
    ("platform", "basic.language", "语言跟随系统/画像"),
    ("platform", "basic.languages", "语言列表跟随系统/画像"),
    ("platform", "basic.hardwareConcurrency", "跟随 CPU/画像"),
    ("platform", "basic.deviceMemory", "跟随内存/画像"),
    ("platform", "basic.maxTouchPoints", "跟随设备形态"),
    ("platform", "basic.doNotTrack", "跟随用户设置"),
    ("platform", "uaDataLow.platform", "UA-CH platform 跟随操作系统"),
    ("platform", "uaDataHigh.architecture", "跟随 CPU 架构"),
    ("platform", "uaDataHigh.bitness", "跟随 CPU 位宽"),
    ("platform", "uaDataHigh.platformVersion", "跟随操作系统版本"),
    ("platform", "uaDataHigh.wow64", "跟随进程位宽"),
    ("platform", "webgl1.unmaskedVendor", "跟随 GPU 驱动"),
    ("platform", "webgl1.unmaskedRenderer", "跟随 GPU 驱动"),
    ("platform", "webgl2.unmaskedVendor", "跟随 GPU 驱动"),
    ("platform", "webgl2.unmaskedRenderer", "跟随 GPU 驱动"),
    ("platform", "webgl1.maxTextureSize", "跟随 GPU 能力"),
    ("platform", "webgl1.maxCubeMapTextureSize", "跟随 GPU 能力"),
    ("platform", "webgl1.maxRenderbufferSize", "跟随 GPU 能力"),
    ("platform", "webgl1.maxVertexUniformVectors", "跟随 GPU 能力"),
    ("platform", "webgl1.maxFragmentUniformVectors", "跟随 GPU 能力"),
    ("platform", "webgl1.maxVaryingVectors", "跟随 GPU 能力"),
    ("platform", "webgl1.maxViewportDims", "跟随 GPU 能力"),
    ("platform", "webgl1.aliasedPointSizeRange", "跟随 GPU 能力"),
    ("platform", "webgl1.extensions", "扩展集合跟随 GPU/驱动"),
    ("platform", "webgl2.extensions", "扩展集合跟随 GPU/驱动"),
    ("platform", "display", "跟随显示器与窗口"),
    ("noise", "fonts", "跟随系统已安装字体"),
    ("noise", "canvasHash", "跟随字体/光栅化"),
    ("noise", "audioSum", "跟随音频栈"),
    ("noise", "clientRects", "跟随布局与缩放"),
    ("noise", "intl", "跟随时区与 locale"),
]

NOISE_PREFIXES = ("display", "intl", "fonts", "canvasHash", "audioSum",
                  "clientRects")


def flatten(node, prefix: str = "") -> dict[str, object]:
    """Flatten nested dicts into dotted paths; lists stay as whole values."""
    out: dict[str, object] = {}
    if isinstance(node, dict):
        for key, value in node.items():
            path = "%s.%s" % (prefix, key) if prefix else str(key)
            if isinstance(value, dict):
                out.update(flatten(value, path))
            else:
                out[path] = value
    else:
        out[prefix] = node
    return out


def categorise(path: str) -> str:
    for category, prefix, _reason in CATEGORIES:
        if path == prefix or path.startswith(prefix + "."):
            return category
    for prefix in NOISE_PREFIXES:
        if path == prefix or path.startswith(prefix + "."):
            return "noise"
    return "unclassified"


def reason_for(path: str) -> str:
    best = ""
    for _category, prefix, reason in CATEGORIES:
        if path == prefix or path.startswith(prefix + "."):
            if len(prefix) > len(best):
                best = reason
    return best


def load(path: str) -> dict:
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    return data.get("value", data)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--a", required=True, help="reference capture (official)")
    parser.add_argument("--b", required=True, help="capture under test (ours)")
    parser.add_argument("--only", default=None,
                        help="print only this category (version/semantics/...)")
    parser.add_argument("--all", action="store_true",
                        help="include noise and platform rows")
    args = parser.parse_args(argv)

    left = flatten(load(args.a))
    right = flatten(load(args.b))
    keys = sorted(set(left) | set(right))

    rows = []
    for key in keys:
        lv = left.get(key, "<absent>")
        rv = right.get(key, "<absent>")
        if lv == rv:
            continue
        rows.append((categorise(key), key, lv, rv))

    order = {"version": 0, "semantics": 1, "unclassified": 2,
             "platform": 3, "noise": 4}
    rows.sort(key=lambda r: (order.get(r[0], 9), r[1]))

    shown = 0
    for category, key, lv, rv in rows:
        if args.only and category != args.only:
            continue
        if not args.all and not args.only and category in ("noise",):
            continue
        shown += 1
        print("[%s] %s" % (category, key))
        print("    A: %s" % json.dumps(lv, ensure_ascii=False))
        print("    B: %s" % json.dumps(rv, ensure_ascii=False))
        note = reason_for(key)
        if note:
            print("    说明: %s" % note)

    counts: dict[str, int] = {}
    for category, _k, _l, _r in rows:
        counts[category] = counts.get(category, 0) + 1
    summary = ", ".join("%s=%d" % (k, v) for k, v in sorted(counts.items()))
    print("\n差异 %d 项（%s）" % (len(rows), summary or "无"))
    if not args.all and not args.only:
        print("已隐藏 noise 类；用 --all 或 --only noise 查看。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
