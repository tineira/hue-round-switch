# hue-round-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-S3 + Round Display. Calls the Philips Hue local API. Not Zigbee.

Same console as `hue-simple-switch` (`https://hue.tineira.com`). This board is a **different product**: tap on the circle, not digital wall contacts.

## Contract

This firmware is one of several switches for one console. The console repo [`hue-switch-console`](https://github.com/tineira/hue-switch-console) owns the contract. Local checkout paths on the maintainer's machine are in `AGENTS.local.md` (gitignored) when it exists; read it to find the sibling repos.

- `docs/device-api.md`: endpoints, auth, payloads, error codes. Authoritative.
- `docs/definitions.md`: product model (recipes, channels, pages).
- `docs/changelog.md`: the console's own release notes. This firmware's notes live in this repo's `CHANGELOG.md`.
- `CHANGELOG.md` (this repo): the release notes CI uploads. A bullet that starts with `Important: ` is shown first on the console's Setup before an update; use it only for something the person must know or do around the update (`docs/specs/finished/setup-update-notes.md`).
- `docs/specs/`: cross-repo specs, each with a checklist per repo.

Rules:

- Do not change what this firmware sends to or expects from the console (endpoints, JSON fields, error handling, NVS keys the console writes over USB) unless `docs/device-api.md` or an approved spec in `docs/specs/` says so. If the work needs a protocol change, stop and propose it for the console repo; do not invent it here.
- When working from a cross-repo spec, do only this repo's checklist section and tick it. The console ships first and stays backward compatible, so boards already on the wall keep working.
- Read the console docs from its checkout (or GitHub); do not copy them into this tree.
- The other switch firmwares (`hue-round-switch`, `hue-simple-switch`, and any later ones) implement the same contract. Do not edit them from this repo. If behavior both should share differs, say so.

## Parallel sessions

Several Claude sessions can work in this repo at once, and they share the checkout in the sketchbook. A branch switch there moves every session's work onto that branch.

- Before every commit, run `git branch --show-current` and confirm it is the branch you mean.
- Do not switch branches, reset or stash in the shared checkout. Do work on another branch in its own worktree, with the folder named like the sketch so arduino-cli still compiles: `git worktree add ../worktrees/<branch>/hue-round-switch -b <branch> origin/main`. Remove it with `git worktree remove` once the branch is merged.
- If you find commits on your branch that are not yours, do not push it. Tell the user.

## Hardware

- Board: Seeed Studio XIAO ESP32-S3 (not Sense, not Plus, unless the user says otherwise)
- Arduino IDE board name: `XIAO_ESP32S3`
- FQBN: `esp32:esp32:XIAO_ESP32S3`
- Core: Arduino-ESP32 **3.3.12** (pinned by the `sketch.yaml` profile)
- Flash: 8 MB, PSRAM: 8 MB OPI. Default 8 MB partition (3 MB APP). USB CDC on boot: Enabled. USB Mode: **Hardware CDC and JTAG** (same COM for flash; DTR reset works without BOOT). Closing Serial Monitor resets the S3 (silicon). `Serial.begin` always (Improv + `HUESET`). `SERIAL_DEBUG` in `config.h`: 0 = no USB logs (product); 1 = USB logs (dev; mixes with Improv).
- Display: Seeed Studio Round Display for XIAO — 1.28" 240×240 GC9A01, capacitive touch CHSC6X (I2C `0x2E`, INT `D7`). Backlight `D6` (v1.1 slide switch must be ON).
- Wi-Fi: 2.4 GHz only.

This sketch lives in the Arduino IDE sketchbook (`arduino-cli config get directories.user`). The sketchbook folder itself is **not** a git repo. This sketch does not use TFT_eSPI; do not edit another project's TFT_eSPI `User_Setup` for it.

## Secrets

- `config.h` (gitignored) holds only `SERIAL_DEBUG`. No Wi-Fi, console URL, or token is compiled in, in dev or product. Wi-Fi is Arduino STA (Improv), token/url are NVS `console` (`HUESET`), both written by the console over USB. Uploads do not erase NVS, so a board provisioned once keeps them across dev flashes.
- Bridge IP, Hue application key, and recipes are not in `config.h`. Discover / pair / NVS / console poll.
- `config.example.h` is the template that is committed.
- Never put SSID, passwords, or Hue keys in the `.ino` or in git.

If `config.h` is missing: `copy config.example.h config.h` and edit it.

## Build (arduino-cli)

Use the same Arduino15 data dir as the IDE so the 3.3.12 core is reused.

Graphics library: **GFX Library for Arduino** (`Arduino_GFX`), pinned in `sketch.yaml`. With the IDE, install it in this sketchbook's `libraries` folder.

```
arduino-cli compile --profile xiao-s3 .
arduino-cli upload  --profile xiao-s3 -p COMx .
arduino-cli monitor -p COMx -c baudrate=115200
```

Host tests: `bash tests/host/run.sh` (g++, no board) builds `tests/host/test_json.cpp` against `json_util.h`, `pages.h` and `recipes.h`, with minimal Arduino stubs in `tests/host/stubs/` (`String`, `Stream`, an in-memory `Preferences`). `build.yml` runs it inside the `compile` job. Extend the stubs only as far as a header under test needs; do not include hardware headers there.

Replace `COMx` with the XIAO port (`arduino-cli board list`, typically COM4 on HWCDC). First flash on a new S3 may need BOOT held while plugging USB. After HWCDC is on the chip, later uploads use DTR on that same COM — no BOOT.

Releases: a push to `main` runs `.github/workflows/firmware.yml`. It builds with `SERIAL_DEBUG` 0 and uploads the four installer parts to the console (`POST https://hue.tineira.com/api/firmware/round`, secret `FIRMWARE_UPLOAD_TOKEN`) with this version's `CHANGELOG.md` entry as the notes; the console serves `/install` and `/changelog` from that upload. A failed upload (missing token or notes, any error) fails the run. Same bins and version again: no change. Different bins under the same version (`409 version_exists`, e.g. a docs-only push): a warning, the notes are updated but the bins are not, so bump `FIRMWARE_VERSION` to ship new bins. The run also refreshes the `usb-installer` GitHub release as a download link.

Updates over Wi-Fi (0.6.0+, console `docs/specs/ota-round.md`): `ota.h`. The poll's `ota` offer is downloaded in the console task into the inactive app slot, checked (length, sha256) and confirmed after the new image's first successful poll (`verifyRollbackLater()` in the `.ino`). NVS namespace `ota` (`dl`, `try`) is the firmware's own, never written over USB. A debug build reporting a version above the current release, plus **Downgrade** on Switches, tests the whole path without a release.

Credits: `THIRD_PARTY.json` (repo root) lists what is linked into the image, with name, version, license and URL: the Arduino-ESP32 core, the ESP-IDF it bundles, and every library under `libraries:` in `sketch.yaml`. The upload sends it as `credits`, and the console shows it on `/credits`. `scripts/check-credits.py` checks that the core and every library pinned in `sketch.yaml` appear there with the same version; the pull request check (`build.yml`) and the release (`firmware.yml`) both run it and fail otherwise; update it in the same commit that changes `sketch.yaml`, with the license read from the component's own files.

Changelog: when `FIRMWARE_VERSION` changes, add a `### X.Y.Z — YYYY-MM-DD` heading at the top of this repo's `CHANGELOG.md` in the same commit (not in the console's `docs/changelog.md`; the console still owns the contract docs above). Without an entry the console rejects the upload. Write each bullet as what changed for the person using the switch (what they see or can now do), not how the code changed: no function names, macros, USB command names, NVS keys, or GPIO numbers. Example: "The switch remembers the Wi-Fi network you saved during setup after it restarts." Not: "Reconnect stored Wi-Fi through the Arduino STA API."

