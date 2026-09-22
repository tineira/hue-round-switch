# Round Display firmware artifacts (USB installer)

Product flash for hue.tineira.com. Chrome writes these parts with Web Serial / esptool-js. **No erase** — NVS (Hue, recipes, pages, `console`) survives a reflash.

Chip: Seeed XIAO ESP32-S3. Partition scheme: `default_8MB` (`sketch.yaml` profile `xiao-s3`). PSRAM OPI, USB Mode Hardware CDC and JTAG, CDC on boot enabled.

## Product compile

Empty `WIFI_SSID`, `WIFI_PASSWORD`, `CONSOLE_URL`, `CONSOLE_TOKEN`. `SERIAL_DEBUG` 0. CDC is still opened (`Serial.begin`) so Improv + `HUESET` work.

```
# config.h (gitignored) — product values:
#   WIFI_SSID "" / WIFI_PASSWORD "" / CONSOLE_URL "" / CONSOLE_TOKEN "" / SERIAL_DEBUG 0

arduino-cli compile --profile xiao-s3 --export-binaries .
```

CI (`.github/workflows/firmware.yml`) writes that `config.h` and compiles on push to `main`. Dev machines keep a filled `config.h`; do not use this screen against localhost.

`--export-binaries` writes under `build/esp32.esp32.XIAO_ESP32S3/` (gitignored). `boot_app0.bin` is copied there next to `flash_args`.

The sketch folder must be named `hue-round-switch` (GitHub checkout). A worktree named something else needs a junction/symlink of that name.

## Flash map

`build/esp32.esp32.XIAO_ESP32S3/flash_args` from `arduino-cli compile --profile xiao-s3 --export-binaries` on Arduino-ESP32 **3.3.12** (same as `platform.txt` upload recipe; `XIAO_ESP32S3.build.bootloader_addr=0x0`):

```
--flash-mode dio --flash-freq 80m --flash-size 8MB
0x0 hue-round-switch.ino.bootloader.bin
0x8000 hue-round-switch.ino.partitions.bin
0xe000 boot_app0.bin
0x10000 hue-round-switch.ino.bin
```

| Offset | File (export name) | Notes |
| --- | --- | --- |
| `0x0` | `hue-round-switch.ino.bootloader.bin` | S3 bootloader |
| `0x8000` | `hue-round-switch.ino.partitions.bin` | `default_8MB` table |
| `0xe000` | `boot_app0.bin` | otadata initial (`tools/partitions/boot_app0.bin`) |
| `0x10000` | `hue-round-switch.ino.bin` | app0 (`0x330000` slot) |

esptool-js: `--flash-mode dio`, `--flash-freq 80m`, `--flash-size 8MB`. Do **not** erase. Do **not** use `hue-round-switch.ino.merged.bin` as the installer path.

`default_8MB.csv`: nvs `0x9000` / `0x5000` (20 KB), otadata `0xe000` / `0x2000`, app0 `0x10000` / `0x330000`, app1 `0x340000` / `0x330000`, spiffs `0x670000` / `0x180000`.

Firmware version is `FIRMWARE_VERSION` in `hue-round-switch.ino` (`0.5.22`). The console manifest should show that string.

Do not flash a C6 with this map. The installer must read the chip and abort if it is not an ESP32-S3.
