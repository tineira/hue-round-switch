# Testing y ops — round

## CI

| Workflow | Trigger | Qué hace |
| --- | --- | --- |
| `build.yml` | pull_request | compile `xiao-s3`, `SERIAL_DEBUG` 0 generado inline. Sin secretos. |
| `firmware.yml` | push `main` + dispatch | compile, artifact, `POST /api/firmware/round`, **borra y recrea** el release `usb-installer`. |

Versión: se parsea del `.ino` (`#define FIRMWARE_VERSION "0.5.30"`), no de `console.h`. No mover el define sin actualizar el sed del workflow.

Upload: mismo patrón que Simple (token secret, CHANGELOG `### 0.5.30`, THIRD_PARTY vs sketch.yaml). `409` = warning, notes sí, bins no.

Diferencia de release GitHub:

- Simple: `gh release upload --clobber` sobre el tag existente.
- Round: `gh release delete usb-installer` + `git push --delete` + `gh release create`. Más agresivo. Un fallo a mitad deja el tag ausente hasta el retry.

`contents: write` en firmware.yml.

## Tests que no existen

- `recipesParseConfig` (falta `pages`, falta `rev`, timeout only, dim lights &gt; 2).
- `pagesParseTimeout` (0, 9→10, 601→600, missing key).
- `hueJobArmRecipe` cursor / 404 skip (hace falta fake HTTP).
- Touch scale 0–127 → 240.

`stability.md` describe escenarios de input-during-hue. No están automatizados.

## Observabilidad

Producto: `SERIAL_DEBUG` 0. Diagnóstico = `HUEGET` + pantallas + punto rojo de token.

Confirm poll alimenta `applied_rev` en la consola igual que Simple.

## MED

- Log de Actions imprime body de la consola.
- Recreate del release `usb-installer` es racing si dos pushes entran seguidos.
- PR no ejecuta el upload.

## GOOD

- PR compile sin secretos.
- Credits + changelog gate.
- Binario de `/setup` sin logs USB.