## Arduino IDE 2.3.10

- File → Open this folder (`hue-round-switch.ino`)
- Board: `XIAO_ESP32S3` (esp32)
- PSRAM: OPI PSRAM. USB CDC on boot: Enabled. USB Mode: Hardware CDC and JTAG. Flash: 8 MB. `SERIAL_DEBUG` in `config.h`.
- Libraries: ESP32 core + Arduino_GFX from this sketchbook. Not LVGL. Not TFT_eSPI.

## Pages (not GPIO)

Screen, not wall contacts. Register with `"product": "round"` and `"channels": []`. Do not invent a `c1` / gpio `0` placeholder. `c1` is only old-device migration on the console; this firmware does not send it.

Physical BOOT (GPIO 0, hold 3 s) is Hue re-pair, not a recipe. Do not `pinMode` it as a maintained contact.

Each page has tap (`short`) and double-tap (`double_click`) recipes, plus a dimmer ring from poll `pages[].dim` (§8.2): `null` (no ring), `{ mode: "group", rid }` (grouped_light), or `{ mode: "lights", rids }` (child lights). The ring is **not** “the same target as the recipe.” Finger position on the arc is 1–100, PUT on release. CHSC6X coords are often 0–127 and must be scaled to 240.

Recipes are assigned in the web console, not on the circle.

## Code conventions

- Arduino `.ino` + small `.h` files; no PlatformIO unless asked
- English everywhere: identifiers, comments, docs, specs, README, commit messages. Translate Spanish you touch
- Screen copy in **English**
- Hue API: local HTTPS Clip v2, not the cloud
- `setInsecure()` only against the Hue Bridge. Verify TLS to `hue.tineira.com`
- Do not impersonate Hue accessories or use Zigbee on this sketch
- Do not put this sketch inside `hue-simple-switch`
- Scratch notes (`docs/_audit-*.md`, `docs/_review-*.md`) are not spec. Delete them once used. Do not commit them.
