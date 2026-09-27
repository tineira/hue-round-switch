# Método y límites — round

## Qué se leyó

Árbol completo de `main` (`3515da7b`). Lectura de:

- Contrato: `AGENTS.md`, `README.md`, `CHANGELOG.md`, `docs/firmware-artifacts.md`, `docs/pages-requirements.md`, `docs/specs/stability.md`, `docs/specs/finished/idle-display.md`, `docs/specs/finished/input-during-hue.md`
- Runtime: `hue-round-switch.ino`, `console.h`, `pages.h`, `recipes.h`, `hue_job.h` (parcial; archivo ~28 KB, se leyeron loop-side + run recipe/dim/refresh), `hue.h`, `usb_setup.h`, `channels.h` (BOOT only), `touch.h`, `display.h`, `json_util.h`, `snapshot.h`
- `ui.h` (~31 KB): se usó como contexto de ownership (loop pinta, hue_job ejecuta). No se auditó cada tema visual.
- Build/CI: `sketch.yaml`, `THIRD_PARTY.json`, `.github/workflows/build.yml`, `.github/workflows/firmware.yml`
- Contrato de consola: `docs/device-api.md`, `AGENTS.md`

No se flasheó. No se midió FPS ni heap en placa.

## Ownership (stability.md §4.9, confirmado en el `.ino`)

| Actor | Core | Dueño de |
| --- | --- | --- |
| `loop` | 1 | UI, gestos, pages/recipes RAM + su NVS |
| `hueJob` | (task propia) | Bridge, link state, IP/key/id, todo Clip v2 |
| `consoleJob` | 0 | Vercel; el body llega al loop por cola de 1 |

Ese reparto es la diferencia grande con Simple.

## IDs cruzados

| ID | Aquí |
| --- | --- |
| P4 snapshot vacío | Mitigado: `hueJobSnapshot` falla → no POST |
| P8 poll en loop | Cerrado: task + cola |
| P11 apply sin bump | **Parcialmente vivo:** timeout sí se aplica con rev igual; group/dim no |
| P12 Wi-Fi retry | Cerrado: auto-reconnect + retry 30 s + grace 5 s en UI |
| P21 whitespace JSON | Mismo `json_util` que Simple (espacio/nl/tab tras `:`) |

## Confianza

Alta en contrato register/config, USB y el hecho de que Hue no corre en el loop. Media en `ui.h` / touch edge-cases (archivo grande, specs de input-during-hue cubren la intención). Media en tope NVS de `pages` `putString` (no se midió un payload de 6 pages con rids largos).
