# Round Display — páginas

Documento de **requisitos de producto**. Cubre `hue-round-switch` (firmware, círculo) y `hue-switch-console` (web). No es una guía de implementación ni un changelog.

Copia canónica también en `hue-switch-console/docs/round-pages.md`. Mantener ambos alineados.

**Estado:** requisitos vigentes. Páginas v1 ya están en consola y firmware. Esta revisión ancla cada página a un **grupo** y redefine el aro (§8.2); **eso aún no está en código**.

**Productos:**

| Repo | Rol en esta feature |
| --- | --- |
| `hue-round-switch` | Círculo 240×240. Una página activa a la vez. Gestos de centro + aro de brillo. Swipe cambia de página. |
| `hue-switch-console` | CRUD de páginas (grupo room/zona, recetas, listas de escenas, eje, theme, nombre). Baja `group` + destinos de dimmer en el poll. |
| `hue-simple-switch` | **Fuera de alcance.** Sigue siendo GPIO + canales. La UI de la consola se ramifica por producto. |

La consola **nunca** llama al Bridge. El Bridge **nunca** ve Vercel. El dedo en el círculo **nunca** espera a la web. Eso no cambia.

---

## 1. Veredicto

**Sí: el concepto tiene sentido.** Un Round Display es un solo disco físico. Hoy es un canal (`c1` / tap → `short`) más un aro de dimmer. Sin páginas, cada habitación extra exige otro aparato o recargar la receta. Las páginas son la forma de que **un** círculo controle varias cosas, sin dibujar botones que no caben.

**Sí: cabe en la pantalla**, con límites estrictos de copy. El disco útil del centro (radio 88 px, diámetro 176 px) muestra el **nombre de página** (size 2, ≤12) y, debajo, la **escena activa** si aplica (size 1, ellipsis). Puntos de página abajo. **Sin textos de ayuda** (`Tap to toggle`, `Drag ring to dim`, etc.).

El chip táctil (CHSC6X) **no** entrega gestos. Tap, doble y swipe se infieren en firmware a partir de INT + (x, y). **No hay hold** en el círculo: pelea con el swipe y con el lift poco fiable del chip.

---

## 2. Problema

Hoy el Round Display es un interruptor de **una** receta. La consola espera canales estilo pared (`{ id, gpio, label, kind }`). El círculo no es un GPIO: es una superficie. Para controlar living + pasillo + velador hay que o bien poner tres discos, o bien cambiar la receta en el teléfono.

El usuario está de pie, a un brazo, tocando un círculo de 39 mm. La UI tiene que ser un estado a la vez, legible, con pocos gestos y sin menús.

---

## 3. Idea

Una **página** está anclada a **un grupo Hue** (`room` o `zone`). Nombre, theme, tap y doble viven ahí. Luces y escenas de las recetas **solo** salen de ese grupo. El aro dimmea según §8.2 (el grupo, o solo las luces de las acciones).

Varias páginas viven en el mismo aparato. El swipe (eje elegido **por display** en la consola) pasa de una a otra. No es una receta Hue: es navegación local.

```text
Página 1 "Living" (room Living)
 tap → cycle Relax, Bright, Night
 double → off Living
 aro → dim Living (grupo; solo luces on)

Página 2 "Patio" (room Patio)
 tap → toggle Patio
 double → off Patio
 aro → dim Patio (grupo)

Página 3 "Lamps" (room Living)
 tap → toggle Velador 1
 double → toggle Velador 2
 aro → dim solo Velador 1 y 2 (las que estén on)
```

Un gesto de escena es una **lista ordenada** de escenas **de ese grupo**. Off no es una escena: es `off` del `grouped_light` del grupo, en **doble tap**.

---

## 4. Conceptos

**Página.** Configuración activa, **siempre** de un `room` o `zone`. No es un canal GPIO. La consola las crea, nombra, ancla al grupo, ordena, colorea y borra. El firmware **no** declara cuántas hay: las recibe en el poll.

**Grupo de la página.** `room` o `zone` del snapshot + su `grouped_light`. Filtra la topología: luces hijas y escenas de ese group. No se mezclan Living y Patio. Una bombilla suelta no es un grupo (no hay página “sin room”).

**Página activa.** La que se ve y la que recibe gestos. Una sola. El swipe la cambia. Se recuerda en NVS (reboot vuelve a la última).

**Puntos de página.** Abajo del disco interior (como en `docs/round-themes.html`). Un punto por página, en el orden de la consola. El punto **lleno** es la página activa; los demás, apagados. Con una sola página no se dibujan. Cerrado: no es un adorno del prototipo, es el chrome del círculo. **Nunca overflow:** la fila entera queda dentro del disco interior; no se recorta contra el borde redondo ni se mete al aro. Una sola fila, nunca wrap. Ver §5.1.

**Gesto de centro.** Ocurre en el disco interior (radio ≤ 88). Eventos de receta: `short` (tap) y `double_click` (doble tap). En la consola **los dos huecos siempre están**; el usuario asigna receta o los deja vacíos. Vacío = no-op. Si doble no tiene receta, el tap no espera la ventana del segundo toque. **No hay hold** en pantalla.

