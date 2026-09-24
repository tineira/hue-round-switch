# hue-round-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-S3 + Round Display. Calls the Philips Hue local API. Not Zigbee.

Same console as `hue-simple-switch` (`https://hue.tineira.com`). This board is a **different product**: tap on the circle, not digital wall contacts.

## Contract

This firmware is one of several switches for one console. The console repo `C:\Users\tinei\hue-switch-console` owns the contract:

- `docs/device-api.md`: endpoints, auth, payloads, error codes. Authoritative.
- `docs/definitions.md`: product model (recipes, channels, pages).
- `docs/specs/`: cross-repo specs, each with a checklist per repo.

Rules:

- Do not change what this firmware sends to or expects from the console (endpoints, JSON fields, error handling, NVS keys the console writes over USB) unless `docs/device-api.md` or an approved spec in `docs/specs/` says so. If the work needs a protocol change, stop and propose it for the console repo; do not invent it here.
- When working from a cross-repo spec, do only this repo's checklist section and tick it. The console ships first and stays backward compatible, so boards already on the wall keep working.
- Read the console docs from that path; do not copy them into this tree.
- The other switch firmwares (`C:\Users\tinei\Arduino\hue-round-switch`, `hue-simple-switch`, and any later ones) implement the same contract. Do not edit them from this repo. If behavior both should share differs, say so.

## Hardware

- Board: Seeed Studio XIAO ESP32-S3 (not Sense, not Plus, unless the user says otherwise)
- Arduino IDE board name: `XIAO_ESP32S3`
- FQBN: `esp32:esp32:XIAO_ESP32S3`
- Core: Arduino-ESP32 **3.3.12** (already installed in `%LOCALAPPDATA%\Arduino15`)
- Flash: 8 MB, PSRAM: 8 MB OPI. Default 8 MB partition (3 MB APP). USB CDC on boot: Enabled. USB Mode: **Hardware CDC and JTAG** (same COM for flash; DTR reset works without BOOT). Closing Serial Monitor resets the S3 (silicon). `Serial.begin` always (Improv + `HUESET`). `SERIAL_DEBUG` in `config.h`: 0 = no USB logs (product); 1 = USB logs (dev; mixes with Improv).
- Display: Seeed Studio Round Display for XIAO — 1.28" 240×240 GC9A01, capacitive touch CHSC6X (I2C `0x2E`, INT `D7`). Backlight `D6` (v1.1 slide switch must be ON).
- Wi-Fi: 2.4 GHz only.

This sketch lives in the Arduino IDE sketchbook (`directories.user` = `C:\Users\tinei\Arduino`). The parent folder is **not** a git repo. Do not mix with GigaDash (`OneDrive\Documents\Arduino`) and do not edit TFT_eSPI `User_Setup` there.

## Secrets

- `config.h` (gitignored) holds only `SERIAL_DEBUG`. No Wi-Fi, console URL, or token is compiled in, in dev or product. Wi-Fi is Arduino STA (Improv), token/url are NVS `console` (`HUESET`), both written by the console over USB. Uploads do not erase NVS, so a board provisioned once keeps them across dev flashes.
- Bridge IP, Hue application key, and recipes are not in `config.h`. Discover / pair / NVS / console poll.
- `config.example.h` is the template that is committed.
- Never put SSID, passwords, or Hue keys in the `.ino` or in git.

If `config.h` is missing: `copy config.example.h config.h` and edit it.

## Build (arduino-cli)

Use the same Arduino15 data dir as the IDE so the 3.3.12 core is reused.

Graphics library: **GFX Library for Arduino** (`Arduino_GFX`) in `C:\Users\tinei\Arduino\libraries` (this sketchbook). Do not install it under OneDrive/GigaDash.

```
arduino-cli compile --profile xiao-s3 .
arduino-cli upload  --profile xiao-s3 -p COMx .
arduino-cli monitor -p COMx -c baudrate=115200
```

Replace `COMx` with the XIAO port (`arduino-cli board list`, typically COM4 on HWCDC). First flash on a new S3 may need BOOT held while plugging USB. After HWCDC is on the chip, later uploads use DTR on that same COM — no BOOT.

Releases: a push to `main` runs `.github/workflows/firmware.yml`. It builds with `SERIAL_DEBUG` 0 and uploads the four installer parts to the console (`POST https://hue.tineira.com/api/firmware/round`, secret `FIRMWARE_UPLOAD_TOKEN`) with this version's `CHANGELOG.md` entry as the notes; the console serves `/install` from that upload. Same bins and version again: no change. Different bins under the same version: rejected, so bump `FIRMWARE_VERSION`. Until the console switches over (`docs/specs/firmware-uploads.md` phase B), the upload only warns on failure, and the old path still runs: the `usb-installer` release plus, if `CONSOLE_REPO_TOKEN` is set, the console's `sync-firmware-bins.yml`, which copies the parts into `public/firmware/round/`. Console agents must not revert that folder.

Changelog: when `FIRMWARE_VERSION` changes, add a `### X.Y.Z — YYYY-MM-DD` heading at the top of this repo's `CHANGELOG.md` in the same commit (not in the console's `docs/changelog.md`; the console still owns the contract docs above). Without an entry the console rejects the upload. Write each bullet as what changed for the person using the switch (what they see or can now do), not how the code changed: no function names, macros, USB command names, NVS keys, or GPIO numbers. Example: "The switch remembers the Wi-Fi network you saved during setup after it restarts." Not: "Reconnect stored Wi-Fi through the Arduino STA API."

## Arduino IDE 2.3.10

- File → Open this folder (`hue-round-switch.ino`)
- Board: `XIAO_ESP32S3` (esp32)
- PSRAM: OPI PSRAM. USB CDC on boot: Enabled. USB Mode: Hardware CDC and JTAG. Flash: 8 MB. `SERIAL_DEBUG` in `config.h`.
- Libraries: ESP32 core + Arduino_GFX from this sketchbook. Not LVGL. Not GigaDash TFT_eSPI.

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
