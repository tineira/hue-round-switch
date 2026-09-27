# Pages, input, hue_job — round

No hay GPIO de receta. BOOT GPIO0 hold 3 s = re-pair Hue, no recipe (`channels.h` stub, `channels: []`).

## Pages

- Hasta 6 pages (`kMaxPages`). Nombre ASCII fold clip 12 chars. Theme de una tabla fija (`docs/round-themes.html` → RGB565).
- Tap `short`, double-tap `double_click`. Swipe horizontal o vertical (`pageSwipeAxis`).
- Anillo dim desde `pages[].dim`: `null` | `{mode:group,rid}` | `{mode:lights,rids}`.
- El anillo **no** es “el target de la recipe” (device-api § pages / spec §8.2). Dedo 1–100, PUT al soltar.
- `kMaxDimLights = 2`. Extra rids se droppean al parsear.
- Compat NVS viejo: `dimTarget` single-rid → `PAGE_DIM_GROUP` hasta el siguiente poll.
- Idle timeout desde consola (`screenTimeoutSec` 0 = always on, else 10–600). Primer toque solo despierta (`idle-display.md`).

Touch CHSC6X a menudo 0–127 → escalar a 240 (AGENTS). Fácil de romper si se asume 240 nativo. `touch.h` es el lugar.

## hue_job (~28 KB)

Dueño único del Bridge.

- Dos slots: **user** (recipe / dim, last-wins, `gHueUserGen`) y **background** (refresh / snapshot).
- User preempta background entre requests (`hueServeUser` en medio de un refresh).
- Resultados al loop por cola; gen viejo no se pinta (cambio de page tira pending).
- Recipe: el loop **arma** el estado optimista (`HueArmInfo`) y mueve el cursor de escena **en el gesto**. El job hace el PUT. Si falla, el loop puede deshacer (flash rojo, no pantalla “Hue error”).
- Toggle con estado fresco: un PUT. Toggle stale: GET+PUT en el job (`resolveToggle`), no en el loop.
- Dim group: un PUT `dimming`. Dim lights: GET on de cada luz, PUT solo a las que corresponden (si alguna está on, solo esas; si todas off, on+bri).
- Snapshot de register: el console task pide `hueJobSnapshot` y espera hasta 180 s. Falla → no POST.
- Link state machine (`HueLink`) publica READY / SEARCHING / PAIRING al loop. El loop no habla Clip.

Specs: `docs/specs/finished/input-during-hue.md`, `idle-display.md`, `docs/specs/stability.md`.

Esto es lo que Simple todavía no tiene. No revertir a HTTP en el loop de paint.

## Riesgos

- MED: dim `mode: lights` = N GETs + N PUTs. Con cap 2 está acotado. Si se sube el cap sin serializar, el Bridge se satura.
- MED: `gNeedHueState` / `gNeedFullPaint` tras apply — flicker posible si el poll llega a mitad de gesto (mitigado aplazando poll con dedo down).
- MED: cursor de escena se escribe en RAM **antes** del PUT. Un 404 skip en el job puede dejar el cursor en un rid que no se activó; `prevRid` viaja en el result para deshacer.
- LOW: `channels.h` es stub. No copiar el de Simple.

## Display

Arduino_GFX, no TFT_eSPI/LVGL. Backlight D6 (slide switch v1.1 ON). `SET_LOOP_TASK_STACK_SIZE(24576)` — comment en console: no copiar recipes al stack del loop.

Pantallas: BOOT / WIFI / WIFI_FAIL / LOADING / NO_BRIDGE / PAIRING / TOKEN / READY / EMPTY. Una sola función (`applyScreen`) decide. Las tasks solo publican estado. Bien.
