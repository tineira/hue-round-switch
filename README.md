# hue-round-switch

Wi‑Fi Hue switch for the **Seeed XIAO ESP32-S3** + **Round Display** (1.28" circle, GC9A01). Talks local Clip v2 to the Bridge. Not Zigbee.

Same console as `hue-simple-switch`: [hue.tineira.com](https://hue.tineira.com). This product is **touched on the screen**, not wired to digital pins.

The contract this firmware implements lives in the console repo: `hue-switch-console/docs/device-api.md`, `docs/definitions.md`, and the pages requirements in `docs/round-pages.md`.

## Setup

1. **Product:** flash and provision from Chrome on [hue.tineira.com](https://hue.tineira.com) → Devices (USB, no Arduino). Improv saves the 2.4 GHz network and `HUESET` stores the token/url in NVS `console`. None of that is compiled in.
2. **Development:** copy `config.example.h` to `config.h` (only `SERIAL_DEBUG`). Provision the XIAO once from the console; `arduino-cli upload` does not erase NVS, so the network and token survive every flash.
3. The XIAO finds the Bridge over mDNS and can pair the Hue key (screen: *Press the Bridge button*). Both are kept in flash.
4. It registers with the console (`POST /api/device/register`) with `"product": "round"` and `channels: []`, and asks for its config (`GET /api/device/config`: `pages[]`, recipes per `pageId`, `dim`).
5. In the console, edit the **pages** (group, tap, double tap, theme). A tap does **not** wait for Vercel: NVS → Bridge. The ring dims according to `pages[].dim` (`group` or `lights`).

Arduino IDE: open `hue-round-switch.ino`, board **XIAO_ESP32S3**, PSRAM **OPI**, USB CDC on boot **Enabled**.

arduino-cli:

```
arduino-cli compile --profile xiao-s3 .
arduino-cli upload  --profile xiao-s3 -p COMx .
```

Display library: `GFX Library for Arduino` in `C:\Users\tinei\Arduino\libraries` (do not mix with the GigaDash / TFT_eSPI install in OneDrive).

On the Round Display v1.1, the backlight slide switch (`D6`) must be **ON**. The XIAO's USB-C points away from the circle.

`config.h` is not committed.

## Release

A push to `main` **is a release**. `.github/workflows/firmware.yml` builds the product image, publishes the `usb-installer` GitHub Release, and triggers the console to pull the bins into `public/firmware/round/`. The Devices screen then offers that build to every Round plugged in over USB.

- `FIRMWARE_VERSION` in `hue-round-switch.ino` is the version the console shows. Bump it for any change a board should pick up, and add a `## Round` entry to the console's `docs/changelog.md`.
- Image layout and offsets: [`docs/firmware-artifacts.md`](docs/firmware-artifacts.md).
- The pipeline needs the `CONSOLE_REPO_TOKEN` secret in this repo. Setup, rotation and troubleshooting: the console repo's `README.md`, "Firmware release pipeline".
