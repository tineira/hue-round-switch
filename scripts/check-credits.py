#!/usr/bin/env python3
"""Check that THIRD_PARTY.json credits every core and library pinned in sketch.yaml.

Every platform and library under the profile in sketch.yaml must appear in
THIRD_PARTY.json with the same version, so a bump cannot ship stale credits.
Run from the repo root (or pass the root as the only argument). Exit 1 on any
mismatch, with GitHub Actions ::error:: lines.

Used by build.yml (pull requests, before merge) and firmware.yml (release).
"""
import json
import os
import re
import sys

# sketch.yaml platform -> THIRD_PARTY.json entry name.
PLATFORM_NAMES = {"esp32:esp32": "Arduino-ESP32 core"}


def fail(msg):
    print(f"::error::{msg}")
    return 1


def main(root):
    try:
        with open(os.path.join(root, "THIRD_PARTY.json"), encoding="utf-8") as f:
            credits = json.load(f)
    except (OSError, ValueError) as e:
        return fail(f"THIRD_PARTY.json is not valid JSON: {e}")
    if not isinstance(credits, list) or not all(isinstance(c, dict) for c in credits):
        return fail("THIRD_PARTY.json must be a JSON array of objects.")
    listed = {c.get("name"): c.get("version") for c in credits}

    pins, section = [], None
    with open(os.path.join(root, "sketch.yaml"), encoding="utf-8") as f:
        for line in f:
            s = line.split("#", 1)[0].rstrip()
            item = re.match(r"^\s*-\s*(?:platform:\s*)?(.+?)\s*\(([^)]+)\)$", s)
            key = re.match(r"^\s*([\w-]+):", s)
            if item and section:
                name, version = item.group(1), item.group(2)
                entry = f"{name} ({version})"
                if section == "platforms":
                    if name not in PLATFORM_NAMES:
                        return fail(f"sketch.yaml platform '{entry}' has no THIRD_PARTY.json mapping.")
                    name = PLATFORM_NAMES[name]
                pins.append((name, version, entry))
            elif key and key.group(1) != "platform_index_url":
                section = key.group(1) if key.group(1) in ("platforms", "libraries") else None
    if not pins:
        return fail("No platforms or libraries found in sketch.yaml.")

    bad = False
    for name, version, entry in pins:
        if name not in listed:
            fail(f"sketch.yaml pins '{entry}' but THIRD_PARTY.json has no entry named '{name}'.")
            bad = True
        elif listed[name] != version:
            fail(f"sketch.yaml pins '{entry}' but THIRD_PARTY.json lists '{name}' at version '{listed[name]}'.")
            bad = True
    if bad:
        return 1
    print("THIRD_PARTY.json covers sketch.yaml: " + ", ".join(e for _, _, e in pins))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
