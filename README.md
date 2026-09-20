# hue-round-switch

Interruptor Hue Wi‑Fi para **Seeed XIAO ESP32-S3** + **Round Display** (círculo 1.28", GC9A01). Habla Clip v2 local con el Bridge. No es Zigbee.

Misma consola que `hue-simple-switch`: [hue.tineira.com](https://hue.tineira.com). Este producto se **toca en la pantalla**, no en pines digitales.

## Setup

1. Copia `config.example.h` a `config.h` y rellena `WIFI_SSID` / `WIFI_PASSWORD` (red **2.4 GHz**), `CONSOLE_URL` y `CONSOLE_TOKEN` (API key de aparato en la consola).
2. El XIAO busca el Bridge por mDNS y puede emparejar la key Hue (pantalla: *Press the Bridge button*). Quedan en flash.
3. Se registra en la consola (`POST /api/device/register`) con el canal virtual `c1` y pide recetas (`GET /api/device/config`).
4. En la consola, asigna la receta de **1** (`short`, p. ej. toggle de un room). El tap **no** espera a Vercel: NVS → Bridge. El aro alrededor del círculo regula el brillo de ese mismo destino (no de una escena).

Arduino IDE: abre `hue-round-switch.ino`, placa **XIAO_ESP32S3**, PSRAM **OPI**, USB CDC on boot **Enabled**.

arduino-cli:

```
arduino-cli compile --profile xiao-s3 .
arduino-cli upload  --profile xiao-s3 -p COMx .
```

Librería de pantalla: `GFX Library for Arduino` en `C:\Users\tinei\Arduino\libraries` (no mezclar con GigaDash / TFT_eSPI de OneDrive).

En el Round Display v1.1, el slide del backlight (`D6`) tiene que estar **ON**. USB-C del XIAO hacia afuera del círculo.

`config.h` no se sube a git.
