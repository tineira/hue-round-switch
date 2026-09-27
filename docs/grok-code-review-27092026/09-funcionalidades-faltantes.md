# Funcionalidades faltantes — round

## Producto

- **OTA.** Spec en consola abierto. Este firmware no baja bins. Reflash USB.
- **HUEBOOT.** Si el wizard lo manda, Round no entra a ROM download. Hoy el path de producto usa DTR. Confirmar con `/setup` antes de implementarlo.
- **Más de 2 luces en dim `mode:lights`.** Cap rígido. Casas que eligen “las luces del tap y del double” están cubiertas (son 2). Un dim de 4 child lights se recorta.
- **Más de 6 pages.** Consola + firmware topean; si el producto quiere 8, hay que mover ambos.

## Contrato / consola

- Decidir P11 timeout (bump vs excepción documentada).
- Hidden SSID en scan Improv (Simple sí, Round no).
- Firma de bins / secure boot: ninguno de los dos firmwares lo tiene. El installer de Chrome escribe lo que la consola sirva.

## DX / ops

- Cerrar Serial Monitor resetea el S3. No hay workaround de software. Documentado en AGENTS; el how-to de usuario debería repetirlo.
- `stability.md` §7 checklist de hardware sigue sin tildar en el spec. Esta review no midió en placa.
- Tests de parse / arm recipe / dim lights: no hay.
- Métrica heap/FPS en `HUEGET`: no hay.

## Fuera de alcance (y está bien)

- GPIO de receta (Simple).
- SoftAP.
- Zigbee / cloud Hue.
- LVGL / TFT_eSPI.
