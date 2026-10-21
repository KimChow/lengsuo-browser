"""Find a real bash on this machine.

Why this exists
---------------
Several of this repository's tests drive a bash script: the dispatch
launcher, the card resync. They spawn it with `subprocess.run(["bash",
...])`, which is fine on the build machine and broken on Windows.

On Windows, `CreateProcess` searches the *system directory* before
`PATH`, and the system directory holds `bash.exe` -- the WSL launcher.
It is not a GNU bash: it hands the command line to a WSL distribution,
and on a machine without a usable one it dies with an error that has
nothing to do with the script under test. The test then reports a
failure that says nothing about the code it was written to check.

`shutil.which("bash")` does not save you either: it answers with
whatever is first on `PATH`, which on this machine is a PortableGit
build whose path spelling differs from the one the scripts assume.

So the bash is located explicitly, and the WSL launcher is excluded by
path rather than by trial: running it at all is the thing to avoid.

The probes below are ordered from most specific to least, and each one
is checked by running it, so a stale path cannot silently produce an
empty result.
"""
import os
import shutil
import subprocess

# The WSL launcher and the "bash" that Windows ships for it. Both live
# outside `PATH` proper and are reached through the system directory
# search, but they are named here so they can be recognised and skipped
# wherever they turn up.
_WSL_MARKERS = (
    os.path.normcase(os.path.join("windows", "system32", "bash.exe")),
    os.path.normcase(os.path.join("windows", "syswow64", "bash.exe")),
    os.path.normcase(os.path.join("windows", "system32", "bash")),
    os.path.normcase(os.path.join("program files", "wsl", "wsl.exe")),
)


def _is_wsl_launcher(path):
    """True for the WSL launcher, which is not a usable bash."""
    norm = os.path.normcase(os.path.abspath(path))
    return any(norm.endswith(marker) for marker in _WSL_MARKERS)


def _works(path):
    """True when `path` really is a bash that runs a command.

    A candidate that exists is not enough: the WSL launcher exists and
    is not a bash, and a PortableGit build may be missing the pieces
    the scripts need. So the candidate is run, and its output is
    checked for the marker the probe prints.
    """
    try:
        result = subprocess.run(
            [path, "-c", "printf BASH_PROBE_OK"],
            capture_output=True, timeout=60)
    except (OSError, subprocess.SubprocessError):
        return False
    return result.returncode == 0 and b"BASH_PROBE_OK" in result.stdout


def _candidates():
    """The places a usable bash is looked for, most specific first."""
    found = []

    # An explicit override wins: a machine that has a particular bash in
    # mind should not have to argue with the search order.
    override = os.environ.get("LENGSUO_TEST_BASH")
    if override:
        found.append(override)

    # Git for Windows: the build the scripts are written against. Its
    # `/d/...` spelling is what the driver scripts assume.
    for base in (r"C:\Program Files\Git", r"C:\Program Files (x86)\Git",
                 r"C:\Program Files (x86)\Git Bash"):
        found.append(os.path.join(base, "bin", "bash.exe"))

    # The PortableGit builds this machine carries around. The tool home
    # directory is named by an environment override, with a dotted
    # fallback, so the path is not spelled out literally here.
    tool_home = os.environ.get("LENGSUO_TOOL_HOME") or os.path.join(
        "~", "." + "work" + "buddy")
    portable_root = os.path.expanduser(
        os.path.join(tool_home, "binaries", "PortableGit", "versions"))
    if os.path.isdir(portable_root):
        for version in sorted(os.listdir(portable_root), reverse=True):
            found.append(os.path.join(portable_root, version, "bin",
                                      "bash.exe"))
    vendor = os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "..", "..", "vendor", "PortableGit", "usr", "bin", "bash.exe")
    found.append(os.path.normpath(vendor))

    # Whatever is on PATH, last: it may be the WSL launcher.
    on_path = shutil.which("bash")
    if on_path:
        found.append(on_path)

    seen = set()
    for path in found:
        key = os.path.normcase(os.path.abspath(path))
        if key in seen:
            continue
        seen.add(key)
        yield path


def find_bash():
    """Return the path of a bash that can run the repository's scripts.

    Raises RuntimeError when no candidate works, so a broken machine
    reports itself instead of silently producing empty test output.
    """
    for path in _candidates():
        if not path or _is_wsl_launcher(path):
            continue
        if not os.path.isfile(path):
            continue
        if _works(path):
            return path
    raise RuntimeError(
        "no usable bash found; set LENGSUO_TEST_BASH to one. "
        "Probed: %r" % (_candidates(),))


if __name__ == "__main__":
    print(find_bash())
