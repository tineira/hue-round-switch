# Rendimiento — round

S3 dual core, 8 MB flash, 8 MB PSRAM. El presupuesto es el loop de UI (core 1), no el HTTP.

## Reparto

- Loop 24 KB stack: touch, paint, apply config, flush lazy, wifi tick.
- `consoleJob` 16 KB core 0: GET/POST Vercel.
- `hueJob`: todo Clip. Keep-alive / reuse está en `hue.h` (no se re-audita aquí cada flag; el spec stability.md lo pide).
- Cola resultados Hue: si está llena, se tira el viejo y entra el nuevo (`huePostResult`). Last-wins.

## Dim y refresh

- Dim group: 1 PUT.
- Dim lights (≤2): hasta 2 GET + 2 PUT.
- Refresh split-fill (dos child lights): 2 GET, con `hueServeUser()` entre medio para no tardar un tap.
- Snapshot register: 4 streams en hueJob, console task espera ≤ 180 s. Durante ese rato el loop sigue pintando.

## Poll vs gesto

`consolePollTick` no dispara job si hay dedo abajo / dim drag / wait double. Evita apply a mitad de swipe. Costo: un rev nuevo espera a que la mano se vaya.

Idle backlight off no debería dejar `gTouchDown` eterno; si el INT del CHSC6X queda pegado, el poll se aplaza hasta reset. MED de hardware.

## Paint

`gNeedFullPaint` tras apply. Un poll 200 con pages nuevas redibuja todo. A 30 s durante edición es aceptable. No hay diff de theme.

## MED

- `hueJobSnapshot` 180 s puede coincidir con taps. User slot preempta entre requests del snapshot; un snapshot largo se alarga más. Correcto vs “UI muerta”.
- Cola de config `portMAX_DELAY`: ver 04.
- `jsonEachArrayObject` malloc por objeto. S3 lo aguanta mejor que el C6.
- Refresh de escena activa (scan) puede hacer varios GET. Se corta si llega un user job.

## GOOD

- HTTP Hue fuera del loop.
- Optimistic paint + result queue.
- Gen descarta results de la page anterior.
- Wi-Fi drop grace 5 s: no parpadea “No Wi-Fi” en un glitch.
- Auto-reconnect nativo + retry 30 s.