**Aro / dimmer.** Disco exterior (radio ~96–118). No es receta. Drag + PUT de brillo al soltar. Destino: el **grupo** o **las luces de las acciones**, según §8.2. Como la app Hue: solo luces que están **on**; si todas las del destino están off, el drag las **prende** a ese %.

**Lista de escenas.** Receta `recall_scene` con 1–8 escenas del **grupo de la página**, en el orden que el usuario fija en la consola. Un ítem = el recall de hoy. Varios = rotar. El Bridge no rota: cada toque es un PUT a un `rid`; el aparato elige el siguiente.

**Swipe de página.** Gesto de navegación, no de Hue. Eje `horizontal` (left/right) o `vertical` (up/down), **por aparato**, no por página. Configurable en consola.

**Theme de página.** Paleta RGB565 del círculo (fondo, tinta, apagado, acento, aro). No es el theme CSS de la web (`ember`, `paper`, …). La consola muestra un preview circular al elegir.

**Nombre de página.** Texto principal **en el círculo**. Lo edita el usuario en la consola. Puede ser “Living”, “Patio”, un apodo.

**Escena activa.** Línea debajo del nombre, solo si esa página tiene lista de escenas y el Bridge (o el último recall) dice cuál está puesta. Es el nombre Hue de esa escena, no un hint. Si no hay escena activa (apagado, toggle, custom), esa línea **no se pinta**.

**Canal GPIO `c1`.** Placeholder actual para que la consola acepte el registro. Con páginas deja de ser el modelo de producto del Round. El simple-switch no se toca.

---

## 5. Cabe en el círculo

Pantalla: 240×240, círculo completo. Centro (120, 120).

| Zona | Radio | Uso |
| --- | --- | --- |
| Disco interior | 0–88 | Nombre de página, escena activa (si hay), puntos, tap / doble / swipe |
| Muerto | 88–96 | No es tap ni aro |
| Aro | 96–118 | Dimmer. No cambia de página |

Fuente built-in Arduino (celda 6×8 en size 1):

| Size | Px / carácter | Caracteres en ~140 px útiles |
| --- | --- | --- |
| 1 | 6×8 | ~23 — poco legible a un brazo |
| 2 | 12×16 | **~12** — nombre de página |
| 3 | 18×24 | ~7 — nombres cortos, opcional |
| 4 | 24×32 | ~5 — hoy el `1`; demasiado para un nombre |

Ancho de cuerda del disco interior a la altura actual del título (y ≈ 92, 28 px sobre el centro): ~167 px. Con margen, **12 caracteres a size 2 caben**. “Living Room” (11) cabe; “Master bedroom” (14) no, se corta con ellipsis.

### 5.1 Puntos: caben, o se achican

El máximo de producto sigue siendo **6 páginas**. Aun así el firmware **no** dibuja a tamaños fijos que se salgan del círculo.

Colocación:

- Una fila, centrada en X, cerca del borde **inferior del disco interior** (radio 88), no en el aro.
- Margen mínimo ~8 px al borde del disco (la cuerda en esa Y es el presupuesto).
- Encima van nombre de página y, si cabe, escena activa. No hay hints de gesto ni de dimmer que empujen esta fila.

Encaje (en este orden; parar en el primer que entre):

1. Ideal: diámetro 6 px, gap 10 px.
2. Bajar el **gap** hasta 4 px.
3. Bajar el **diámetro** hasta 4 px (gap 4).
4. Último recurso (no debería dispararse con N ≤ 6): no dibujar puntos que se clippen; pintar `2/6` en size 1 centrado en esa fila. No segunda fila. No carrusel de puntos (el swipe ya es el carrusel).

La fila se centra. El punto activo se llena con `ink`. Los otros: `ink` atenuado sobre el fill (mismo truco que la escena; `mute` sobre ámbar no contrasta). Al swipe, solo cambia cuál está lleno (y el nombre/theme).

Con 6 puntos a 4+4 el ancho es ~44 px; la cuerda abajo del disco interior es >100 px. El recorte 2–3 existe para no romper si el layout vertical empuja la fila hacia el borde (cuerda más estrecha).

### 5.2 Escena activa: una línea, o nada

Debajo del nombre de página, **size 1**. Color: tinta mezclada con el fill (no `mute` crudo: en paletas como Night el mute sobre el ámbar no se lee). Una sola línea. No wrap.

- Solo si la página tiene `recall_scene` y hay una escena de esa lista con `status.active` (o el `rid` del último PUT local).
- Luces off, receta toggle, o ninguna de la lista activa → **no se dibuja** la línea (no “Off”, no “—”).
- El nombre viene en el poll (`targets[].name`, el de Hue). El aparato no adivina.

Overflow (en este orden):

1. Pintar size 1, centrado, en la cuerda de esa Y menos 8 px de margen.
2. Si el string no entra, truncar y poner ellipsis (`Relax eveni…`). Típico: ~16–18 caracteres.
3. NVS guarda como mucho 24 caracteres; el paint recorta a lo que cabe.
4. Si ni 8 caracteres + ellipsis entran (casi imposible): omitir la línea. El nombre de página y los puntos no se achican para hacerle sitio.

No se usa size 2 para la escena (el título es la página). No segunda línea.

**Layout v1 del estado Ready:**

