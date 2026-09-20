# Round Display — reposo de pantalla

Documento de **requisitos de producto**. Cubre `hue-round-switch` (firmware, círculo) y `hue-switch-console` (timeout por aparato). `hue-simple-switch` está **fuera de alcance**.

No es un screensaver (reloj, widgets, animación). Es **reposo**: el disco se apaga cuando nadie lo toca, para gastar menos (luz de fondo, y en batería el resto) y no dejar un faro en la pared.

**Estado:** requisitos, no implementado.

Alinea `docs/pages-requirements.md` §9.1 (ajuste del display), poll §11.2 y decisión 25.

---

## 1. Veredicto

Tras **X segundos** sin toque en Ready, el círculo entra en reposo: **backlight off**, panel negro. No se pinta Ready a media tinta.

El **primer contacto** (dedo abajo → lift) **solo despierta**. No dispara tap, doble, aro ni swipe. El usuario no sabe qué página está; tiene que ver el disco (nombre, fill, escena, puntos, aro) y **después** gesticular.

El siguiente gesto completo, ya con la pantalla on, actúa como siempre.

X lo elige el usuario en la **consola**, una vez por aparato (como el eje de swipe). **Default 30 s.**

---

## 2. Problema

Ready deja el GC9A01 y el BL en D6 encendidos todo el día. Eso es el gasto grande (y la luz de noche). El `loop()` y el Wi‑Fi a full suman, pero sin apagar el disco el ahorro es cosmética.

Un interruptor de pared que dispara receta a ciegas, con el disco negro, cambia la página o la lámpara **equivocada**. Por eso el wake no actúa.

---

## 3. Resultado esperado

| Situación | Qué pasa |
| --- | --- |
| Ready, nadie toca, pasan X s (X > 0) | Reposo: BL off, panel negro. Sin copy, sin `...`, sin reloj. |
| Reposo, primer toque (cualquier zona, cualquier movimiento) | BL on, se pinta Ready de la **página activa** (la de NVS). Ese contacto **no** es receta ni swipe ni dimmer. Al soltar, el usuario ve dónde está. |
| Ready despierto, tap / doble / aro / swipe | Como hoy (`input-during-hue.md`, fill partido, etc.). |
| Despierto y otra vez X s sin toque | Vuelve a reposo. |
| X = 0 en consola | Nunca reposo (siempre on). |
| Wi‑Fi, pairing, error, boot, loading | No entran en reposo. Hay que leer el mensaje. |

El timer de inactividad se **reinicia** con cualquier toque, incluido el de wake.

El contacto de wake **no** abre la ventana de doble tap. Un tap justo después del lift de wake es un tap nuevo, no el segundo golpe de un doble.

BOOT hold 3 s (re-pair) sigue vivo en reposo: es GPIO, no receta de pantalla.

---

## 4. Consola

Ajuste del **display** (una vez por Round, junto a Page swipe), no por página.

- **Screen timeout** — segundos de inactividad hasta reposo.
- Default **30**.
- **0** = Off (la pantalla no se apaga).
- Entero. Rango válido: **0** o **10–600**. Fuera de rango lo rechaza el server (o lo clampa en firmware al bajar: 0 o 10–600, default 30).
- Copy en inglés, p. ej. label `Screen timeout`, hint `Seconds until the display sleeps. 0 = always on.`
- Simple-switch: no se muestra.

Se guarda con el resto de páginas (Save pages / el PUT que ya persiste `pageSwipeAxis`). Sube `rev`. Baja en el poll de config.

Campo JSON (camelCase, junto a `pageSwipeAxis`):

```text
screenTimeoutSec: 30
```

Persistencia consola: columna (o equivalente) en `switches`, p. ej. `screen_timeout_sec integer not null default 30`. Aparatos viejos sin columna → 30.

El firmware guarda el valor en NVS con el resto de ajustes del display. Si el poll no trae el campo, se queda el NVS / 30.

---

## 5. Firmware (comportamiento)

### 5.1 Entrar en reposo

Solo desde **Ready** (o Empty, si no hay páginas: igual negro, igual wake para ver el vacío). Reloj de inactividad = último lift (o último sample de drag) + `screenTimeoutSec`.

Si `screenTimeoutSec == 0`, no arrancar el reloj.

Al entrar: BL D6 LOW (o el sleep del GC9A01 + BL off). No redibujar Ready. No `UI_BUSY`. No texto.

### 5.2 En reposo

- No GET de on/brillo/escena cada ~20 s.
- No redibujar el disco.
- Seguir pudiendo notar el INT de CHSC6X (D7) para despertar.
- Poll de consola (1 h) puede correr: no pinta el círculo. Si cambia config, aplicar en NVS; el disco sigue negro hasta wake.
- Worker Hue: no encolar refresh de página. Un PUT in-flight de un gesto *anterior* puede terminar; no pinta sobre negro.

No deep-sleep del ESP. No `WiFi.disconnect()`. Modem sleep (`setSleep(true)` en reposo, `false` al despertar) es **permitido**, no obligatorio en v1. Lo obligatorio es apagar el BL.

### 5.3 Wake (primer contacto)

1. INT o primer punto en el círculo.
2. BL on, `uiPaint` Ready de la página activa.
3. Marcar ese gesto como **wake**: hasta el lift, no `uiFireEvent`, no `uiDimPut`, no `pagesNext`/`pagesPrev`.
4. Al lift: timer de inactividad a cero. Listo para el **siguiente** gesto.

Refresh de estado Hue (on, brillo, escena, fill partido) en **background** al despertar, igual que al cambiar de página. El usuario ve al menos nombre + último fill local; el GET corrige.

### 5.4 Gestos después del wake

Igual que Ready hoy. Tap/doble/aro/swipe y last-wins de HTTP no cambian.

### 5.5 Qué no es esto

- No es hold de receta.
- No es un tap que “a veces” actúa (si el disco ya estaba on, el tap actúa; si estaba en reposo, no).
- No es atenuar el fill y dejar el BL a full.

---

## 6. Fuera de alcance (v1)

- Reloj, widgets, animación de reposo.
- PWM de BL en varios escalones (on / dim / off). v1 es on u off.
- Deep sleep del S3, apagar Wi‑Fi, wake por timer de Hue.
- Timeout distinto por página.
- `hue-simple-switch`.
- Medir mA en este documento (sí conviene medirlo al implementar).

---

## 7. Criterio de hecho

- En la consola, un Round tiene **Screen timeout** (default 30, 0 = always on), se guarda y baja en el poll.
- Con timeout 30, Ready sin toque ~30 s → disco negro.
- Primer toque en negro → se ve la página activa; **no** cambia luces ni página.
- El toque siguiente (ya despierto) sí actúa.
- Timeout 0 → no se apaga.
- Pairing / error / Wi‑Fi no se apagan solos.
- Simple-switch y el resto de gestos Ready no cambian de significado cuando la pantalla ya está on.
