# Console poll — round

Arquitectura más madura que Simple.

- Task `consoleJob` 16 KB, core 0, notify.
- Cola de 1 `ConsoleConfigMsg` (puntero a `String` en heap + epoch). El loop aplica (`consoleApplyConfigIfReady`).
- Snapshot Hue lo arma `hueJob` (`hueJobSnapshot`, timeout 180 s), no el task de consola.
- Register `product: "round"`, `channels: []`. No hay placeholder `c1`.
- Si snapshot falla: no POST (mismo “last good” que Simple).

## Rev y 204

- Path incluye `&rev=` salvo `gRecipesBidReset` (cambio de bridge: pide body entero aunque el número coincida).
- 204: nada que parsear.
- Apply (`consoleApplyConfig`):
  - `rev` faltante → abort.
  - si `localRev >= remote` y no force → keep recipes/pages; **sí** parsea `screenTimeoutSec` (`pagesParseTimeout`) y lo guarda si cambió. Group/dim del JSON se descartan.
  - Eso es **P11 parcial**: timeout se actualiza sin bump; group/dim no.
- Parse fail → `recipesLoad` / `pagesLoad` restore NVS. No deja RAM a medias.

## Cadencia

- `X-Poll-Sec` 30–3600; parse **estricto** (`consoleParsePollSec`: solo dígitos, no basura). Mejor que Simple (`toInt()`).
- Register topology máximo cada 1 h aunque el poll sea más frecuente.
- No lanza job si touch/dim/double-tap wait (`gTouchDown`, `gIdleWakeHold`, `gDimDragging`, `gTapWaitDouble`). UX primero.
- Tras apply: `gConsoleConfirmPoll` inmediato.
- 401 → poll 3600 s.

## MED

- `xQueueSend(..., portMAX_DELAY)`: un loop que no drena la cola clava el task de consola. El `.ino` llama `consoleApplyConfigIfReady` tanto con Wi-Fi up como down. No sacar esa llamada.
- Body `new String` en heap. Si el loop está ocupado en un gesto largo, el task espera. Aceptable; no encolar un segundo body.
- Poll aplazado con el dedo abajo puede retrasar un apply minutos si alguien deja la mano en el cristal. El timeout de idle (10–600 s o 0) no cancela `gTouchDown` por sí solo si el driver reporta contacto fantasma.
- `gRecipesBidReset` omite `&rev=`. La consola entonces no puede responder 204. Es el punto: quiere el body. Bien.

## GOOD

- Token copiado bajo mux antes del HTTP.
- Epoch tira bodies post-HUECLR.
- Confirm poll.
- GPIO/touch no espera Vercel.
- product `"round"` + `channels: []` explícitos.