```text
        · nombre de página (size 2, máx. 12)
        · escena activa (size 1, ellipsis) — solo si hay una puesta
        · puntos de página (2+ páginas)
        · aro de brillo si el destino del dimmer es light / grouped_light
```

**Sin copy de instrucciones** en Ready: nada de `Tap to toggle`, `Tap to cycle scenes`, `Drag ring to dim`, `Double-tap to turn off`, `Assign in console`. El disco no enseña a usarse. Los estados de sistema (Wi-Fi, pairing, error) sí llevan una línea de estado, no un how-to.

Al hacer swipe, los puntos se actualizan **ya** (el lleno se corre). No hace falta animar el resto del disco.

Estados de sistema (Wi-Fi, pairing, error) **no** son páginas. Siguen a pantalla completa, sin swipe ni puntos.

Una sola página: no se dibujan puntos, el swipe no hace nada visible (gesto ignorado). El nombre igual se muestra (deja de ser el `1` del canal).

---

## 6. Gestos

El CHSC6X entrega INT + un punto. Todo lo de esta sección es firmware.

### 6.1 Clasificación por zona de **inicio**

| Dónde empieza el dedo | Qué es |
| --- | --- |
| Disco interior | Tap, doble, o swipe de página |
| Aro | Dimmer. Nunca cambia de página, aunque el dedo se cuele al centro |

Un trazo se clasifica al bajar. No se reconvierte a mitad de gesto.

### 6.2 Centro — recetas

| Gesto | Evento de receta | Condición |
| --- | --- | --- |
| Tap | `short` | Poco movimiento al soltar. Si doble **tiene receta**, espera ~350 ms por un segundo tap |
| Doble tap | `double_click` | Segundo tap en la ventana, mismo disco. Solo si doble tiene receta |

Hueco sin receta: no-op. **No** hay fallback `double_click` → `on` en el círculo (eso es del contacto `maintained` de pared).

Si `double_click` **no tiene receta**, el tap no espera la ventana del segundo toque. Tener receta en doble es lo que retrasa el tap ~350 ms. No hay checkbox aparte de “activar gesto”.

El tap **deja de dispararse al down**. Hoy `uiOnTap()` corre al poner el dedo; con doble/swipe eso es incorrecto. Disparo al **up**.

**No hay hold en el círculo.** Un reloj de “quieto ≥ 600 ms” pelea con el swipe lento y con el CHSC6X (el firmware ya tiene que adivinar el lift: sin punto 80 ms, INT alto 400 ms). El swipe se decide **solo por desplazamiento** en el eje, en cuanto cruza el umbral, sin esperar un timer. Off va en `double_click`. El hold de **3 s en BOOT** (re-pair) no cambia: es el botón físico del XIAO.

### 6.3 Centro — swipe de página

No es receta. No llama al Bridge.

- Eje del **aparato**: `horizontal` (default) o `vertical`.
- Recorrido mínimo en el eje ≈ 40 px. En cuanto se cruza, es swipe (no se espera a soltar ni a ningún hold). El eje ortogonal se ignora (un tap tembloroso no pasa de página).
- Dirección: swipe **left** → página siguiente (índice +1); right → anterior. Si el eje es vertical: up → siguiente, down → anterior.
- Wrap: última +1 → primera, y al revés.
- Una página: el gesto no hace nada.
- Feedback: al cambiar, se pinta la página nueva de inmediato. Animación de slide: no es requisito v1.
- El índice activo se guarda en NVS.

### 6.4 Aro — dimmer

Posición angular 1–100, PUT al soltar. Destino = **set de dimmer de la página** (§8.2), no un slot extra.

- Destino **grupo**: un PUT a `grouped_light` (el Bridge, como la app, suele tocar solo las on; si el grupo está off, el drag prende).
- Destino **luces de las acciones**: GET de esas luces; PUT de `dimming` solo a las `on`. Si ninguna está on, PUT `on` + `dimming` a todas las del set. El % del aro es **absoluto** (todas las on quedan en el mismo 1–100), no un scale relativo.
- Sin destino → no hay aro.

El swipe de página **no** vive en el aro. El aro no se “configura” como gesto de receta.

### 6.5 Fuera de páginas

- BOOT físico del XIAO, hold 3 s → re-pair Hue. No es gesto de pantalla. No es receta.
- Estados Wi-Fi / pairing / error: el disco no ejecuta recetas ni cambia de página.

---

## 7. Qué se ve

El disco en Ready **no** lleva frases de ayuda. Solo nombre, escena si hay, puntos, aro.

| Situación | Título | Debajo |
| --- | --- | --- |
| Tap → toggle / on / off | nombre de página | (nada) |
| Lista de escenas, una activa | nombre de página | nombre Hue de esa escena (ellipsis) |
| Lista de escenas, ninguna activa / luces off | nombre de página | (nada) |
| Página sin recetas | nombre de página | (nada) |
| Varias páginas | nombre + puntos (lleno = activa) | escena si aplica |

El dígito `1` del canal deja de ser el título.

Nombres de página > 12 caracteres: la consola avisa; el aparato trunca con ellipsis. Escenas: §5.2. ASCII: tildes/ñ se pliegan. Sin saltos de línea.

