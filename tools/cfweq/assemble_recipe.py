#!/usr/bin/env python3
"""Apply a reprise recipe (schema 2) to inputs without the Rust assembler.

Byte-for-byte equivalent of `reprise-cli bundle apply-recipe` for recipes that
use only input/data/zero segments (the OSOS payload case). Rules taken from
patching/recipes/segments.py and host/crates/reprise-bundle/src/assembly.rs.

Usage:
    python assemble_recipe.py --recipe build/osos/osos.json \
        --data build/osos/osos.data --inputs inputs --out build/osos-cfw.bin
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path


class RecipeError(Exception):
    pass


def fingerprint(data, label):
    if (
        len(data) != label["bytes"]
        or hashlib.sha256(data).hexdigest() != label["sha256"]
    ):
        raise RecipeError(f"fingerprint mismatch: {label}")


def load_inputs(directory, spec):
    loaded = {}
    for name, label in spec.items():
        # Recipe input names match AppleInputs: "osos" is stored as osos.bin.
        candidates = [Path(directory) / name, Path(directory) / (name + ".bin")]
        path = next((item for item in candidates if item.is_file()), None)
        if path is None:
            raise RecipeError(f"missing input: {candidates[0]}")
        data = path.read_bytes()
        fingerprint(data, label)
        loaded[name] = data
    return loaded


def verify_checks(recipe, inputs):
    for check in recipe["checks"]:
        data = inputs[check["name"]]
        offset = check["offset"]
        expected = bytes.fromhex(check["hex"])
        if data[offset : offset + len(expected)] != expected:
            raise RecipeError(
                f"preimage check failed at {offset:#x} in {check['name']}"
            )


def assemble(recipe, data, inputs):
    if recipe.get("relocations"):
        raise RecipeError("relocations are not supported by this assembler")
    if recipe.get("ffs_checksums"):
        raise RecipeError("ffs checksums are not supported by this assembler")
    verify_checks(recipe, inputs)
    out = bytearray()
    for segment in recipe["segments"]:
        kind = segment["kind"]
        count = segment["bytes"]
        if kind == "input":
            source = inputs[segment["name"]]
            out += source[segment["offset"] : segment["offset"] + count]
        elif kind == "data":
            out += data[segment["offset"] : segment["offset"] + count]
        elif kind == "zero":
            out += bytes(count)
        else:
            raise RecipeError(f"unknown segment kind: {kind}")
    if len(out) != recipe["output"]["bytes"]:
        raise RecipeError(f"output size {len(out)} != {recipe['output']['bytes']}")
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--inputs", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        recipe = json.loads(args.recipe.read_text())
        if recipe.get("schema") != 2:
            raise RecipeError(f"unsupported recipe schema: {recipe.get('schema')}")
        data = args.data.read_bytes()
        if (
            len(data) != recipe["data"]["bytes"]
            or hashlib.sha256(data).hexdigest() != recipe["data"]["sha256"]
        ):
            raise RecipeError("compiled data does not match the recipe")
        inputs = load_inputs(args.inputs, recipe["inputs"])
        image = assemble(recipe, data, inputs)
    except (OSError, KeyError, RecipeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(image)
    print(f"assembled {len(image)} bytes, sha256 {hashlib.sha256(image).hexdigest()}")
    print(f"output: {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
