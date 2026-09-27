# Seguridad — round

Misma amenaza que Simple: USB físico, NVS en claro, TLS split consola/Bridge. El S3 añade PSRAM y HWCDC.

## GOOD

- Token/url no compilados. NVS `console`. Caps RAM: tok/url 128.
- Token `hsw_` + charset + 20–80 al **usar**. URL en `consoleSetUrl` exige `http://` o `https://` y largo &lt; 128.
- Consola HTTPS: CA bundle. HTTP solo si HUESET lo pide.
- Hue Bridge: `setInsecure()` solo LAN (AGENTS lo exige).
- 401 consola sticky (`std::atomic`); NVS recipes/pages se quedan. UI: punto rojo en READY, o pantalla Token si no hay recipes.
- `HUEGET` no vierte secretos (`token=0|1`, `key=0|1`).
- Token se copia bajo `gConsoleMux` a un buffer local **antes** del HTTP. No hay data race obvio con HUESET.
- `consoleForget` incrementa `gConsoleEpoch` para tirar un body in-flight. `HUECLR` además sube `gHueClrEpoch` y notifica `hueJob`.
- Improv checksum. SSID ≤ 32, pass ≤ 64. `Serial.setTxTimeoutMs(100)`.
- No hay SoftAP / WebServer.

## MED

- `http://` permitido en URL. Bearer en claro si alguien lo escribe. Prod usa HTTPS.
- NVS plaintext (USB = posesión física).
- `SERIAL_DEBUG=1` ensucia Improv; CI release = 0.
- Cerrar Serial Monitor **resetea el S3** (HWCDC silicon). No es un bug de app; sí de DX/setup. AGENTS lo documenta.
- URL cap 128: un self-host con path largo falla `consoleSetUrl` (mejor que truncar).
- `hueLooksLikeKey` igual de laxo que Simple.

## LOW

- No hay `HUEBOOT` en Round. El flash usa DTR del mismo COM (Hardware CDC and JTAG). Primer flash de un S3 virgen puede necesitar BOOT físico.
- `usbReplySta` arma una línea de 1280 bytes en stack/static. No filtra key/token.
- Logs de register fallido pueden incluir body de consola.

## No es vulnerabilidad

- Clip `setInsecure` LAN.
- MAC en register / HUESTA.
- Optimistic UI paint antes del PUT (el job puede fallar y el loop deshace). Es UX, no un leak.
