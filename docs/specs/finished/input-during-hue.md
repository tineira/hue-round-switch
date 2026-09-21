# Round Display — toques mientras Hue responde

Documento de **requisitos de producto**. Cubre `hue-round-switch` (firmware, círculo). No es una guía de implementación ni un changelog.

La consola (`hue-switch-console`) y `hue-simple-switch` están **fuera de alcance**. El círculo no espera a Vercel; este documento trata el **Bridge local** (Clip v2 HTTPS en LAN).

Cierra el hueco de `docs/pages-requirements.md` §8.1 y §13: el GET+PUT al Bridge **sí ocurre**, pero **el disco no se congela** mientras tanto.

**Estado:** implementado. Firmware **0.5.8** saca GET/PUT de receta, dimmer y refresh del `loop()` (`hue_job.h`: un slot last-wins en tarea propia). Firmware **0.5.14+** saca snapshot / register / poll de consola del loop de toque. `hueHttp()` sigue síncrono, pero en esas tareas, no en Ready. Pairing / discover / re-pair BOOT 3 s pueden seguir en el loop. `pages-requirements.md` §8.1 / §13 / decisión 23 apuntan aquí.

---

## 1. Veredicto

**Seguir aceptando toques.** Un tap, doble tap o soltar el aro **no** bloquea Ready. Un segundo gesto es un comando nuevo, no “espera a que las lámparas terminen”.

**No bloquear la pantalla.** No entrar a `UI_BUSY`, no pintar `...`, no textos de espera ni de ayuda. El ack es lo que Ready ya sabe pintar: invert al apretar el centro, fill on/off, nombre de escena, aro en vivo.

**Último comando gana.** No hace falta una cola larga de gestos. Si llega otro tap o otro soltar de aro mientras un GET/PUT sigue en vuelo, se **reemplaza** el trabajo pendiente (last-wins). El swipe de página nunca espera red.

Eso es lo que hacen un Hue dimmer / Tap Dial, un Lutron Maestro y HomeKit: el control de pared sigue vivo; las luces tardan (transición CLIP ~400 ms; el Bridge limita ~10 PUT/s a `/light` y ~1/s a `/grouped_light`).

---

## 2. Problema

El usuario está de pie, a un brazo, tocando un círculo de 39 mm. Un tap debe sentirse como un interruptor de pared, no como un formulario que espera 201.

Antes de 0.5.8 no era así:

1. `hueHttp()` sigue siendo GET/PUT síncrono en el `loop()` (timeouts 2,5–8 s). `recipeFire()` y `uiDimPut()` salen de `uiPollTouch()` / `uiTick()`. No hay cola ni tarea Hue.
2. CHSC6X no entrega IDs de gesto. Tap, doble (`kDoubleTapMs` = 350, `gSecondTap` al segundo down, fire al lift), swipe (≥40 px) y lift se infieren en `uiPollTouch` / `uiTick`. Si el loop está dentro de HTTP, esos FSM **no corren**: el siguiente toque se pierde.
3. Grupo + `dim` ya están en firmware: un ciclo de escenas es GET `status.active` (2500 ms) por candidata y luego PUT; un aro `PAGE_DIM_LIGHTS` es GET por rid y PUT a las on. Eso **alarga** el bloqueo, no lo saca del loop.
4. `UI_BUSY` (pinta `...` y pulso de aro) **existe** y **no se entra** desde tap, doble, dimmer ni swipe. No hay que usarlo en Ready.
5. El invert del centro se limpia al lift **antes** de `recipeFire()`. El fill on/off y el nombre de escena se pintan **después** de un HTTP ok (`uiSetLightOn` / `uiApplyLastSceneName`). No hay ack optimista: durante el PUT el disco queda en Ready quieto.
6. `gOnHueWait` bombea `uiTick` solo en el wait de **pairing** (`hue_discover.h`), no en GET/PUT de receta. `uiTick` no llama `uiPollTouch`. No cuenta como “Ready vivo”.
7. Tras un swipe, `uiOnPageChanged` pinta ya y deja `gNeedHueState`; el GET de on/brillo/escena corre en `uiTick` cuando no hay dedo. Ese GET **sí** bloquea el loop al soltar.

El spec de páginas exige disco vivo (`pages-requirements.md` §8.1, §13, decisión 23). Receta / aro / refresh de página salieron del loop en 0.5.8; snapshot / register / poll de consola en 0.5.14.

---

