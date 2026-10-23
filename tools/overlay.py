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
    ("native/wire_reader.h", "lengsuo/native/wire_reader.h"),
    ("native/profile_catalog.h", "lengsuo/native/profile_catalog.h"),
    ("native/profile_catalog.cc", "lengsuo/native/profile_catalog.cc"),
    ("native/BUILD.gn", "lengsuo/native/BUILD.gn"),
    ("native/golden.inc", "lengsuo/native/golden.inc"),
    ("native/canvas_noise.h", "lengsuo/native/canvas_noise.h"),
    ("native/canvas_noise.cc", "lengsuo/native/canvas_noise.cc"),
    ("native/canvas_apply.h", "lengsuo/native/canvas_apply.h"),
    ("native/canvas_apply.cc", "lengsuo/native/canvas_apply.cc"),
    ("native/fp_overrides.h", "lengsuo/native/fp_overrides.h"),
    ("native/fp_overrides.cc", "lengsuo/native/fp_overrides.cc"),
    ("native/font_family_traits.h", "lengsuo/native/font_family_traits.h"),
    ("native/font_family_traits.cc", "lengsuo/native/font_family_traits.cc"),
    ("native/font_metrics.h", "lengsuo/native/font_metrics.h"),
    ("native/font_metrics.cc", "lengsuo/native/font_metrics.cc"),
    ("native/product_brand.h", "lengsuo/native/product_brand.h"),
    ("integration/browser_bootstrap.cc", "lengsuo/integration/browser_bootstrap.cc"),
    ("integration/official_defaults.h", "lengsuo/integration/official_defaults.h"),
    ("integration/official_defaults.cc", "lengsuo/integration/official_defaults.cc"),
    ("integration/browser_bootstrap.h", "lengsuo/integration/browser_bootstrap.h"),
    ("integration/profile_bootstrap_state.cc",
     "lengsuo/integration/profile_bootstrap_state.cc"),
    ("integration/profile_bootstrap_state.h",
     "lengsuo/integration/profile_bootstrap_state.h"),
    ("integration/browser_bootstrap_unittest.cc",
     "lengsuo/integration/browser_bootstrap_unittest.cc"),
    ("integration/official_defaults_unittest.cc",
     "lengsuo/integration/official_defaults_unittest.cc"),
    ("native/l3_wire.h", "lengsuo/native/l3_wire.h"),
    ("native/l3_wire.cc", "lengsuo/native/l3_wire.cc"),
    ("native/l3_runtime.h", "lengsuo/native/l3_runtime.h"),
    ("native/l3_runtime.cc", "lengsuo/native/l3_runtime.cc"),
    ("native/storage_pin.h", "lengsuo/native/storage_pin.h"),
    ("native/storage_pin.cc", "lengsuo/native/storage_pin.cc"),
    ("native/storage_consistency.h", "lengsuo/native/storage_consistency.h"),
    ("native/storage_consistency.cc", "lengsuo/native/storage_consistency.cc"),
    ("native/cache_record.h", "lengsuo/native/cache_record.h"),
    ("native/cache_record.cc", "lengsuo/native/cache_record.cc"),
    ("native/extension_pin.h", "lengsuo/native/extension_pin.h"),
    ("native/extension_pin.cc", "lengsuo/native/extension_pin.cc"),
    ("native/cookie_mirror.h", "lengsuo/native/cookie_mirror.h"),
    ("native/cookie_mirror.cc", "lengsuo/native/cookie_mirror.cc"),
    ("native/kernel_pipe.h", "lengsuo/native/kernel_pipe.h"),
    ("native/kernel_pipe.cc", "lengsuo/native/kernel_pipe.cc"),
    ("native/kernel_pipe_test.cc", "lengsuo/native/kernel_pipe_test.cc"),
    ("native/kernel_pipe_frame.h", "lengsuo/native/kernel_pipe_frame.h"),
    ("native/kernel_pipe_frame.cc", "lengsuo/native/kernel_pipe_frame.cc"),
    ("native/kernel_pipe_frame_test.cc",
     "lengsuo/native/kernel_pipe_frame_test.cc"),
    ("native/kernel_pipe_json.h", "lengsuo/native/kernel_pipe_json.h"),
    ("native/kernel_pipe_json.cc", "lengsuo/native/kernel_pipe_json.cc"),
    ("native/kernel_pipe_json_test.cc",
     "lengsuo/native/kernel_pipe_json_test.cc"),
    ("native/kernel_pipe_envelope.h",
     "lengsuo/native/kernel_pipe_envelope.h"),
    ("native/kernel_pipe_envelope.cc",
     "lengsuo/native/kernel_pipe_envelope.cc"),
    ("native/kernel_pipe_envelope_test.cc",
     "lengsuo/native/kernel_pipe_envelope_test.cc"),
    ("native/kernel_pipe_keys.h", "lengsuo/native/kernel_pipe_keys.h"),
    ("native/kernel_pipe_keys.cc", "lengsuo/native/kernel_pipe_keys.cc"),
    ("native/kernel_pipe_keys_test.cc",
     "lengsuo/native/kernel_pipe_keys_test.cc"),
    ("native/kernel_pipe_test_util.h",
     "lengsuo/native/kernel_pipe_test_util.h"),
    ("native/action_table.h", "lengsuo/native/action_table.h"),
    ("native/action_table.cc", "lengsuo/native/action_table.cc"),
    ("native/action_table_test.cc", "lengsuo/native/action_table_test.cc"),
    ("native/machine_facts.h", "lengsuo/native/machine_facts.h"),
    ("native/machine_facts.cc", "lengsuo/native/machine_facts.cc"),
    ("native/machine_facts_test.cc",
     "lengsuo/native/machine_facts_test.cc"),
    ("native/profile_bundle.h", "lengsuo/native/profile_bundle.h"),
    ("native/profile_bundle.cc", "lengsuo/native/profile_bundle.cc"),
    ("native/profile_bundle_test.cc", "lengsuo/native/profile_bundle_test.cc"),
    ("native/l3_envelope.h", "lengsuo/native/l3_envelope.h"),
    ("native/l3_envelope.cc", "lengsuo/native/l3_envelope.cc"),
    ("native/l3_envelope_test.cc", "lengsuo/native/l3_envelope_test.cc"),
    ("native/l3_envelope_session.h", "lengsuo/native/l3_envelope_session.h"),
    ("native/l3_envelope_session.cc", "lengsuo/native/l3_envelope_session.cc"),
    ("native/l3_envelope_session_test.cc",
     "lengsuo/native/l3_envelope_session_test.cc"),
    ("native/resp_monitor.h", "lengsuo/native/resp_monitor.h"),
    ("native/resp_monitor.cc", "lengsuo/native/resp_monitor.cc"),
    ("native/resp_monitor_config.h", "lengsuo/native/resp_monitor_config.h"),
    ("native/resp_monitor_config.cc", "lengsuo/native/resp_monitor_config.cc"),
    ("native/resp_monitor_body.h", "lengsuo/native/resp_monitor_body.h"),
    ("native/resp_monitor_body.cc", "lengsuo/native/resp_monitor_body.cc"),
    ("native/resp_monitor_report.h", "lengsuo/native/resp_monitor_report.h"),
    ("native/resp_monitor_report.cc", "lengsuo/native/resp_monitor_report.cc"),
    ("native/resp_monitor_test.cc", "lengsuo/native/resp_monitor_test.cc"),
    ("native/resp_monitor_config_test.cc",
     "lengsuo/native/resp_monitor_config_test.cc"),
    ("native/resp_monitor_body_test.cc",
     "lengsuo/native/resp_monitor_body_test.cc"),
    ("native/resp_monitor_report_test.cc",
     "lengsuo/native/resp_monitor_report_test.cc"),
    ("native/fp_sampling.h", "lengsuo/native/fp_sampling.h"),
    ("native/fp_sampling.cc", "lengsuo/native/fp_sampling.cc"),
    ("native/fp_sampling_test.cc", "lengsuo/native/fp_sampling_test.cc"),
    ("native/fp_converge.h", "lengsuo/native/fp_converge.h"),
    ("native/fp_converge.cc", "lengsuo/native/fp_converge.cc"),
    ("native/fp_converge_test.cc", "lengsuo/native/fp_converge_test.cc"),
    ("native/fp_target.h", "lengsuo/native/fp_target.h"),
    ("native/fp_target.cc", "lengsuo/native/fp_target.cc"),
    ("native/fp_target_match.h", "lengsuo/native/fp_target_match.h"),
    ("native/fp_target_match.cc", "lengsuo/native/fp_target_match.cc"),
    ("native/fp_target_test.cc", "lengsuo/native/fp_target_test.cc"),
    ("native/fp_overrides_test.cc",
     "lengsuo/native/fp_overrides_test.cc"),
    ("integration/kernel_pipe_client.h", "lengsuo/integration/kernel_pipe_client.h"),
    ("integration/kernel_pipe_client.cc", "lengsuo/integration/kernel_pipe_client.cc"),
    ("integration/l3_bootstrap.h", "lengsuo/integration/l3_bootstrap.h"),
    ("integration/l3_bootstrap.cc", "lengsuo/integration/l3_bootstrap.cc"),
    ("integration/l3_provisioning_impl.h",
     "lengsuo/integration/l3_provisioning_impl.h"),
    ("integration/l3_provisioning_impl.cc",
     "lengsuo/integration/l3_provisioning_impl.cc"),
    ("integration/l3_provisioning_impl_test.cc",
     "lengsuo/integration/l3_provisioning_impl_test.cc"),
    ("public/mojom/l3_provisioning.mojom",
     "lengsuo/public/mojom/l3_provisioning.mojom"),
    ("public/mojom/BUILD.gn", "lengsuo/public/mojom/BUILD.gn"),
    ("integration/BUILD.gn", "lengsuo/integration/BUILD.gn"),
    # The access policy (task T2). Appended at the end: the mapping is
    # append-only so that two cards can add their own files without touching
    # each other's lines.
    ("native/policy_rules.h", "lengsuo/native/policy_rules.h"),
    ("native/policy_rules.cc", "lengsuo/native/policy_rules.cc"),
    ("native/policy_rules_test.cc", "lengsuo/native/policy_rules_test.cc"),
    ("native/policy_match.h", "lengsuo/native/policy_match.h"),
    ("native/policy_match.cc", "lengsuo/native/policy_match.cc"),
    ("native/policy_match_test.cc", "lengsuo/native/policy_match_test.cc"),
    ("native/policy_block.h", "lengsuo/native/policy_block.h"),
    ("native/policy_block.cc", "lengsuo/native/policy_block.cc"),
    ("native/policy_block_test.cc", "lengsuo/native/policy_block_test.cc"),
    ("native/policy_report.h", "lengsuo/native/policy_report.h"),
    ("native/policy_report.cc", "lengsuo/native/policy_report.cc"),
    # The access policy's driver (task 0018). Appended at the end, after the
    # kernels it drives: the mapping is append-only so that two cards can add
    # their own files without touching each other's lines.
    ("integration/policy_throttle.h", "lengsuo/integration/policy_throttle.h"),
    ("integration/policy_throttle.cc", "lengsuo/integration/policy_throttle.cc"),
    ("integration/policy_throttle_test.cc",
     "lengsuo/integration/policy_throttle_test.cc"),
)
GN_TARGETS = ("//lengsuo/integration:bootstrap", "//lengsuo/native:profile_wire")
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


def execute(source, repository, lock, apply=False, allow_patched_baseline=False):
    receipt = {"schema_version": 1, "mode": "apply" if apply else "plan",
               "status": "failed", "files": [], "build_verified": False,
               "browser_integrated": False, "cross_file_atomic": False,
               "gn_target": "//lengsuo/native:profile_wire",
               "gn_targets": list(GN_TARGETS)}
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
        if lock["patches"] and not allow_patched_baseline:
            # Patched baselines are applied by tools.patch_apply; placement
            # here needs the explicit override so patches are never ignored.
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
    parser.add_argument("--allow-patched-baseline", action="store_true")
    args = parser.parse_args()
    try:
        receipt = execute(args.source, args.repository, read_lock(args.lock),
                          args.apply, args.allow_patched_baseline)
    except (BaselineError, OSError, ValueError) as error:
        receipt = {"status": "failed", "error": str(error), "files": [],
                   "build_verified": False, "browser_integrated": False,
                   "gn_target": "//lengsuo/native:profile_wire",
                   "gn_targets": list(GN_TARGETS)}
    print(json.dumps(receipt, indent=2))
    return 1 if receipt["status"] == "failed" else 0


if __name__ == "__main__":
    raise SystemExit(main())
