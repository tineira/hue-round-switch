# hue-round-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-S3 + Round Display. Calls the Philips Hue local API. Not Zigbee.

Same console as `hue-simple-switch` (`https://hue.tineira.com`). This board is a **different product**: tap on the circle, not digital wall contacts.

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

- `config.h` (gitignored) holds `WIFI_SSID`, `WIFI_PASSWORD`, `CONSOLE_URL`, and `CONSOLE_TOKEN` for **dev**. Product bins compile those empty; Wi-Fi is Arduino STA (Improv), token/url are NVS `console`.
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

USB installer images: compile with empty `WIFI_*` / `CONSOLE_*`, copy the four parts into the **console** tree `public/firmware/round/` and set `manifest.json` `version` to `FIRMWARE_VERSION`. Console agents must not revert that folder; tell them in the same recorte. The wizard shows that version, not this sketch until those files are in the console repo (and deployed).

Changelog: when `FIRMWARE_VERSION` changes, add a `### X.Y.Z — YYYY-MM-DD` heading under `## Round` in the **console** tree `docs/changelog.md`, with the `<!-- commit -->` marker above it, in the same recorte. Write each bullet as what changed for the person using the switch (what they see or can now do), not how the code changed: no function names, macros, USB command names, NVS keys, or GPIO numbers. Example: "The switch remembers the Wi-Fi network you saved during setup after it restarts." Not: "Reconnect stored Wi-Fi through the Arduino STA API."

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
- English identifiers; comments in Spanish if they explain intent
- Screen copy in **English**
- Hue API: local HTTPS Clip v2, not the cloud
- `setInsecure()` only against the Hue Bridge. Verify TLS to `hue.tineira.com`
- Do not impersonate Hue accessories or use Zigbee on this sketch
- Do not put this sketch inside `hue-simple-switch`
- Scratch notes (`docs/_audit-*.md`, `docs/_review-*.md`) are not spec. Delete them once used. Do not commit them.