Estados de sistema (inglés, una línea): `Wi-Fi...`, `No Wi-Fi`, `No Bridge`, `Press Bridge button`, `Hue error`. No son páginas.

---

## 8. Recetas por página

Una receta sigue siendo acción Hue + destino(s). La clave deja de ser `(channelId, event)` en el Round y pasa a ser `(pageId, event)`.

| Evento | Default de acción Hue | Destinos (siempre del grupo de la página) |
| --- | --- | --- |
| `short` | `toggle` | `grouped_light` del grupo; o una luz **hija**; o lista de escenas del grupo |
| `double_click` | `off` | `grouped_light` del grupo (típico); o una luz hija; o otra lista de escenas del grupo |

Vacío = no hace nada. Guardar incompleto es válido (solo tap, sin doble). Un destino fuera del grupo lo rechaza la consola.

**Dimmer:** el usuario no elige un slot “aro”. La consola calcula el set al guardar (§8.2).

### 8.1 Lista de escenas (rotar)

Clip v2 no tiene “siguiente escena”. `recall_scene` en Round es **una lista ordenada** de 1–8 `rid` de tipo `scene`.

| Lista | Comportamiento al usar el gesto |
| --- | --- |
| 1 escena | PUT `recall.active` a ese `rid` (igual que hoy) |
| 2–8 escenas | aplica la **siguiente** de la lista (wrap a la primera) |

Reglas:

- Todas las escenas de una lista pertenecen al **grupo de la página**. La consola no muestra (ni deja) escenas de otro room/zona.
- El **orden lo edita el usuario** en la consola (agregar, quitar, drag / flechas). Ese orden es el del ciclo.
- Off **no** entra en la lista. No existe “scene off” en Clip v2 (`recall.action` es `active` \| `dynamic_palette` \| `static`). Apagar es **doble tap** → `off` del `grouped_light` de ese grupo.
- Smart scenes (`smart_scene` / `deactivate`) quedan fuera de v1.
- Una escena que desaparece del snapshot se marca stale en la consola; el firmware la salta.
- El círculo **sí** muestra la escena activa debajo del nombre de página (§5.2). El título sigue siendo el de la página.

Al disparar el gesto, el aparato:

1. Mira cuál de la lista está activa en el Bridge (`status.active` distinto de `inactive`). Caso típico: un GET a la última recordada; si sigue activa, no hace falta recorrer las demás.
2. Si hay una activa → PUT de la **siguiente** en el orden de consola (la última envuelve a la primera).
3. Si ninguna está activa (luces apagadas, o la app Hue puso otra escena) → PUT de la **primera**.
4. El PUT es el de siempre: `PUT /clip/v2/resource/scene/{rid}` `{"recall":{"action":"active"}}`.

El dedo no espera a Vercel. El GET+PUT al Bridge (LAN) **sí ocurre**; el círculo **no se congela** mientras tanto: tap, doble, aro y swipe siguen vivos. Detalle: `docs/input-during-hue.md`.

Índice / último `rid` se puede cachear en NVS para ir rápido; la fuente de verdad de “cuál está puesta” es el Bridge, no el índice local (así un cambio en la app Hue no deja el círculo un paso atrás para siempre). Tras el PUT (o al entrar a la página), se pinta el `name` de ese `rid`.

### 8.2 Set de dimmer (aro)

La consola lo calcula **al guardar** (snapshot). El firmware no infiere el room a partir de una escena.

En cada página el poll baja `dim`:

```text
dim: null
  | { mode: "group", rid: "<grouped_light>" }
  | { mode: "lights", rids: ["<light>", "<light>"] }
```

`null` → sin aro.

Reglas, en este orden:

1. **Hay `recall_scene` en tap o doble** → `mode: "group"` (el `grouped_light` de la página). Una escena pinta el cuarto; el aro es el slider de la app Hue.
2. **No hay escenas, y alguna receta apunta al `grouped_light` del grupo** (p. ej. tap = velador, doble = off Living) → `mode: "group"`.
3. **No hay escenas ni acción de grupo: solo luces hijas** (p. ej. tap = Velador 1, doble = Velador 2) → `mode: "lights"`, `rids` = esas luces, sin duplicar. El aro **no** toca el plafón ni el resto del room.
4. Nada dimmable → `null`.

Comportamiento al soltar el aro (1–100 absoluto):

| `dim.mode` | PUT |
| --- | --- |
| `group` | un PUT `dimming` al `grouped_light`. El Bridge deja off las que ya están off; si el grupo está todo off, el drag **prende**. |
| `lights` | GET de esos `rid`. PUT `dimming` solo a las `on`. Si **ninguna** está on → PUT `on` + `dimming` a todas las del set. |

Un rid stale (404) se salta, igual que una receta huérfana. Dos luces: como mucho dos PUT a `/light` (cabe en el límite del Bridge).

---

## 9. Consola

Al seleccionar un Round Display (no un simple-switch), la columna izquierda **no** lista GPIO. Lista **páginas** de ese aparato.

### 9.1 Ajustes del display (una vez por aparato)

- **Page swipe:** `Left / right` (default) o `Up / down`.
- El label del switch (`switches.label`) sigue siendo el nombre del aparato en la lista de la consola. **No** se pinta en el círculo. El círculo muestra el nombre de la **página**.

