"""Report the Claude Code settings keys and any model-related config.

Shell heredocs kept mangling the inline Python, so this lives in a file.
"""

import json
import pathlib

SETTINGS = pathlib.Path.home() / ".claude" / "settings.json"


def main() -> int:
    data = json.loads(SETTINGS.read_text())
    print("settings keys:", sorted(data))
    for key in ("model", "models", "env", "contextWindow", "contextWindows"):
        if key in data:
            print("  %s = %r" % (key, data[key]))
    # Anything mentioning a model name, wherever it is nested.
    blob = json.dumps(data)
    for needle in ("auto", "contextWindow", "200000", "sonnet"):
        if needle in blob:
            print("  contains %r" % needle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
