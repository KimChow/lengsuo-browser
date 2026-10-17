"""Run independent Python/Rust v1/v2 encoders against frozen C++ fixtures."""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rustc", default="rustc")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location(
        "golden_generator", repo / "tests/golden_v2/gen_golden_v2.py")
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    with tempfile.TemporaryDirectory(prefix="lengsuo-golden-") as directory:
        work = Path(directory)
        for version, fixture in [(1, "golden.inc"), (2, "golden_v2.inc")]:
            data = bytes(generator.build(
                {**generator.FIELDS, "schema_version": version}))
            body = (repo / "native" / fixture).read_text().split("{", 1)[1]
            frozen = bytes(map(int, re.findall(r"\d+", body.split("}", 1)[0])))
            if data != frozen:
                raise ValueError(f"Python v{version} differs from frozen fixture")
            (work / f"golden_v{version}_python.bin").write_bytes(data)
        env = {**os.environ, "REVIEWS_DIR": str(work),
               "GOLDEN_V1_PATH": str(repo / "native/golden.inc"),
               "GOLDEN_V2_SNAPSHOT": str(repo / "native/golden_v2.inc")}
        binary = work / ("golden_test.exe" if os.name == "nt" else "golden_test")
        subprocess.run([args.rustc, "--edition", "2021", "--test",
                        "-D", "warnings", "-o", str(binary),
                        str(repo / "tests/golden_v2_rust.rs")],
                       env=env, check=True, timeout=120)
        subprocess.run([str(binary), "--test-threads=1"], env=env,
                       check=True, timeout=60)


if __name__ == "__main__":
    main()