### 9.2 Lista de páginas

- Agregar página.
- Eliminar (con confirmación). Se van las recetas de esa página.
- Reordenar (drag o flechas). El orden de la lista **es** el orden del swipe.
- Elegir una página para editarla.

Mínimo 1 página (no se puede borrar la última: queda vacía, asignable). Máximo **6**.

Página nueva: hay que **elegir el grupo** (room/zona). Nombre default = nombre Hue del grupo recortado a 12 / ASCII (editable). Theme `ember`. Tap y doble vacíos. `dim` null hasta que haya recetas.

### 9.3 Editor de una página

- **Group** — room o zona, obligatorio. Cambiar el grupo **limpia** recetas que ya no pertenezcan (aviso). La columna de topología **solo** muestra luces hijas y escenas de ese group.
- **Name** — input, máx. 12 caracteres. Es lo que se ve en el círculo.
- **Theme** — picker visual de **diales redondos**, el mismo lenguaje que `hue-round-switch/docs/round-themes.html` (no el dropdown CSS del sitio). Una paleta por página: click en el círculo la elige (anillo de seleccionado). On/Off en el preview para ver luz prendida vs apagada. El nombre en el dial de muestra puede ser el de la página. El usuario no edita hex.
- **Slots de receta** — siempre **Tap** y **Double tap**, asignables o vacíos. Click en la topología filtrada. Defaults según §8. Vacío = no-op; si doble está vacío, el tap no espera.
- **Lista de escenas** — click en una escena del grupo la agrega; click de nuevo la saca; reordenar. Máx. 8. Off no es ítem de la lista.
- Frase de confirmación, p. ej. *“Living · tap → cycle Relax, Bright, Night · double-tap → turn off Living · ring dims Living (on lights)”* o *“Lamps · tap → Velador 1 · double-tap → Velador 2 · ring dims those lights”*.

### 9.4 Simple-switch

Sin cambios de UI: canales `boot` / `d0` / `d1` / `d2`, eventos `on` / `off` / `double_click` / `short`. El círculo no añade `hold` al schema compartido.

La consola distingue el producto por lo que registra el firmware (`product: "round"` vs canales GPIO). Un Round viejo que aún manda solo `c1` se trata como Round de una página (migración, §13).

---

## 10. Themes del círculo

Conjunto **cerrado** de **20 paletas** para el GC9A01. Cada paleta nombra colores RGB565, no un CSS de la web. Las 20 quedan; no se recorta el set.

Especificación visual del picker (y de las hex de prototipo): `hue-round-switch/docs/round-themes.html`. En la consola es el **mismo dial**: fondo, disco interior, aro de dimmer 270°, nombre de página, escena de muestra si aplica, puntos. **Sin** `Tap to cycle scenes` ni otros how-to. En el prototipo los 3 puntos son de muestra; **en el aparato son de verdad**: N puntos = N páginas, el lleno = página activa. Diferencias respecto al HTML suelto:

| Prototipo | Consola (editor de página) |
| --- | --- |
| Keep / varios a la vez | **Una** paleta por página. Click = elegir. El seleccionado lleva el anillo |
| Copy kept ids | no existe |
| Título “Living” fijo | usa el **nombre de la página** si ya hay |
| All On / All Off | un toggle On/Off de preview (o por dial), para juzgar luz prendida |
| Página standalone | embebido en el editor de página, diales un poco más chicos si hace falta |

Campos por paleta:

| Rol | Dónde se usa |
| --- | --- |
| `bg` | Fondo |
| `ink` | Texto e iconos con luz on |
| `mute` | Texto secundario, luz off |
| `fillOn` / `fillOff` | Disco interior |
| `accent` | Aro y estado “on” / pressed |
| `ringTrack` | Pista vacía del dimmer |
| `error` | Wi-Fi fail, Hue error (puede ser compartido) |

Lista v1 (id estable, copy en inglés):

| id | Name | Blurb |
| --- | --- | --- |
| `ember` | Ember | Current firmware. Charcoal, amber. **Default.** |
| `night` | Night | OLED black, electric amber. |
| `coal` | Coal | Almost off. Faint ember ring. |
| `graphite` | Graphite | Zinc, champagne. |
| `ink` | Ink | Blue-black, teal. |
| `ocean` | Ocean | Navy, sky cyan. |
| `nord` | Nord | Polar night, frost. |
| `plum` | Plum | Espresso, rose. |
| `violet` | Violet | Deep purple, lavender. |
| `dracula` | Dracula | Purple, pink. |
| `matrix` | Matrix | Black, phosphor green. |
| `forest` | Forest | Dark green, gold. |
| `copper` | Copper | Warm metal, rust. |
| `snow` | Snow | Cool white, blue. |
| `paper` | Paper | Cream, filament. |
| `sand` | Sand | Warm beige, terracotta. |
| `linen` | Linen | Ivory, olive. |
| `meadow` | Meadow | Sage, leaf. |
| `sky` | Sky | Pale blue, azure. |
| `porcelain` | Porcelain | Blush, rose. |

Página nueva: `ember`. El usuario no edita hex. Un `theme` desconocido en config se trata como `ember`.

