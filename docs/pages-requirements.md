# Round Display — páginas

Documento de **requisitos de producto**. Cubre `hue-round-switch` (firmware, círculo) y `hue-switch-console` (web). No es una guía de implementación ni un changelog.

Copia canónica también en `hue-switch-console/docs/round-pages.md`. Mantener ambos alineados.

**Estado:** requisitos, no implementado. No hay código de páginas hasta que se apruebe este documento.

**Productos:**

| Repo | Rol en esta feature |
| --- | --- |
| `hue-round-switch` | Círculo 240×240. Una página activa a la vez. Gestos de centro + aro de brillo. Swipe cambia de página. |
| `hue-switch-console` | CRUD de páginas, recetas por gesto (listas de escenas ordenables), eje de swipe, theme, nombre. Baja todo en el poll de config. |
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

Una **página** es la configuración activa del display: nombre en pantalla, theme de color, gestos de centro habilitados y recetas de esos gestos. El aro sigue siendo dimmer del destino dimmable de esa página.

Varias páginas viven en el mismo aparato. El swipe (eje elegido **por display** en la consola) pasa de una a otra. No es una receta Hue: es navegación local.

```text
Página 1 "Living"                    Página 2 "Patio"
 tap → cycle Relax, Bright, Night    tap → toggle group
 double → off Living                 double → off Patio
 aro → dim Living                    aro → dim Patio
```

Un gesto de escena no apunta a un `rid` suelto: apunta a una **lista ordenada** de escenas del mismo room o zona. Cada uso de ese gesto aplica la siguiente. Off no es una escena: es `off` del `grouped_light` de ese grupo, en **doble tap**.

---

## 4. Conceptos

**Página.** Configuración activa. No es un canal GPIO. La consola las crea, nombra, ordena, colorea y borra. El firmware **no** declara cuántas hay: las recibe en el poll.

**Página activa.** La que se ve y la que recibe gestos. Una sola. El swipe la cambia. Se recuerda en NVS (reboot vuelve a la última).

**Puntos de página.** Abajo del disco interior (como en `docs/round-themes.html`). Un punto por página, en el orden de la consola. El punto **lleno** es la página activa; los demás, apagados. Con una sola página no se dibujan. Cerrado: no es un adorno del prototipo, es el chrome del círculo. **Nunca overflow:** la fila entera queda dentro del disco interior; no se recorta contra el borde redondo ni se mete al aro. Una sola fila, nunca wrap. Ver §5.1.

**Gesto de centro.** Ocurre en el disco interior (radio ≤ 88). Eventos de receta: `short` (tap) y `double_click` (doble tap). Cada página elige cuáles están **habilitados**. Si el doble está apagado, el tap no espera la ventana del segundo toque. **No hay hold** en pantalla.

**Aro / dimmer.** Disco exterior (radio ~96–118). No es receta. Drag + PUT de brillo al soltar, igual que hoy. Destino dimmable: un `light` / `grouped_light`, o el grupo padre de una lista de escenas.

**Lista de escenas.** Receta `recall_scene` con 1–8 escenas del **mismo** room o zona, en el orden que el usuario fija en la consola. Un ítem = el recall de hoy. Varios = rotar. El Bridge no rota: cada toque es un PUT a un `rid`; el aparato elige el siguiente.

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
| Tap | `short` | Poco movimiento al soltar. Si doble está habilitado, espera la ventana (~350 ms) por un segundo tap |
| Doble tap | `double_click` | Segundo tap en la ventana, mismo disco. Solo si la página lo tiene habilitado |

Huecos vacíos (gesto habilitado, sin receta): no-op. **No** hay fallback `double_click` → `on` en el círculo (eso es del contacto `maintained` de pared).

Si `double_click` está **apagado** en esa página, el tap no espera la ventana del segundo toque. Por eso el doble se configura por página.

El tap **deja de dispararse al down**. Hoy `uiOnTap()` corre al poner el dedo; con doble/swipe eso es incorrecto. Disparo al **up**.

**No hay hold en el círculo.** Un reloj de “quieto ≥ 600 ms” pelea con el swipe lento y con el CHSC6X (el firmware ya tiene que adivinar el lift: sin punto 80 ms, INT alto 400 ms). El swipe se decide **solo por desplazamiento** en el eje, en cuanto cruza el umbral, sin esperar un timer. Off va en `double_click`. El hold de **3 s en BOOT** (re-pair) no cambia: es el botón físico del XIAO.

### 6.3 Centro — swipe de página

No es receta. No llama al Bridge.

