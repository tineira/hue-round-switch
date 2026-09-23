# hue-round-switch

Interruptor Hue Wi‑Fi para **Seeed XIAO ESP32-S3** + **Round Display** (círculo 1.28", GC9A01). Habla Clip v2 local con el Bridge. No es Zigbee.

Misma consola que `hue-simple-switch`: [hue.tineira.com](https://hue.tineira.com). Este producto se **toca en la pantalla**, no en pines digitales.

## Setup

1. **Producto:** flashea y provisiona desde Chrome en [hue.tineira.com](https://hue.tineira.com) (USB, sin Arduino). Improv guarda la red 2.4 GHz y `HUESET` deja token/url en NVS `console`. Nada de eso va compilado.
2. **Desarrollo:** copia `config.example.h` a `config.h` (solo `SERIAL_DEBUG`). Provisiona el XIAO una vez desde la consola; `arduino-cli upload` no borra la NVS, así que la red y el token siguen tras cada flasheo.
3. El XIAO busca el Bridge por mDNS y puede emparejar la key Hue (pantalla: *Press the Bridge button*). Quedan en flash.
4. Se registra en la consola (`POST /api/device/register`) con `"product": "round"` y `channels: []`, y pide config (`GET /api/device/config`: `pages[]`, recetas por `pageId`, `dim`).
5. En la consola, edita **páginas** (grupo, tap, doble tap, theme). El tap **no** espera a Vercel: NVS → Bridge. El aro dimmea según `pages[].dim` (`group` o `lights`). `c1` es solo migración vieja; este firmware no lo manda.

Arduino IDE: abre `hue-round-switch.ino`, placa **XIAO_ESP32S3**, PSRAM **OPI**, USB CDC on boot **Enabled**.

arduino-cli:

```
arduino-cli compile --profile xiao-s3 .
arduino-cli upload  --profile xiao-s3 -p COMx .
```

Librería de pantalla: `GFX Library for Arduino` en `C:\Users\tinei\Arduino\libraries` (no mezclar con GigaDash / TFT_eSPI de OneDrive).

En el Round Display v1.1, el slide del backlight (`D6`) tiene que estar **ON**. USB-C del XIAO hacia afuera del círculo.

`config.h` no se sube a git.
