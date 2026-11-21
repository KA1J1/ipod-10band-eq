#!/usr/bin/env python3
"""Run the repository's host C contract fixtures without sanitizers.

`patching/tests/test_payload.py` builds every fixture with
`-fsanitize=address,undefined`, which the MinGW host compiler used here does not
ship. This runner generates the same headers and compiles the same fixtures with
the same warnings, minus the sanitizer instrumentation.
"""

import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

FIXTURES = (
    "album_artist",
    "album_artists_index",
    "album_artists",
    "ui_visibility",
    "ui_model",
    "eq_preferences",
    "eq_audio",
    "eq_canvas",
    "menu_resources",
    "bitmap_assets",
    "dark_mode",
    "osos_handoff",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--keep", action="store_true", help="keep generated headers")
    parser.add_argument("fixtures", nargs="*", default=[])
    args = parser.parse_args()
    repo = args.repo.resolve()
    sys.path.insert(0, str(repo))
    from patching.ui import Resources, generate

    metadata = json.loads((repo / "firmware/ui.json").read_text())
    bindings = json.loads((repo / "payload/native/ui.json").read_text())["bindings"]
    selected = [name for name in args.fixtures or FIXTURES]
    temporary = Path(tempfile.mkdtemp(prefix="payload-test-"))
    generate(
        Resources(metadata["resources"], bindings),
        [
            repo / "payload/dark_mode.ui",
            repo / "payload/custom_eq.ui",
            repo / "payload/cfw_info.ui",
            repo / "payload/album_artists.ui",
        ],
        temporary,
        "test",
    )
    failures = 0
    for name in selected:
        executable = temporary / name
        compile_command = [
            args.cc,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-g",
            "-I",
            str(temporary),
            "-I",
            str(repo / "payload"),
            str(repo / "patching/tests/fixtures" / f"{name}.c"),
            "-o",
            str(executable),
        ]
        build = subprocess.run(compile_command, capture_output=True, text=True)
        if build.returncode:
            failures += 1
            print(f"FAIL {name}: build")
            print(build.stdout + build.stderr)
            continue
        run = subprocess.run([str(executable)], capture_output=True, text=True)
        if run.returncode:
            failures += 1
            print(f"FAIL {name}: exit {run.returncode}")
            print(run.stdout + run.stderr)
            continue
        print(f"ok   {name}: {run.stdout.strip()}")
    if not args.keep:
        subprocess.run(["cmd", "/c", "rmdir", "/s", "/q", str(temporary)], check=False)
    else:
        print(f"headers kept in {temporary}")
    print(f"{len(selected) - failures}/{len(selected)} fixtures passed")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
