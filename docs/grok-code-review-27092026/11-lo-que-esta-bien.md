# Lo que no hay que romper — round

1. **Hue HTTP fuera del loop de UI.** Todo Clip pasa por `hueJob`. No “un GET rapidito” desde `ui.h`.
2. **Ownership:** loop dueño de pages/recipes NVS; hueJob dueño del Bridge; consoleJob dueño de Vercel.
3. **Cola de config de 1 + apply en el loop.** Drenar siempre (`consoleApplyConfigIfReady` también con Wi-Fi down).
4. **Snapshot abort si Clip ≠ 200.** No POSTear `[]`.
5. **product `"round"` + `channels: []`.** Nunca inventar `c1`.
6. **Token shape check + TLS consola verificado.**
7. **`HUEGET` sin secretos.**
8. **401 no borra NVS** y no bloquea el touch si hay pages.
9. **`gConsoleEpoch` / `gHueClrEpoch`** tiran trabajo in-flight post-HUECLR.
10. **Token copiado bajo mux** antes del HTTP.
11. **Parse fail restaura NVS** (`recipesLoad` + `pagesLoad`).
12. **Flush lazy de cursores** (no write NVS por tap).
13. **Primer toque tras idle solo despierta.**
14. **Dim ring ≠ target de la recipe.**
15. **CHSC6X scale a 240.** No asumir coords nativas.
16. **BOOT hold 3 s = re-pair**, no recipe. No `pinMode` maintained.
17. **User job last-wins + gen.** Results de la page anterior no se pintan.
18. **Optimistic paint + flash rojo** si el PUT falla. No pantalla modal que coma el siguiente tap.
19. **Scan Improv con ACK inmediato.**
20. **CI: changelog + THIRD_PARTY + SERIAL_DEBUG 0.**
21. **Arduino_GFX, no TFT_eSPI/LVGL.**

P11 timeout (escribir `sto` con rev igual) es la excepción consciente: o se documenta en device-api o se deja de escribir.
