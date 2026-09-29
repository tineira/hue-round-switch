# hue-round-switch

Wi‑Fi Hue switch for the **Seeed XIAO ESP32-S3** + **Round Display** (1.28" circle, GC9A01). Talks local Clip v2 to the Bridge. Not Zigbee.

Same console as `hue-simple-switch`: [hue.tineira.com](https://hue.tineira.com). This product is **touched on the screen**, not wired to digital pins.

The contract this firmware implements lives in the console repo: `hue-switch-console/docs/device-api.md`, `docs/definitions.md`, and the pages requirements in `docs/round-pages.md`.

## Setup

1. **Product:** flash and provision from Chrome on [hue.tineira.com → Setup](https://hue.tineira.com/setup) (USB, no Arduino). Improv saves the 2.4 GHz network and `HUESET` stores the token/url in NVS `console`. None of that is compiled in.
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

Display library: `GFX Library for Arduino` 1.6.8, pinned in `sketch.yaml` (arduino-cli installs it; with the IDE, use the Library Manager). This sketch does not use TFT_eSPI.

On the Round Display v1.1, the backlight slide switch (`D6`) must be **ON**. The XIAO's USB-C points away from the circle.

`config.h` is not committed.

## Release

A push to `main` **is a release**. `.github/workflows/firmware.yml` builds the product image (`SERIAL_DEBUG` 0), checks `THIRD_PARTY.json` against `sketch.yaml`, and uploads the four installer parts to the console (`POST <console>/api/firmware/round`, where `<console>` is the `CONSOLE_UPLOAD_URL` repository variable, `https://hue.tineira.com` when unset) with this version's `CHANGELOG.md` entry as release notes and `THIRD_PARTY.json` as credits. The console's USB installer then offers that build to every Round plugged in over USB. A missing token (on this repo) or notes, a credits mismatch, or an upload error fails the run. The workflow also keeps the `usb-installer` GitHub Release as a download link.

- `FIRMWARE_VERSION` in `hue-round-switch.ino` is the version the console shows. Bump it for any change a board should pick up, and add a `### X.Y.Z — YYYY-MM-DD` entry at the top of this repo's `CHANGELOG.md` in the same commit (not the console's `docs/changelog.md`). A push without a bump re-sends the notes only (`409 version_exists` warning).
- [`THIRD_PARTY.json`](THIRD_PARTY.json) credits the core, ESP-IDF and every library linked into the image; the console shows it on `/credits`. `scripts/check-credits.py` fails the pull request check and the release run if the core or any library pinned in `sketch.yaml` is missing from it or has a different version. Update it in the same commit as any core or library bump.
- Image layout and offsets: [`docs/firmware-artifacts.md`](docs/firmware-artifacts.md).
- The pipeline needs the `FIRMWARE_UPLOAD_TOKEN` secret in this repo. Setup, rotation and troubleshooting: the console repo's `README.md`, "Firmware release pipeline".

### Building from a fork

A fork builds the same image and can upload it to its own console (see the self-hosting guide, `docs/self-hosting.md` in the console repo):

1. Pick a long random value and set it as `FIRMWARE_UPLOAD_TOKEN` in your console's environment.
2. In your fork, under **Settings → Secrets and variables → Actions**, add the repository **variable** `CONSOLE_UPLOAD_URL` with your console's origin (for example `https://hue.example.org`, no trailing path) and the repository **secret** `FIRMWARE_UPLOAD_TOKEN` with the same value.
3. Push to `main` (or run `firmware-binaries` by hand). The release waits in your console's `/admin` until you make it current.

Without the secret, a fork's run still builds the image, checks the credits and keeps the `usb-installer` release, and skips the upload with a notice. On `tineira/hue-round-switch` a missing secret fails the run.

## Contributing

See [`CONTRIBUTING.md`](CONTRIBUTING.md). Bugs and ideas go in GitHub issues.

## Sponsor

The console and the switch firmwares are free and stay that way. If they are useful
to you, you can [sponsor the project on GitHub](https://github.com/sponsors/tineira).
Sponsorship helps pay for development and does not unlock anything.

## License

Copyright (c) 2026 Tomas Neira and contributors. [MIT](LICENSE).

The image also links open-source components under their own licenses, listed in
[`THIRD_PARTY.json`](THIRD_PARTY.json). The Arduino-ESP32 core is LGPL-2.1-or-later:
because this firmware's source is public, you can rebuild it against a modified core.

The console this firmware talks to, [`hue-switch-console`](https://github.com/tineira/hue-switch-console),
is AGPL-3.0; it also holds the device contract (`docs/device-api.md`).

Not affiliated with or endorsed by Signify. Philips Hue is a trademark of Signify.