## 3. Resultado esperado

En Ready, con Wi-Fi y Bridge ok:

| El usuario hace | Qué ve (al tiro) | Qué pasa con el siguiente gesto |
| --- | --- | --- |
| Tap (centro, receta `short`) | Invert al down; al lift, fill on/off y escena **locales** (sin how-to) | Otro tap, doble, aro o swipe se acepta **sin** esperar el PUT |
| Doble tap (`double_click`) | Igual: invert, luego fill / escena / off | Igual |
| Arrastrar el aro | El arco 270° sigue el dedo (1–100 absoluto) | Durante el drag no hay PUT. Al soltar, un PUT del **último** %. Si suelta otra vez antes de que vuelva el HTTP, gana el **último** % |
| Swipe de página | Nombre, puntos y theme de la nueva página **al cruzar el umbral** | Nunca espera Hue. El GET de estado de la página nueva va en background |
| Receta vacía | Nada Hue; el gesto es no-op | El loop no se bloquea |

Tras un PUT **ok**: el fill y el nombre de escena ya pintados en local se quedan. Un poll posterior (~20 s) o el GET de fondo al cambiar de página puede corregir si el Bridge discrepa.

Tras un PUT **fallido** (timeout, 4xx, Wi-Fi down): pantalla de error como hoy, un rato, y vuelta a Ready. No se queda un `...` eterno.

Nada de esto pinta copy nueva en Ready. Siguen prohibidos `Tap to toggle`, `Please wait`, `...` de `UI_BUSY`, y cualquier spinner de texto.

---

## 4. Política de input (cerrada)

1. **Ready nunca se congela** por un GET/PUT de receta o dimmer. `uiPollTouch` / `uiTick` (o el equivalente que infiera gestos) siguen corriendo.
2. **Last-wins**, no ignore-during-inflight y no cola FIFO. Un tap nuevo sustituye el PUT de receta que aún no salió o que sigue en vuelo. Un segundo soltar de aro sustituye el brillo pendiente. No se “acumulan” tres taps para ejecutarlos en serie cuando Hue libere.
3. **Un PUT de dimmer por soltar**, no uno por sample del drag. Eso ya es así (`gBriLastSent`); se mantiene. Si el % no cambió, no hay PUT.
4. **Swipe > HTTP.** Cambiar de página pinta ya. Si había un PUT de la página anterior en vuelo, se cancela o se deja morir sin aplicar su ack a la página nueva. El GET de on/brillo/escena de la página nueva es background.
5. **Ack optimista.** Invert = down en el centro. Fill on/off y nombre de escena = resultado **local** de la receta (ciclo: el `rid` que se va a PUT; off: fill apagado y sin línea de escena). El aro en drag = ack de brillo. No hay ack extra de “HTTP en curso”.
6. **`UI_BUSY` no es de Ready.** Sigue para pairing / loading si hace falta. No se entra desde tap, doble, dimmer ni ciclo de escenas.
7. **El Bridge sigue siendo la verdad** de “qué escena está activa” y de on/brillo, en background. El dedo no espera esa verdad para el **siguiente** gesto.
8. **Límites Hue.** No martillar el Bridge: un comando en vuelo a la vez hacia Clip v2 desde este aparato (el last-wins ya serializa). No un PUT por pixel del aro.
9. **Timeouts.** Siguen existiendo (hoy 2500 / 4000 / 8000 ms). Si vencen, error de sistema, no un Ready congelado hasta el timeout: el usuario ya pudo haber hecho otro gesto.
10. **Sin hold de receta.** BOOT hold 3 s (re-pair) no cambia. Re-pair puede bloquear: no es un gesto de Ready.

---

## 5. Cambios que hay que hacer

Solo firmware (`hue-round-switch`). La consola no llama al Bridge. El poll de config (Vercel) no entra en esta feature.

### 5.1 Desacoplar Hue del loop de toque

Hoy GET/PUT de receta y dimmer corren en el mismo `loop()` que lee CHSC6X. Hay que **sacarlos de ese camino**.

Resultado: mientras un GET/PUT está en el cable, el aparato sigue infiriendo down / move / lift / swipe / ventana de doble tap.

Cómo (tarea, cola de un slot, HTTP no bloqueante) lo decide la implementación. El requisito es el comportamiento, no la API de FreeRTOS.

No copiar `HueRecipe[]` / `Page[]` en el stack del loop (sigue valiendo el límite de 8 KB / `SET_LOOP_TASK_STACK_SIZE`).