Estados de sistema (pairing, error) pueden ignorar el theme de página y usar la paleta de sistema (`ember` + rojo de error), para no pintar un error “bonito” que no se lea.

---

## 11. Contrato aparato ↔ consola

Sigue habiendo `POST /api/device/register` y `GET /api/device/config?mac=`. El GPIO (simple) no espera este GET. El círculo tampoco: NVS → Bridge.

### 11.1 Registro (Round)

Además de MAC, firmware, bridge, snapshot:

```text
product: "round"
```

Ya no hace falta inventar un canal `c1` / gpio `0` para satisfacer a la consola. `channels` puede ir `[]`. La consola no asigna recetas a pines en este producto.

`hue-simple-switch` no manda `product` (o manda `"simple"`). Se infiere por canales GPIO si falta el campo.

### 11.2 Config que baja al Round

Hoy: `{ rev, recipes[] }` con `channelId`. El Round necesita además `pages[]` con **grupo**, `dim`, nombres, themes, eje y recetas. Eso **sí** baja al aparato (el `switches.label` no).

```text
{
  rev: 12,
  product: "round",
  pageSwipeAxis: "horizontal",          // o "vertical"
  pages: [
    {
      id: "p1",
      name: "Living",
      theme: "ember",
      group: { rtype: "room", rid: "living-room-…", groupedLightRid: "living-gl-…" },
      dim: { mode: "group", rid: "living-gl-…" }
    },
    {
      id: "p3",
      name: "Lamps",
      theme: "night",
      group: { rtype: "room", rid: "living-room-…", groupedLightRid: "living-gl-…" },
      dim: { mode: "lights", rids: ["velador-1-…", "velador-2-…"] }
    }
  ],
  recipes: [
    {
      pageId: "p1",
      event: "short",
      action: "recall_scene",
      targets: [
        { rtype: "scene", rid: "relax-…", name: "Relax" },
        { rtype: "scene", rid: "bright-…", name: "Bright" },
        { rtype: "scene", rid: "night-…", name: "Night" }
      ]
    },
    { pageId: "p1", event: "double_click", action: "off", target: { rtype: "grouped_light", rid: "living-gl-…" } },
    { pageId: "p3", event: "short", action: "toggle", target: { rtype: "light", rid: "velador-1-…" } },
    { pageId: "p3", event: "double_click", action: "toggle", target: { rtype: "light", rid: "velador-2-…" } }
  ]
}
```

El orden de `pages[]` **es** el orden del swipe. El orden de `targets[]` en un `recall_scene` **es** el orden del ciclo. `id` de página estable (no se recicla al borrar). `rev` sube al guardar páginas, recetas, grupo, theme, nombre o eje.

`recall_scene` usa `targets` (1–8 escenas del `group` de la página, con `name`). `on` / `off` / `toggle` usan un `target` que es el `grouped_light` de la página o una luz hija. El firmware no acepta `target` suelto en `recall_scene`.

El firmware, si `rev` remoto > local, **reemplaza** páginas + recetas + eje (no es un patch). Igual que hoy con el array de recetas.

Simple-switch: el GET sigue siendo `{ rev, recipes[] }` con `channelId`. Ignora `pages` si algún día viniera.

### 11.3 Eventos legales

| Producto | Eventos de receta |
| --- | --- |
| Round, por página | `short`, `double_click` |
| Simple, por canal `maintained` | `on`, `off`, `double_click` |
| Simple, por canal `momentary` | `short` |

Round no introduce eventos nuevos respecto al schema actual. `double_click` en el círculo es el hueco de off (u otra receta), no un hold.

---

## 12. Datos (consola)

Hoy `recipes` es unique `(switch_id, channel_id, event)` y `event` está checkeado a `on | off | double_click | short`. Eso no modela páginas.

Necesario:

- Discriminar producto en `switches` (`product` text: `simple` | `round`).
- Ajustes de Round en el switch: `page_swipe_axis`.
- Tabla (o JSON) de **páginas** por switch: `id`, `name`, `sort_order`, `theme`, `group` (room/zone + grouped_light rid), `dim` (`group` \| `lights` \| null).
- Recetas del Round ligadas a `page_id` + `event` (`short` | `double_click`).
- `recall_scene` en Round guarda **varios** `rid` ordenados (tabla hija o JSON), no un solo `target_rid`.
- Recetas del simple-switch se quedan como están (un `rid` por receta; no rotan).

El detalle SQL lo define quien implemente; este documento exige el modelo, no el DDL.

Límites que el server valida:

| Límite | Valor | Por qué |
| --- | --- | --- |
| Páginas por Round | 1–6 | Puntos + NVS + uso de pared |
| Nombre | 1–12 caracteres | Ancho del disco a size 2 |
| Theme | id del set cerrado | RGB565 predecible |
| Gestos | tap y doble siempre listos; receta opcional | Vacío = no-op; sin checkbox extra |
| Recetas | ≤ 12 (6×2) | Cabe en NVS; firmware puede dejar `kMaxRecipes` en 16 o bajar |
| Escenas por lista | 1–8 | Ciclo usable en pared; JSON/NVS |
| Grupo por página | 1 room o zona, obligatorio | Filtra luces y escenas |
| Luces en `dim.mode=lights` | las de tap/doble, ≤ 2 | Un PUT por luz on |

