# Contrato vs consola — round

SSOT: `hue-switch-console` `docs/device-api.md`.

## Alineado

| Tema | Firmware |
| --- | --- |
| product | siempre `"round"` |
| channels register | `[]` |
| no `c1` placeholder | sí |
| Bearer `hsw_` | sí |
| GET config + rev | sí (omitido si `gRecipesBidReset`) |
| 204 keep NVS | sí |
| `X-Poll-Sec` | clamp 30–3600, parse estricto |
| snapshot fail | no POST |
| bridgeid change | drop recipes **y** pages antes del poll |
| USB | HUESET / HUEGET / HUEPAIR / HUECLR + Improv |
| pages + recipes en config | parse exige ambos keys |
| dim group / lights | sí |
| screenTimeoutSec | 0 o 10–600 |
| FIRMWARE_VERSION | `0.5.30` en el `.ino` |
| CI | `POST /api/firmware/round` |
| 401 | NVS se queda; UI punto rojo / Token |

## Drift

- **HUEBOOT no existe.** `docs/specs/finished/devices.md` §6 lista HUEBOOT para el setup. Simple lo implementa. Round flashea por DTR/HWCDC. El wizard de consola que mande `HUEBOOT` a un Round recibe `HUEERR unknown` y tiene que usar el reset DTR. Documentar en el spec o implementar no-op `HUEOK boot` si el wizard lo exige.
- **P11 timeout.** device-api: “If local rev ≥ remote, do not write NVS.” Round escribe `sto` igual. Es útil (cambiar solo el timeout sin bump) y choca con la letra del contrato. O la consola bump-ea al cambiar timeout, o el contrato admite “timeout puede llegar sin bump”.
- `kMaxPages=6`, `kMaxDimLights=2`, `kMaxRecipes=16`. La consola valida pages/group; el tope de dim lights en firmware no está en device-api.
- Recipes solo `short` / `double_click`. Un `hold` en el body se droppea (correcto).
- `http://` permitido.
- Scan Improv: Simple pide hidden SSIDs (`scanNetworks(true, true)`); Round no (`scanNetworks(true)`). Setup UX diverge.
- Connect timeout 30 s vs 20 s Simple.

## problems vs código

| ID | Aquí |
| --- | --- |
| P4 | Mitigado en firmware |
| P8 | Cerrado |
| P11 | Vivo para timeout; cerrado para group/dim |
| P12 | Cerrado |
| P21 | Cerrado (whitespace tras `:`) |

## No inventar

Mismo rule que Simple. No extraer lib C6↔S3. Round puede prestar código a un futuro S3 Simple, no al revés.