### 5.2 Recetas (tap / doble)

`uiFireEvent` / `recipeFire` no deben `return` después de un `hueHttp()` síncrono que haya dejado el touch muerto.

Al disparar el gesto:

- Actualizar fill y escena **antes o al mismo tiempo** que se encola el HTTP, con el resultado local (on/off, siguiente `rid` de la lista). Hoy eso ocurre **después** del 200; hay que adelantarlo.
- Encolar **un** trabajo: la receta de la **página activa** y el evento. Si ya hay uno, el nuevo **lo reemplaza**.
- El ciclo de escenas puede seguir haciendo GET `status.active` + PUT al siguiente `rid` (páginas §8.1). Ese GET+PUT no corre en el loop de toque.

### 5.3 Aro

Sin cambio de producto: PUT al soltar, last % wins, skip si `gBriLastSent` igual.

Sí cambia el runtime: `uiDimPut` no bloquea el loop. Un soltar durante un PUT anterior sustituye el % (o se ignora si es el mismo). `mode: lights` (GET por rid + PUT a las on) también fuera del loop de toque.

### 5.4 Swipe y poll de estado

Al swipe: pintar ya (`uiOnPageChanged`). El `gNeedHueState` GET de on/brillo/escena **no** se hace dentro del gesto. Si un PUT de la página anterior sigue vivo, no debe pintar fill/escena de esa página sobre la nueva.

El poll ~20 s de Ready puede seguir; tampoco debe bloquear el toque.

### 5.5 Error

Si el trabajo en vuelo falla y el usuario **no** ha cambiado de página ni disparado otro comando que lo reemplace: `UI_ERROR` como hoy y vuelta a Ready. Si ya hay un comando más nuevo, el fallo del viejo no pisa el ack del nuevo.

### 5.6 Spec de páginas — hecho

`pages-requirements.md` y `hue-switch-console/docs/round-pages.md` ya dicen que el GET+PUT ocurre y el círculo no se congela. No hace falta reescribirlos salvo alinear un detalle nuevo.

---

## 6. Fuera de alcance

- Consola web, spinners de “Saving pages”, o cualquier HTTP a Vercel.
- `hue-simple-switch` (GPIO; su máquina de double-click no se toca).
- Eventstream Clip v2 (`/eventstream`) en v1.
- Cola de N gestos, undo, o “tap rápido = full on” estilo Lutron Maestro (un segundo tap es **la receta de tap**, no un significado extra).
- Animación de slide entre páginas, pulso de aro como busy en Ready, o copy nueva.
- Medir latencia típica de este Bridge (no bloquea el veredicto).
- Cambiar timeouts numéricos salvo que haga falta para no dejar un worker colgado.
- Re-pair (BOOT 3 s) y pantallas de sistema (Wi-Fi, pairing, error): pueden seguir siendo síncronas.

---

## 7. Relación con páginas

No cambia: máximo 6 páginas, tap y doble, sin hold, swipe local, página = un room/zona, `dim` §8.2 (ya en 0.5.7), listas de escenas, Ready sin how-to, ASCII, themes cerrados.

El **trabajo** Hue (GET active + PUT next, dim group/lights) se queda; la **espera del usuario** no. Grupo/dim no sustituyen esta feature: las hacen más urgentes porque hay más GET en el mismo loop.

---

## 8. Criterio de hecho

Esta feature está **lista** cuando, en Ready:

- Un tap (o doble) dispara la receta y **enseguida** se puede tap / doble / aro / swipe otra vez, aunque el PUT anterior no haya vuelto.
- El disco no entra a `UI_BUSY` ni muestra `...` por una receta o un dimmer.
- El invert, el fill y el nombre de escena se actualizan en local al gesto, no al 200 del Bridge.
- El aro sigue last-wins al soltar; no hay un PUT por sample; un segundo soltar gana.
- Un swipe cambia de página al umbral, sin esperar Hue.
- Un PUT que tarda (o el timeout de 2,5–8 s) **no** traga el segundo toque.
- `hue-simple-switch` y la consola no cambian.

Hasta que el HTTP de receta, dimmer y refresh de página salga del loop de toque **y** el fill/escena se pinten en local al gesto, el círculo seguirá perdiendo toques y el ack llegará tarde. Eso es el gap; no un “busy screen” a medias. `gOnHueWait` en pairing no cierra este gap.