---

## 13. Firmware (Round)

- Poll igual: sin recetas/páginas ~1 min; con config al boot y cada 1 h. El dedo no espera.
- NVS guarda `rev`, eje, páginas (id, nombre, theme, group, `dim`), recetas (listas de escenas con `rid` + `name` ASCII), índice de página activa, último `rid` de escena por gesto (caché).
- Aro `mode: lights`: GET de esos rid + PUT a las on (o prender el set si todas off). No copiar esos GET en el stack del loop.
- Rotar escenas: GET de estado al Bridge + PUT de la siguiente (LAN). No llama a la consola.
- Al cambiar de página: pintar de inmediato, GET de estado Hue del nuevo destino (on/brillo) en background. Un swipe no se bloquea a la red.
- GET/PUT de receta y dimmer **tampoco** bloquean el loop de toque. Last-wins si llega otro gesto. Ready no usa `UI_BUSY`. Ver `docs/input-during-hue.md`.
- Si el `pageId` de la receta ya no existe: ignorar. Si el índice activo apunta a una página borrada: ir a la primera.
- Si cambia el `bridgeid` emparejado: tirar páginas y recetas (los `rid` son de otro Bridge), dejar una página vacía default.
- API key revocada: el poll falla; lo que hay en NVS **sigue** ejecutándose en la LAN.
- `kMaxRecipes` hoy es 16; con 6×2 (tap + doble) 16 sigue sobrando.

### Capacidad medida (XIAO ESP32-S3, `default_8MB`)

Compilación actual (`arduino-cli compile --profile xiao-s3`, firmware 0.3.0), **antes** de páginas:

| Recurso | Usado | Tope | Margen |
| --- | --- | --- | --- |
| App flash (`app0`) | 1 188 458 B (35%) | 3 342 336 B (0x330000) | ~2,05 MB |
| SRAM estática (Arduino “globales”) | 57 964 B (17%) | 327 680 B | ~270 KB heap |
| PSRAM OPI 8 MB | 0 (no se usa) | 8 MB | no hace falta |
| NVS | IP/key Hue + JSON de recetas + Wi‑Fi del core | 20 KB (`0x5000`) | el único sitio justo |

Partición: `nvs` 20 KB, `app0`/`app1` 3,19 MB c/u (OTA cabe con el binario actual), `spiffs` 1,5 MB vacío.

Extra estimado de páginas (6 páginas, 12 recetas, listas de hasta 8 escenas, paletas, máquina de gestos, UI de nombre/puntos):

| Recurso | Extra | ¿Cabe? |
| --- | --- | --- |
| Flash de código | ~20–40 KB | sí, sigue ~36% |
| SRAM estática | ~2 KB (páginas + recetas 24 × ~100 B) | sí |
| Heap en poll | JSON de config ~4–6 KB con listas de escenas; snapshot de registro ya era el pico | sí |
| NVS `putString` | un string ≤ **4000 B** (límite `nvs_set_str`) | sí si se parte: recetas en `json`, páginas en otra key. Listas de escenas pueden empujar el JSON; partir por página si se acerca a 4000 B |

El S3 **tiene espacio de sobra** en flash y RAM. No hace falta framebuffer ni PSRAM. El único cuidado de implementación: no meter páginas+recetas en **un** `Preferences.putString` que se acerque a 4000 B; dos keys. El cuello de producto sigue siendo el disco de 176 px, no el chip.

---

## 14. Migración

Aparatos Round que ya registraron `c1` + receta `short`:

1. La consola los marca `product: round`.
2. Crea una página `p1`, theme `ember`, tap/doble según la receta migrada.
3. Copia `c1`/`short` a `p1`/`short`. Infere `group` del target (luz → su room; escena → su `group`; `grouped_light` → ese). Si no se puede inferir, la página queda sin grupo hasta que el usuario elija uno en consola (sin aro, recetas stale). `dim` según §8.2.
4. El siguiente poll con `rev` nuevo baja el objeto de §11.2.
5. Firmware viejo (sin parser de `pages`): **no se le puede mandar solo recipes con `pageId`**. Hasta flashear, o bien se dual-escribe `{ channelId: "c1", … }` (compat) o el aparato se queda con NVS viejo hasta el flash. v1 asume **flash de firmware junto con el corte de consola**. No hay requisito de dual-stack largo.

Simple-switch: cero migración.

---

## 15. Fuera de alcance (v1)

- Triple tap.
- Hold en el círculo (gesto de receta). Off es doble tap. El hold de 3 s en BOOT (re-pair) se queda.
- Swipe como receta Hue. El swipe **solo** cambia de página. Rotar escenas es el gesto de receta (`recall_scene` con lista), no un swipe.
- Meter “Off” como ítem del carrusel de escenas.
- Smart scenes (`smart_scene`) en la lista.
- Pinch / multi-touch (el chip es un dedo).
- Más de 6 páginas.
- Nombre de página editado **en el círculo**.
- Theme con hex libre.
- Animación de slide entre páginas.
- Reloj / screensaver / widgets en una página.
- Páginas compartidas entre varios aparatos.
- Un Round hablando con dos Bridges.
- Cambiar `hue-simple-switch` ni su máquina de double-click de GPIO.

