# Code review — hue-round-switch

**Fecha:** 2026-09-27  
**Repo:** `tineira/hue-round-switch` @ `main` (`3515da7b`)  
**Firmware:** `0.5.30` (`hue-round-switch.ino` `FIRMWARE_VERSION`)  
**Hardware:** Seeed XIAO ESP32-S3 + Round Display 240×240 GC9A01, touch CHSC6X, Arduino-ESP32 3.3.12, flash 8 MB, PSRAM 8 MB OPI  
**Contrato:** `hue-switch-console` `docs/device-api.md`  
**Regla de esta entrega:** solo documentos. No se cambió firmware ni secretos.

Relacionado: review de consola en `hue-switch-console/docs/grok-code-review-27092026/` y de Simple en `hue-simple-switch/docs/grok-code-review-27092026/`.

## Cómo leer

| Tag | Significado |
| --- | --- |
| CRIT | Explotable o pérdida de control del dispositivo / flota |
| HIGH | Debe corregirse pronto; impacto real en uso o seguridad |
| MED | Deuda o riesgo con condiciones |
| LOW | Calidad, DX, hygiene |
| GOOD | Vale la pena no romperlo |

## Archivos

1. [01-metodo.md](01-metodo.md) — método y límites
2. [02-seguridad.md](02-seguridad.md) — tokens, TLS, NVS, USB
3. [03-usb-improv.md](03-usb-improv.md) — Improv, HUESET/GET/PAIR/CLR (sin HUEBOOT)
4. [04-console-poll.md](04-console-poll.md) — consoleJob, cola, rev, timeout
5. [05-pages-input-hue.md](05-pages-input-hue.md) — pages, touch, dim ring, hue_job
6. [06-nvs-persistencia.md](06-nvs-persistencia.md) — pages / recipes partidas, flush lazy
7. [07-rendimiento.md](07-rendimiento.md) — cores, colas, dim multi-light
8. [08-contrato.md](08-contrato.md) — vs device-api.md y P9–P11
9. [09-funcionalidades-faltantes.md](09-funcionalidades-faltantes.md) — huecos
10. [10-testing-ops.md](10-testing-ops.md) — CI, changelog, HWCDC
11. [11-lo-que-esta-bien.md](11-lo-que-esta-bien.md) — no romper

## Prioridad sugerida

1. No meter HTTP Hue otra vez en el loop de UI. `hue_job.h` (~28 KB) es el corazón.
2. P11 parcial: `consoleApplyConfig` con `localRev >= remote` **igual** aplica `pagesParseTimeout`. Group/dim del body se ignoran. Decidir si timeout debe bump-ear rev en consola.
3. Tests de `recipesParseConfig` / `pagesParseArray` / dim `mode:lights`.
4. Misma regla `http://` en HUESET que Simple.
5. `kMaxDimLights = 2` y `kMaxPages = 6`: la consola puede mandar más; el firmware recorta en silencio.
