"""Produce a signed L3W1 document for manual acceptance (1.47 or local).

Usage on the build machine:
    python3 tools/rebuild/make_l3_doc.py <output-path> [origin] [key] [value]

The document is signed with the same MD5 scheme cache_record uses, so the
browser accepts it through the normal signature gate. The origin/key/value
defaults reproduce the pinduoduo reference scenario from the decrypted Reference
cache, so a manual check can compare against the real thing.

The document is written in plain text on purpose: it is a *managed file*, and
the browser reads it directly. The command line only ever carries the path.
"""

import hashlib
import pathlib
import sys


def md5_hex(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def build(origin: str, key: str, value: str, timestamp: int) -> str:
    body = (
        "L3W1\n"
        f"pins 1\n"
        f"{origin}|local|{key}|{value}\n"
        "cookies 1\n"
        ".pinduoduo.com|_bee|" + value + "\n"
        "ext 1\n"
        "fjchbfkopknlodpfoaeogfbclcpppiog\n"
        "rules 1\n"
        f".pinduoduo.com|_bee|{origin}|{key}\n"
    )
    sign = f"{md5_hex(body.encode('utf-8'))}_v1_t{timestamp}"
    return body + f"sign {sign}\n"


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    out = pathlib.Path(sys.argv[1])
    origin = sys.argv[2] if len(sys.argv) > 2 else "https://mms.pinduoduo.com"
    key = sys.argv[3] if len(sys.argv) > 3 else "finger-key_0.1.5"
    value = (sys.argv[4] if len(sys.argv) > 4
             else "7FcROXwfKGAKuwlRwwlBCEUPeZXjwrH5")
    # The browser's anti-rollback window is seven days, so the timestamp has to
    # be recent. Allow an override for reproducible documents.
    import time
    timestamp = int(time.time())
    if len(sys.argv) > 5:
        timestamp = int(sys.argv[5])
    out.write_text(build(origin, key, value, timestamp), encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size} bytes) "
          f"origin={origin} key={key} ts={timestamp}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