---

## 16. Decisiones propuestas (para no dejar el doc hueco)

Estas no son código. Son el default si no se dice lo contrario.

1. **Páginas ≠ canales GPIO.** La consola inventa páginas; el firmware no declara pines falsos.
2. **Máximo 6 páginas.** El disco muestra puntos; 8 ya aprieta.
3. **Nombre ≤ 12 caracteres**, size 2. Más largo → ellipsis.
4. **Eje de swipe por aparato**, default horizontal.
5. **Swipe left = siguiente** (índice +1). Right = anterior. Vertical: up = siguiente, down = anterior.
6. **Wrap** en el carrusel.
7. **Última página activa** se recuerda en NVS. Un reboot vuelve a esa.
8. **Página = un room o zona.** Recetas solo de ese grupo. **`dim` al guardar** (§8.2): escena o acción de grupo → `grouped_light`; solo luces hijas → esas luces; si no, sin aro. El aro no es un slot.
9. **Sin fallback** `double_click` → otra receta en el círculo.
10. **Themes = las 20 paletas del prototipo**, set cerrado. Picker = grid de diales redondos (como `docs/round-themes.html`). Una por página. Default `ember`. No es el theme CSS del sitio.
11. **Sin hold en el círculo.** Off vive en `double_click`. El swipe se decide por desplazamiento, no por reloj. BOOT hold 3 s (re-pair) no cambia.
12. **Tap al soltar**, nunca al down.
13. **Clasificación por zona de inicio** (centro vs aro) para no pelear swipe con dimmer.
14. **`recall_scene` = lista ordenada** (1–8) del mismo room/zona. Repetir el gesto rota. Off es `grouped_light` en doble tap, no una escena.
15. **Orden de las escenas = orden en la consola.** El usuario lo edita (drag / flechas).
16. **Debajo del nombre de página, la escena activa** (size 1, ellipsis, o nada si no hay). El título sigue siendo la página. El poll manda `targets[].name`.
17. **Puntos abajo = páginas.** Un punto por página, orden de consola. El lleno es la activa. Una página → sin puntos. Una fila dentro del disco interior: se achica gap, luego diámetro; nunca overflow ni dos filas.
18. **Sin textos explicativos en Ready.** Nada de `Tap to toggle` ni `Drag ring to dim`. El disco no enseña gestos. Estados de sistema sí tienen una línea de estado.
19. **Página nueva: tap y doble visibles y vacíos.** Se asigna en consola o se deja vacío. Sin checkbox de gesto. El tap espera doble solo si doble tiene receta.
20. **Nombres en el círculo = ASCII.** Página y escena: tildes/ñ se pliegan (`Niños` → `Ninos`). Fuente built-in 5×7.
21. **Aro como la app Hue:** solo luces on del destino. Si el destino está todo off, el drag prende a ese %. `mode: lights` usa % absoluto en cada luz on, no un scale relativo.
22. **Tap = velador y doble = off del grupo** → `dim.mode = group` (regla 2), no las luces sueltas.
23. **Toques mientras Hue responde:** el disco sigue aceptando input. Ack optimista (invert, fill, escena, aro). Last-wins. Sin `UI_BUSY` en Ready. `docs/input-during-hue.md`.
24. **Tap = luz A y doble = luz B** (dos `light` distintas): el fill del disco se parte (izquierda = tap, derecha = doble). El área táctil y los gestos no cambian. Cada lado guarda el on de su `rid`; el toggle no usa un bit único de página. Cualquier otro layout: disco entero.

---

## 17. Preguntas abiertas

Ninguna. Cerradas:

1. Swipe left = siguiente.
2. Página nueva: tap y doble siempre asignables (vacíos al crear).
3. Reboot vuelve a la última página.
4. ASCII en el círculo.

---

## 18. Criterio de hecho

Esta feature está **lista** cuando:

- En la consola, un Round tiene páginas ancladas a un room/zona, que se agregan, borra, reordenan, nombran y colorean.
- Tap y doble solo ven luces/escenas de ese grupo.
- Un gesto de escena acepta una lista ordenable de **ese** grupo; cada uso aplica la siguiente. Off es el grupo en doble tap, no una escena.
- El aro dimmea el grupo si hay escenas o acción de grupo; si solo hay luces hijas, dimmea esas (on only).
- El círculo muestra el nombre de la página, debajo la escena activa si hay (sin how-to), puntos si hay más de una (lleno = en cuál estoy, sin salirse del disco), y el aro si toca.
- En la consola, el theme de cada página se elige en un grid de diales redondos (las 20 paletas).
- Un swipe en el eje configurado cambia de página **sin** llamar a Vercel ni al Bridge.
- Tap / doble de esa página ejecutan NVS → Bridge **sin** congelar el toque (`docs/input-during-hue.md`). Swipe cambia de página. No hay hold de pantalla.
- Un simple-switch en la misma consola sigue viéndose como canales GPIO.

La revisión de grupo + `dim.mode` no está en el firmware/consola actuales hasta que se implemente. El resto de páginas v1 ya corre.
