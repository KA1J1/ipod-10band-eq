#!/usr/bin/env python3
"""Build the OSOS payload (and optionally the flashed image) on Windows.

This drives the repository's own recipe builder directly, so it needs only
Python + an ARM EABI GCC/binutils, not Cargo, Make, Perl or Apple inputs.

    python build_payload.py --repo osos-lab --out build/osos \
        --prefix toolchain/.../bin/arm-none-eabi- --jobs 8
    python assemble_recipe.py --recipe build/osos/osos.json \
        --data build/osos/osos.data --inputs inputs --out build/osos-cfw.bin
"""

import argparse
import json
import shutil
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--prefix", required=True, help="e.g. /path/bin/arm-none-eabi-")
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--version", default=None, help="override the reported version")
    parser.add_argument("--revision", default=None)
    parser.add_argument("--work", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    prefix = str(Path(args.prefix).resolve()) + "-"
    if not shutil.which(prefix + "gcc"):
        parser.error(f"ARM GCC not found: {prefix}gcc")

    sys.path.insert(0, str(repo))
    sys.path.insert(0, str(repo / "tools"))
    import firmware  # noqa: E402  (tools/firmware.py)
    import version  # noqa: E402  (tools/version.py)
    from patching.recipes.osos import build_recipe  # noqa: E402

    info = version.identity(repo)
    revision = args.revision or info["revision"]
    release = args.version or info["version"]

    target = firmware.load()
    fingerprint = target["inputs"]["osos.bin"]
    metadata = json.loads((firmware.DIRECTORY / "ui.json").read_text())
    if metadata["schema"] != 1 or metadata["input"] != fingerprint:
        parser.error("UI metadata does not match the pinned OSOS")

    work = args.work or (args.out / ".work")
    print(f"payload: revision {revision}, version {release}", flush=True)
    recipe = build_recipe(
        repo / "payload",
        work / "payload",
        fingerprint,
        metadata["resources"],
        prefix,
        args.jobs,
        revision,
        release,
    )
    args.out.mkdir(parents=True, exist_ok=True)
    files = recipe.save(args.out, "osos")
    print(f"recipe: {files['recipe']} ({Path(files['recipe']).stat().st_size} bytes)")
    print(f"data:   {files['data']} ({Path(files['data']).stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
