# hue-round-switch

Wi-Fi wall switch firmware for Seeed XIAO ESP32-S3 + Round Display. Calls the Philips Hue local API. Not Zigbee.

Same console as `hue-simple-switch` (`https://hue.tineira.com`). This board is a **different product**: tap on the circle, not digital wall contacts.

## Hardware

- Board: Seeed Studio XIAO ESP32-S3 (not Sense, not Plus, unless the user says otherwise)
- Arduino IDE board name: `XIAO_ESP32S3`
- FQBN: `esp32:esp32:XIAO_ESP32S3`
- Core: Arduino-ESP32 **3.3.12** (already installed in `%LOCALAPPDATA%\Arduino15`)
- Flash: 8 MB, PSRAM: 8 MB OPI. Default 8 MB partition (3 MB APP). USB CDC on boot: Enabled. USB Mode: **USB-OTG (TinyUSB)**, not Hardware CDC and JTAG. HWCDC resets the S3 when the serial monitor closes (DTR/RTS); TinyUSB + `Serial.enableReboot(false)` does not. Upload still works; if a later flash fails, hold BOOT.
- Display: Seeed Studio Round Display for XIAO — 1.28" 240×240 GC9A01, capacitive touch CHSC6X (I2C `0x2E`, INT `D7`). Backlight `D6` (v1.1 slide switch must be ON).
- Wi-Fi: 2.4 GHz only.

This sketch lives in the Arduino IDE sketchbook (`directories.user` = `C:\Users\tinei\Arduino`). The parent folder is **not** a git repo. Do not mix with GigaDash (`OneDrive\Documents\Arduino`) and do not edit TFT_eSPI `User_Setup` there.

## Secrets

- `config.h` (gitignored) holds `WIFI_SSID`, `WIFI_PASSWORD`, `CONSOLE_URL`, and `CONSOLE_TOKEN`.
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

Replace `COMx` with the XIAO port (`arduino-cli board list`). First flash on a new S3 may need BOOT held while plugging USB.

## Arduino IDE 2.3.10

- File → Open this folder (`hue-round-switch.ino`)
- Board: `XIAO_ESP32S3` (esp32)
- PSRAM: OPI PSRAM. USB CDC on boot: Enabled. USB Mode: USB-OTG (TinyUSB). Flash: 8 MB.
- Libraries: ESP32 core + Arduino_GFX from this sketchbook. Not LVGL. Not GigaDash TFT_eSPI.

## Channels (v1)

Screen, not GPIO contacts. The console still expects `{ id, gpio, label, kind }`:

| id | gpio | kind | label | event |
| --- | --- | --- | --- | --- |
| `c1` | `0` (placeholder; not a wall input) | `momentary` | `1` | tap → `short` |

`gpio: 0` exists only because the console rejects missing/`< 0` gpio. Do not `pinMode` it as a maintained contact. Physical BOOT (GPIO 0, hold 3 s) is re-pair Hue, not a recipe.

Center tap fires the console recipe. The outer ring is brightness for the same target when it is `light` or `grouped_light` (not a scene): finger position on the arc is 1–100, PUT on release. The center disc is bright when that target is on, dim when off (GET Clip v2 `on`, poll ~20 s). CHSC6X coords are often 0–127 and must be scaled to 240. No extra console slot.

More on-screen channels later. Recipes are assigned in the web console, not on the circle.

## Code conventions

- Arduino `.ino` + small `.h` files; no PlatformIO unless asked
- English identifiers; comments in Spanish if they explain intent
- Screen copy in **English**
- Hue API: local HTTPS Clip v2, not the cloud
- `setInsecure()` only against the Hue Bridge. Verify TLS to `hue.tineira.com`
- Do not impersonate Hue accessories or use Zigbee on this sketch
- Do not put this sketch inside `hue-simple-switch`
