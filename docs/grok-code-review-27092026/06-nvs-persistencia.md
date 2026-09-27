# NVS — round

Namespaces: `console`, `hue`, `recipes`, `pages`. STA en NVS del driver. Flash del app no borra NVS.

## console

Buffers C de 128. Load al boot. `consoleForget` clear + epoch++.

## hue

Igual que Simple: `ip`, `key`, `bid`. El hueJob es el único escritor en runtime. HUECLR llama `hueForgetSaved` y `gHueClrEpoch++` para que el job no re-guarde.

## recipes

No hay un jsonb único. Se parte en `json` / `j1` / `j2` / `j3` para no pasar `kNvsStrMax = 3900` por `putString`. `recipesSaveJson` va metiendo recipes hasta el tope de cada chunk.

| Key | Contenido |
| --- | --- |
| `rev` | uint |
| `bid` | bridgeid |
| `json` `j1` `j2` `j3` | arrays de recipes |

Caps: 16 recipes, 8 scenes, nombre de escena fold 24 chars. Eventos solo `short` / `double_click` (no `hold` — eso es Simple).

`recipesBindBridge`: mismatch → clear + `gRecipesBidReset = true` (el siguiente poll omite `&rev=`).

## pages

Namespace propio para no mezclar con recipes (un `putString` tiene que quedar &lt; 4000).

| Key | Contenido |
| --- | --- |
| `json` | array de pages (id, name, theme, group, dim) |
| `axis` | `horizontal` / `vertical` |
| `sto` | `screenTimeoutSec` |
| `bid` | bridgeid |
| `idx` | page index |
| `s0`…`s5` | last scene rid por page |

`pagesFlushLazy`: cursor e index se escriben a los 5 s o al sleep (`force`). No un write por tap.

`pagesEnsureDefault`: si count=0, page `p1` “Page 1” theme ember en RAM. Tras HUECLR el default vive solo en RAM (namespace vacío).

## MED

- `pages` `json` es un solo `putString`. 6 pages con group + dim lights puede acercarse a 4000. No hay split como recipes. Si `putString` trunca, el load parsea un JSON cortado y puede quedar en default `p1`.
- `kMaxDimLights = 2` / `kMaxPages = 6`. Consola puede mandar más; extras se droppean. El usuario no ve error en la placa.
- `recipesClear` pone rev=0 (mismo patrón que Simple). Consola bump-ea.
- Save pages + recipes son dos begins distintos. Un reset a mitad puede dejar pages nuevas con recipes viejas. El restore-on-parse-fail ayuda si el body no parsea; no si parsea a medias y el segundo save no corre. `consoleApplyConfig` llama `recipesSave()` y luego `pagesSave()` seguidos.

## GOOD

- Recipes partidas.
- Flush lazy de cursores.
- Bind bridge drop **antes** del poll.
- Parse fail → reload NVS.
- Timeout se puede persistir solo (`pagesSaveTimeout`) cuando el rev no cambia.
