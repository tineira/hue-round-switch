#!/usr/bin/env bash
# Builds and runs the host tests (plain C++, no board). Run from anywhere.
# Needs g++ (or set CXX). CI runs this in build.yml's compile job.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
out="${TMPDIR:-/tmp}/hue-round-host-tests"
"${CXX:-g++}" -std=gnu++17 -g -O1 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
  -fsanitize=address,undefined -fno-sanitize-recover=undefined \
  -I "$here/stubs" -I "$root" \
  "$here/test_json.cpp" -o "$out"
"$out"
