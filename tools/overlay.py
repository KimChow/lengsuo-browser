"""Plan/apply the decoder source overlay; never claims browser integration."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import stat


from tools.baseline import BaselineError, _git, inspect_checkout, read_lock

MAPPING = (
    ("native/profile_wire.h", "lengsuo/native/profile_wire.h"),
    ("native/profile_wire.cc", "lengsuo/native/profile_wire.cc"),
    ("native/BUILD.gn", "lengsuo/native/BUILD.gn"),
)
MAX_SOURCE = 1024 * 1024


class OverlayError(ValueError):
    """Unsafe or conflicting overlay operation."""


def _parts(name):
    parts = name.split("/")
    if (not name or "\\" in name or ":" in name or name.startswith("/")
            or any(part in {"", ".", ".."} for part in parts)):
        raise OverlayError("Unsafe relative overlay path")
    return parts


def _safe_path(root, name):
    current = Path(root).absolute()
    for component in (*current.parents, current):
        if component.is_symlink():
            raise OverlayError("Symlink root ancestor")
    for part in _parts(name):
        current = current / part
        if current.is_symlink():
            raise OverlayError("Symlink overlay path")
        if current.exists() and current != Path(root).absolute() / name:
            if not current.is_dir():
                raise OverlayError("Non-directory overlay ancestor")
    return current


def _read(path):
    if os.name == "posix":
        flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
        directory = os.open("/", flags)
        try:
            for part in path.absolute().parts[1:-1]:
                next_directory = os.open(part, flags, dir_fd=directory)
                os.close(directory)
                directory = next_directory
            fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK,
                         dir_fd=directory)
        finally:
            os.close(directory)
        stream = os.fdopen(fd, "rb")
    else:
        stream = path.open("rb")
    with stream:
        if not stat.S_ISREG(os.fstat(stream.fileno()).st_mode):
            raise OverlayError("Overlay entry is not a regular file")
        data = stream.read(MAX_SOURCE + 1)
    if len(data) > MAX_SOURCE:
        raise OverlayError("Overlay source exceeds limit")
    return data

def _supports_apply():
    return os.name == "posix" and hasattr(os, "O_NOFOLLOW")


def _atomic_add(root, name, data):
    # Directory descriptors plus O_NOFOLLOW prevent ancestor-symlink races.
    # hard-link publication is atomic and fails rather than replacing a file.
    if not _supports_apply():
        raise OverlayError("Safe apply requires POSIX no-follow directory APIs")
    flags = os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW
    directory = os.open("/", flags)
    temporary = None
    try:
        components = list(Path(root).absolute().parts[1:]) + _parts(name)[:-1]
        for component in components:
            try:
                next_directory = os.open(component, flags, dir_fd=directory)
            except FileNotFoundError:
                os.mkdir(component, mode=0o755, dir_fd=directory)
                next_directory = os.open(component, flags, dir_fd=directory)
            os.close(directory)
            directory = next_directory
        # Use an unpredictable name, then open relative to the checked parent.
        for _ in range(100):
            temporary = ".lengsuo-overlay-" + secrets.token_hex(12)
            try:
                fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                             os.O_NOFOLLOW, 0o644, dir_fd=directory)
                break
            except FileExistsError:
                temporary = None
        else:
            raise OverlayError("Cannot allocate temporary overlay file")
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, _parts(name)[-1], src_dir_fd=directory,
                dst_dir_fd=directory, follow_symlinks=False)
        os.fsync(directory)
    finally:
        if temporary is not None:
            os.unlink(temporary, dir_fd=directory)
        os.close(directory)


def execute(source, repository, lock, apply=False):
    receipt = {"schema_version": 1, "mode": "apply" if apply else "plan",
               "status": "failed", "files": [], "build_verified": False,
               "browser_integrated": False, "cross_file_atomic": False,
               "gn_target": "//lengsuo/native:profile_wire"}
    try:
        if apply and not _supports_apply():
            raise OverlayError("Safe apply requires POSIX no-follow directory APIs")
        source = Path(source).absolute()
        repository = Path(repository).absolute()
        # Check ancestors before baseline Git inspection follows any paths.
        _safe_path(source, "lengsuo/native")
        _safe_path(repository, "native")
        receipt["baseline"] = inspect_checkout(source, lock)
        if Path(_git(source, "rev-parse", "--show-toplevel").decode().strip()) != source:
            raise OverlayError("Source must be the checkout root")
        if lock["patches"]:
            raise OverlayError("Baseline patches require a separate reviewed applicator")
        pending = []
        for origin, destination in MAPPING:
            data = _read(_safe_path(repository, origin))
            target = _safe_path(source, destination)
            digest = hashlib.sha256(data).hexdigest()
            entry = {"source": origin, "destination": destination,
                     "sha256": digest, "size": len(data), "action": "create"}
            receipt["files"].append(entry)
            dirty = _git(source, "diff", "HEAD", "--name-only", "--", destination)
            if dirty.strip():
                raise OverlayError("Dirty tracked overlay target")
            if target.exists():
                if _read(target) != data:
                    raise OverlayError("Existing overlay target differs")
                entry["action"] = "identical"
            pending.append((destination, data, entry))
        if apply:
            for destination, data, entry in pending:
                if entry["action"] == "create":
                    _atomic_add(source, destination, data)
                    entry["action"] = "created"
        receipt["status"] = "applied" if apply else "planned"
    except (OverlayError, BaselineError, OSError, ValueError, UnicodeError) as error:
        receipt["error"] = str(error)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--repository", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    parser.add_argument("--apply", action="store_true")
    args = parser.parse_args()
    try:
        receipt = execute(args.source, args.repository, read_lock(args.lock), args.apply)
    except (BaselineError, OSError, ValueError) as error:
        receipt = {"status": "failed", "error": str(error), "files": [],
                   "build_verified": False, "browser_integrated": False}
    print(json.dumps(receipt, indent=2))
    return 1 if receipt["status"] == "failed" else 0


if __name__ == "__main__":
    raise SystemExit(main())