- Eje del **aparato**: `horizontal` (default) o `vertical`.
- Recorrido mínimo en el eje ≈ 40 px. En cuanto se cruza, es swipe (no se espera a soltar ni a ningún hold). El eje ortogonal se ignora (un tap tembloroso no pasa de página).
- Dirección: en horizontal, swipe left → página siguiente (índice +1); swipe right → anterior. En vertical, up → siguiente, down → anterior. (Por confirmar en Open questions; es convención tipo carrusel.)
- Wrap: última +1 → primera, y al revés.
- Una página: el gesto no hace nada.
- Feedback: al cambiar, se pinta la página nueva de inmediato. Animación de slide: no es requisito v1.
- El índice activo se guarda en NVS.

### 6.4 Aro — dimmer

Igual que hoy en espíritu: posición angular 1–100, PUT al soltar. Destino = **destino de dimmer de la página** (ver §8). Light/group apagado: el aro puede verse apagado (como ahora) y un drag que sube brillo puede prender, según lo que ya haga Clip v2 al PUT de `dimming`.

Si el destino de dimmer sale de una lista de escenas, el aro regula el `grouped_light` de ese room/zona (las escenas prenden el grupo; el aro recorta brillo).

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

Nombres de página > 12 caracteres: la consola avisa; el aparato trunca con ellipsis. Escenas: §5.2. Sin saltos de línea.

Estados de sistema (inglés, una línea): `Wi-Fi...`, `No Wi-Fi`, `No Bridge`, `Press Bridge button`, `Hue error`. No son páginas.

---

## 8. Recetas por página

Una receta sigue siendo acción Hue + destino(s). La clave deja de ser `(channelId, event)` en el Round y pasa a ser `(pageId, event)`.

| Evento | Default de acción Hue | Destinos típicos |
| --- | --- | --- |
| `short` | `toggle` | `light` / `grouped_light`; o lista de escenas (`recall_scene`) |
| `double_click` | `off` | `light` / `grouped_light` del mismo grupo. **No** una escena. También puede ser otra lista de escenas si se quiere |

Vacío = no hace nada. Guardar incompleto es válido (solo tap, sin doble).

**Destino de dimmer de la página (aro):**

1. Si `short` es `light` o `grouped_light` → ese target.
2. Si `short` (u otro gesto habilitado, en ese orden) es `recall_scene` → el `grouped_light` del room/zona de esa lista.
3. Si nada de eso → sin aro.

No hay un slot extra “dimmer” en la consola en v1. El aro no gasta un evento.

### 8.1 Lista de escenas (rotar)

Clip v2 no tiene “siguiente escena”. `recall_scene` en Round es **una lista ordenada** de 1–8 `rid` de tipo `scene`.

| Lista | Comportamiento al usar el gesto |
| --- | --- |
| 1 escena | PUT `recall.active` a ese `rid` (igual que hoy) |
| 2–8 escenas | aplica la **siguiente** de la lista (wrap a la primera) |

Reglas:

- Todas las escenas de una lista pertenecen al **mismo** `group` (mismo `room` o `zone` del snapshot). La consola no deja mezclar Living + Patio.
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

El dedo no espera a Vercel. Sí puede esperar el GET+PUT al Bridge (LAN), igual que un `toggle`.

Índice / último `rid` se puede cachear en NVS para ir rápido; la fuente de verdad de “cuál está puesta” es el Bridge, no el índice local (así un cambio en la app Hue no deja el círculo un paso atrás para siempre). Tras el PUT (o al entrar a la página), se pinta el `name` de ese `rid`.

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

Página nueva: nombre `Page 2` (inglés, editable al tiro), theme default, tap habilitado, doble apagado, sin recetas.

### 9.3 Editor de una página

- **Name** — input, máx. 12 caracteres. Es lo que se ve en el círculo.
- **Theme** — picker visual de **diales redondos**, el mismo lenguaje que `hue-round-switch/docs/round-themes.html` (no el dropdown CSS del sitio). Una paleta por página: click en el círculo la elige (anillo de seleccionado). On/Off en el preview para ver luz prendida vs apagada. El nombre en el dial de muestra puede ser el de la página. El usuario no edita hex.
- **Gestures** — checkboxes: Tap, Double tap. Al menos uno debe quedar on. Deshabilitar doble es lo que hace al tap más inmediato (no espera ~350 ms).
- **Slots de receta** — solo los gestos habilitados. Elegir el hueco, click en la topología de la derecha. Defaults de acción según §8.
- **Lista de escenas** — si el hueco queda en `recall_scene` (click en una escena, o default de doble tap):
  - Click en una escena del **mismo** room/zona la **agrega** al final (si aún no está).
  - Click en una escena que ya está en la lista la **saca**.
  - Click en una escena de **otro** grupo: no se mezcla; aviso en inglés (*“Scenes must be in the same room or zone.”*).
  - Bajo el slot, la lista se **reordena** (drag o flechas). Ese orden es el del ciclo en el círculo.
  - Mínimo 1 escena para que la receta exista; máximo **8**.
  - Off no se ofrece como ítem de la lista. Para apagar: doble tap → el room/zona (`grouped_light`).
