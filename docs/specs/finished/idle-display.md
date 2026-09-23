# Round Display — screen sleep

**Product requirements** document. Covers `hue-round-switch` (firmware, circle) and `hue-switch-console` (per-device timeout). `hue-simple-switch` is **out of scope**.

This is not a screensaver (clock, widgets, animation). It is **sleep**: the disc turns off when nobody touches it, to use less power (backlight, and on battery everything else) and not leave a beacon on the wall.

**Status:** implemented (firmware 0.5.11+, console `screenTimeoutSec`). Spec archived. Not an implementation gap.

Aligns with the pages requirements (`hue-switch-console/docs/round-pages.md`) §9.1 (display setting), the §11.2 poll and decision 25.

---

## 1. Verdict

After **X seconds** without a touch in Ready, the circle goes to sleep: **backlight off**, black panel. Ready is not painted at half brightness.

The **first contact** (finger down → lift) **only wakes it**. It does not fire tap, double, ring or swipe. The user doesn't know which page is showing; they need to see the disc (name, fill, scene, dots, ring) and **then** gesture.

The next complete gesture, with the screen on, acts as usual.

The user chooses X in the **console**, once per device (like the swipe axis). **Default 30 s.**

---

## 2. Problem

Ready keeps the GC9A01 and the backlight on D6 lit all day. That is the big cost (and the light at night). The `loop()` and full-power Wi‑Fi add up, but without turning off the disc the savings are cosmetic.

A wall switch that fires a recipe blind, with the disc black, changes the **wrong** page or lamp. That's why wake does not act.

---

## 3. Expected result

| Situation | What happens |
| --- | --- |
| Ready, nobody touches it, X s pass (X > 0) | Sleep: backlight off, black panel. No copy, no `...`, no clock. |
| Asleep, first touch (any zone, any movement) | Backlight on, Ready of the **active page** (the one in NVS) is painted. That contact is **not** a recipe, swipe or dimmer. On lift, the user sees where they are. |
| Ready awake, tap / double / ring / swipe | As usual (`input-during-hue.md` in this folder, split fill, etc.). |
| Awake and again X s without a touch | Back to sleep. |
| X = 0 in the console | Never sleeps (always on). |
| Wi‑Fi, pairing, error, boot, loading | Do not sleep. The message must be readable. |

The inactivity timer **restarts** on any touch, including the wake one.

The wake contact does **not** open the double-tap window. A tap right after the wake lift is a new tap, not the second half of a double.

BOOT hold 3 s (re-pair) stays live while asleep: it's a GPIO, not a screen recipe.

---

## 4. Console

**Display** setting (once per Round, next to Page swipe), not per page.

- **Screen timeout** — seconds of inactivity until sleep.
- Default **30**.
- **0** = Off (the screen never turns off).
- Integer. Valid range: **0** or **10–600**. Out of range is rejected by the server (or clamped in firmware on receipt: 0 or 10–600, default 30).
- English copy, e.g. label `Screen timeout`, hint `Seconds until the display sleeps. 0 = always on.`
- Simple switch: not shown.

Saved with the rest of the pages (Save pages / the PUT that already persists `pageSwipeAxis`). Bumps `rev`. Delivered in the config poll.

JSON field (camelCase, next to `pageSwipeAxis`):

```text
screenTimeoutSec: 30
```

Console persistence: a column (or equivalent) on `switches`, e.g. `screen_timeout_sec integer not null default 30`. Old devices without the column → 30.

The firmware stores the value in NVS with the rest of the display settings. If the poll doesn't carry the field, NVS / 30 stays.

---

## 5. Firmware (behavior)

### 5.1 Going to sleep

Only from **Ready** (or Empty, if there are no pages: also black, also wake to see the empty state). Inactivity clock = last lift (or last drag sample) + `screenTimeoutSec`.

If `screenTimeoutSec == 0`, don't start the clock.

On entry: backlight D6 LOW (or GC9A01 sleep + backlight off). Don't redraw Ready. No `UI_BUSY`. No text.

### 5.2 While asleep

- No on/brightness/scene GET every ~20 s.
- No redrawing the disc.
- Still able to notice the CHSC6X INT (D7) to wake.
- The console poll (1 h) may run: it doesn't paint the circle. If the config changed, apply it to NVS; the disc stays black until wake.
- Hue worker: don't queue page refreshes. An in-flight PUT from an *earlier* gesture may finish; it doesn't paint over black.

No ESP deep sleep. No `WiFi.disconnect()`. Modem sleep (`setSleep(true)` while asleep, `false` on wake) is **allowed**, not required in v1. What is required is turning off the backlight.

### 5.3 Wake (first contact)

1. INT or first point on the circle.
2. Backlight on, `uiPaint` Ready of the active page.
3. Mark that gesture as **wake**: until the lift, no `uiFireEvent`, no `uiDimPut`, no `pagesNext`/`pagesPrev`.
4. On lift: inactivity timer to zero. Ready for the **next** gesture.

Hue state refresh (on, brightness, scene, split fill) in the **background** on wake, same as on page change. The user sees at least the name + last local fill; the GET corrects it.

### 5.4 Gestures after wake

Same as Ready. Tap/double/ring/swipe and HTTP last-wins don't change.

### 5.5 What this is not

- It's not a recipe hold.
- It's not a tap that "sometimes" acts (if the disc was already on, the tap acts; if it was asleep, it doesn't).
- It's not dimming the fill while leaving the backlight at full.

---

## 6. Out of scope (v1)

- Clock, widgets, sleep animation.
- Multi-step backlight PWM (on / dim / off). v1 is on or off.
- S3 deep sleep, turning off Wi‑Fi, waking on a Hue timer.
- A different timeout per page.
- `hue-simple-switch`.
- Measuring mA in this document (worth measuring when implementing).

---

## 7. Definition of done

- In the console, a Round has **Screen timeout** (default 30, 0 = always on); it is saved and delivered in the poll.
- With timeout 30, Ready without a touch for ~30 s → black disc.
- First touch on black → the active page is shown; it does **not** change lights or page.
- The next touch (already awake) does act.
- Timeout 0 → never turns off.
- Pairing / error / Wi‑Fi don't turn off by themselves.
- The Simple switch and the rest of the Ready gestures don't change meaning when the screen is already on.
