# USB e Improv — round

Archivo: `usb_setup.h`. Espejo de Simple con diferencias de chip y de comandos.

## Comandos ASCII

| Línea | Respuesta | Efecto |
| --- | --- | --- |
| `HUESET token <tok>` | `HUEOK token` / `HUEERR token` | NVS + RAM, limpia 401, `gNeedConsoleSync` |
| `HUESET url <url>` | `HUEOK url` / `HUEERR url` | exige `http(s)://` y largo &lt; 128 |
| `HUEGET` | `HUESTA … product=round chip=s3 token=0\|1 key=0\|1` | no secretos |
| `HUEPAIR` | `HUEOK pair` / `HUEERR no-wifi` | `huePairRequest()` al hueJob |
| `HUECLR` | `HUEOK clear` | cancel pair + forget STA + console + recipes + pages + hue + `gHueClrEpoch++` + notify |
| `HUEBOOT` | **no existe** | `HUEERR unknown` |

Línea máxima 192. `HUESTA` se arma en buffer 1280 con percent-encoding.

## Improv

- Mismo framing que Simple. Info: `hue-round-switch`, `FIRMWARE_VERSION`, `XIAO_ESP32S3/esp32-s3`, `Round Display`.
- Scan asíncrono + retry 400 ms / 15 s. `WiFi.disconnect(false, false)` no borra NVS.
- Connect timeout **30 s** (Simple 20 s).
- `improvHello()` manda Current State al boot (spec Improv: clientes escuchan sin RPC).
- **No** `Serial.flush()` en el send: en HWCDC puede colgar TX hasta que el host lea.

## Diferencias vs Simple

- No `HUEBOOT`. El S3 con Hardware CDC+JTAG se flashea por DTR en el mismo COM. Cerrar el Serial Monitor resetea el chip.
- `gWifiStaForgotten` + `wifiKeepForgotten()` para que HUECLR no deje que auto-reconnect resucite el STA.
- Pair no corre en el parser USB: se pide al hueJob.
- Token/url en buffers C fijos bajo `gConsoleMux`, no `String` globales.

## MED

- Misma aceptación de `http://`.
- URL ≥ 128 → `HUEERR url`. Mejor que truncar.
- Línea ASCII 192 puede cortar un HUESET url largo **antes** del cap 128. Prod cabe.

## GOOD

- ACK de scan inmediato (wizard).
- HUECLR cancela el job de pair **antes** de wipe, y bump de epoch para que hueJob no persista una key a medias.
- HUEGET no filtra secretos.