- Frase de confirmación en inglés, p. ej. *“Living · tap → cycle Relax, Bright, Night · double-tap → turn off Living · ring dims Living”*.

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

Hoy: `{ rev, recipes[] }` con `channelId`. Eso **no basta**: el círculo necesita nombres, themes, eje de swipe y gestos habilitados. Esos datos **sí** se envían al aparato (al revés del `switches.label`, que no viaja).

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
      gestures: { tap: true, double_tap: true }
    },
    {
      id: "p2",
      name: "Patio",
      theme: "meadow",
      gestures: { tap: true, double_tap: true }
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
    { pageId: "p2", event: "short", action: "toggle", target: { rtype: "grouped_light", rid: "patio-gl-…" } },
    { pageId: "p2", event: "double_click", action: "off", target: { rtype: "grouped_light", rid: "patio-gl-…" } }
  ]
}
```

El orden de `pages[]` **es** el orden del swipe. El orden de `targets[]` en un `recall_scene` **es** el orden del ciclo. `id` de página estable (no se recicla al borrar). `rev` sube al guardar páginas, recetas (incluida la lista/orden de escenas), theme, nombre o eje.

`recall_scene` usa `targets` (array, 1–8, todos `rtype: scene`, mismo group). Cada ítem lleva `name` (string Hue, el aparato recorta a 24 para NVS y luego al ancho del disco). `on` / `off` / `toggle` siguen con un solo `target` (`light` o `grouped_light`). El firmware Round no acepta `target` suelto en `recall_scene`.

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
- Tabla (o JSON) de **páginas** por switch: `id`, `name`, `sort_order`, `theme`, flags de gestos.
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
| Gestos | ≥ 1 habilitado | Página inútil si no hay ninguno |
| Recetas | ≤ 12 (6×2) | Cabe en NVS; firmware puede dejar `kMaxRecipes` en 16 o bajar |
| Escenas por lista | 1–8 | Ciclo usable en pared; JSON/NVS |
| Grupos en una lista | 1 (mismo room o zona) | Clip v2 aplica la escena a su grupo |

---

## 13. Firmware (Round)

- Poll igual: sin recetas/páginas ~1 min; con config al boot y cada 1 h. El dedo no espera.
- NVS guarda `rev`, eje, páginas (id, nombre, theme, flags), recetas (listas de escenas con `rid` + `name`), índice de página activa, último `rid` de escena por gesto (caché).
- Rotar escenas: GET de estado al Bridge + PUT de la siguiente (LAN). No llama a la consola.
- Al cambiar de página: pintar de inmediato, GET de estado Hue del nuevo destino (on/brillo) en background. Un swipe no se bloquea a la red.
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
2. Crea una página `p1`, nombre `Page 1` (editable), theme default, tap on, doble off.
3. Copia la receta `c1`/`short` a `p1`/`short`. Si era `recall_scene` de un `rid`, queda una lista de un elemento.
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
5. **Swipe left / up = siguiente** en la lista de la consola.
6. **Wrap** en el carrusel.
7. **Última página activa** se recuerda en NVS.
8. **Aro dimmea el target de tap** si es dimmable; si tap es lista de escenas, dimmea el `grouped_light` de ese grupo; si no, el primer gesto dimmable; si no, sin aro.
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

---

## 17. Preguntas abiertas

Resolverlas antes de implementar. Los defaults de §16 valen si no hay respuesta.

1. **Dirección del swipe.** ¿Left = siguiente (como un carrusel que empuja el contenido) o left = anterior (como pasar una página de libro)?
2. **¿Página nueva nace con tap-only** (doble off) o con tap y doble visibles y vacíos?
3. **¿Reboot vuelve a la última página** (propuesto) o siempre a la primera?
4. **Nombres en español en el círculo.** Página y escena son del usuario / Hue. ¿Truncar UTF-8 (tildes) o restringir a ASCII para la fuente built-in?

---

## 18. Criterio de hecho

Esta feature está **lista** cuando:

- En la consola, un Round tiene páginas que se agregan, borra, reordenan, nombran y colorean.
- Cada página tiene tap y doble optativos, con recetas al mismo árbol Hue de siempre.
- Un gesto de escena acepta una lista ordenable del mismo room/zona; cada uso en el círculo aplica la siguiente. Off es el grupo en doble tap, no una escena ni un hold.
- El círculo muestra el nombre de la página, debajo la escena activa si hay (sin how-to), puntos si hay más de una (lleno = en cuál estoy, sin salirse del disco), y el aro si toca.
- En la consola, el theme de cada página se elige en un grid de diales redondos (las 20 paletas).
- Un swipe en el eje configurado cambia de página **sin** llamar a Vercel ni al Bridge.
- Tap / doble de esa página ejecutan NVS → Bridge. Swipe cambia de página. No hay hold de pantalla.
- Un simple-switch en la misma consola sigue viéndose como canales GPIO.

Hasta que se apruebe este documento, no hay implementación.
