# Round Display — touches while Hue answers

**Product requirements** document. Covers `hue-round-switch` (firmware, circle). Not an implementation guide or a changelog.

The console (`hue-switch-console`) and `hue-simple-switch` are **out of scope**. The circle doesn't wait for Vercel; this document is about the **local Bridge** (Clip v2 HTTPS on the LAN).

Closes the gap in the pages requirements (`hue-switch-console/docs/round-pages.md`) §8.1 and §13: the GET+PUT to the Bridge **does happen**, but **the disc does not freeze** meanwhile.

**Status:** implemented. Firmware **0.5.8** moves recipe, dimmer and refresh GET/PUT out of `loop()` (`hue_job.h`: one last-wins slot in its own task). Firmware **0.5.14+** moves snapshot / register / console poll out of the touch loop. `hueHttp()` is still synchronous, but in those tasks, not in Ready. Pairing / discovery / BOOT 3 s re-pair may stay in the loop. `round-pages.md` §8.1 / §13 / decision 23 point here.

---

## 1. Verdict

**Keep accepting touches.** A tap, double tap or ring release does **not** block Ready. A second gesture is a new command, not "wait for the lamps to finish".

**Don't lock the screen.** No `UI_BUSY`, no `...`, no waiting or help text. The ack is what Ready already knows how to paint: invert on pressing the center, on/off fill, scene name, live ring.

**Last command wins.** No long gesture queue is needed. If another tap or ring release arrives while a GET/PUT is still in flight, the pending work is **replaced** (last-wins). The page swipe never waits on the network.

That's what a Hue dimmer / Tap Dial, a Lutron Maestro and HomeKit do: the wall control stays alive; the lights take their time (CLIP transition ~400 ms; the Bridge limits ~10 PUT/s to `/light` and ~1/s to `/grouped_light`).

---

## 2. Problem

The user is standing, at arm's length, touching a 39 mm circle. A tap must feel like a wall switch, not a form waiting for a 201.

Before 0.5.8 it didn't:

1. `hueHttp()` was a synchronous GET/PUT in `loop()` (timeouts 2.5–8 s). `recipeFire()` and `uiDimPut()` came from `uiPollTouch()` / `uiTick()`. No queue, no Hue task.
2. The CHSC6X doesn't deliver gesture IDs. Tap, double (`kDoubleTapMs` = 350, `gSecondTap` on the second down, fire on lift), swipe (≥40 px) and lift are inferred in `uiPollTouch` / `uiTick`. If the loop is inside HTTP, those state machines **don't run**: the next touch is lost.
3. Group + `dim` were already in firmware: a scene cycle was a `status.active` GET (2500 ms) per candidate and then a PUT; a `PAGE_DIM_LIGHTS` ring was a GET per rid and a PUT to the ones that are on. That **lengthened** the block, it didn't move it out of the loop.
4. `UI_BUSY` (paints `...` and a ring pulse) **exists** and is **not entered** from tap, double, dimmer or swipe. It must not be used in Ready.
5. The center invert was cleared on lift **before** `recipeFire()`. The on/off fill and the scene name were painted **after** an HTTP ok (`uiSetLightOn` / `uiApplyLastSceneName`). No optimistic ack: during the PUT the disc sat still in Ready.
6. `gOnHueWait` pumps `uiTick` only in the **pairing** wait (`hue_discover.h`), not in recipe GET/PUT. `uiTick` doesn't call `uiPollTouch`. It doesn't count as "Ready alive".
7. After a swipe, `uiOnPageChanged` paints immediately and sets `gNeedHueState`; the on/brightness/scene GET runs in `uiTick` when there is no finger. That GET **did** block the loop on lift.

The pages spec requires a live disc (`round-pages.md` §8.1, §13, decision 23). Recipe / ring / page refresh left the loop in 0.5.8; snapshot / register / console poll in 0.5.14.

---

## 3. Expected result

In Ready, with Wi‑Fi and Bridge ok:

| The user does | What they see (immediately) | What happens to the next gesture |
| --- | --- | --- |
| Tap (center, `short` recipe) | Invert on down; on lift, **local** on/off fill and scene (no how-to) | Another tap, double, ring or swipe is accepted **without** waiting for the PUT |
| Double tap (`double_click`) | Same: invert, then fill / scene / off | Same |
| Drag the ring | The 270° arc follows the finger (1–100 absolute) | No PUT during the drag. On release, one PUT of the **last** %. If released again before the HTTP returns, the **last** % wins |
| Page swipe | Name, dots and theme of the new page **as the threshold is crossed** | Never waits for Hue. The new page's state GET runs in the background |
| Empty recipe | Nothing Hue; the gesture is a no-op | The loop doesn't block |

After a **successful** PUT: the fill and scene name already painted locally stay. A later poll (~20 s) or the background GET on page change may correct them if the Bridge disagrees.

After a **failed** PUT (timeout, 4xx, Wi‑Fi down): error screen as before, for a moment, then back to Ready. No endless `...`.

None of this paints new copy in Ready. `Tap to toggle`, `Please wait`, the `UI_BUSY` `...`, and any text spinner stay forbidden.

---

## 4. Input policy (closed)

1. **Ready never freezes** on a recipe or dimmer GET/PUT. `uiPollTouch` / `uiTick` (or whatever infers gestures) keep running.
2. **Last-wins**, not ignore-during-inflight and not a FIFO queue. A new tap replaces the recipe PUT that hasn't gone out yet or is still in flight. A second ring release replaces the pending brightness. Three taps are not "accumulated" to run in series when Hue frees up.
3. **One dimmer PUT per release**, not one per drag sample. That is already the case (`gBriLastSent`); it stays. If the % didn't change, there's no PUT.
4. **Swipe > HTTP.** Changing page paints immediately. If a PUT from the previous page is in flight, it is cancelled or left to die without applying its ack to the new page. The new page's on/brightness/scene GET is background.
5. **Optimistic ack.** Invert = down on the center. On/off fill and scene name = the recipe's **local** result (cycle: the `rid` about to be PUT; off: fill off and no scene line). The ring while dragging = brightness ack. There is no extra "HTTP in progress" ack.
6. **`UI_BUSY` is not for Ready.** It stays for pairing / loading if needed. Not entered from tap, double, dimmer or scene cycle.
7. **The Bridge is still the truth** for "which scene is active" and on/brightness, in the background. The finger doesn't wait for that truth before the **next** gesture.
8. **Hue limits.** Don't hammer the Bridge: one command in flight at a time to Clip v2 from this device (last-wins already serializes). Not one PUT per ring pixel.
9. **Timeouts.** They still exist (2500 / 4000 / 8000 ms). If they expire: system error, not a Ready frozen until the timeout: the user may already have made another gesture.
10. **No recipe hold.** BOOT hold 3 s (re-pair) doesn't change. Re-pair may block: it's not a Ready gesture.

---

## 5. Changes required

Firmware only (`hue-round-switch`). The console doesn't call the Bridge. The config poll (Vercel) is not part of this feature.

### 5.1 Decouple Hue from the touch loop

Recipe and dimmer GET/PUT ran in the same `loop()` that reads the CHSC6X. They must **leave that path**.

Result: while a GET/PUT is on the wire, the device keeps inferring down / move / lift / swipe / double-tap window.

How (task, one-slot queue, non-blocking HTTP) is up to the implementation. The requirement is the behavior, not the FreeRTOS API.

Do not copy `HueRecipe[]` / `Page[]` onto the loop's stack (the 8 KB / `SET_LOOP_TASK_STACK_SIZE` limit still applies).

### 5.2 Recipes (tap / double)

`uiFireEvent` / `recipeFire` must not `return` after a synchronous `hueHttp()` that left touch dead.

When firing the gesture:

- Update fill and scene **before or at the same time** as the HTTP is queued, with the local result (on/off, next `rid` in the list). Previously that happened **after** the 200; it must move earlier.
- Queue **one** job: the **active page's** recipe and the event. If one is already queued, the new one **replaces** it.
- The scene cycle may keep doing a `status.active` GET + PUT to the next `rid` (pages §8.1). That GET+PUT does not run in the touch loop. (Later amended: the cycle uses the NVS cache, see `round-pages.md` §8.1.)

### 5.3 Ring

No product change: PUT on release, last % wins, skip if `gBriLastSent` is the same.

The runtime does change: `uiDimPut` doesn't block the loop. A release during an earlier PUT replaces the % (or is ignored if it's the same). `mode: lights` (GET per rid + PUT to the ones that are on) also leaves the touch loop.

### 5.4 Swipe and state poll

On swipe: paint immediately (`uiOnPageChanged`). The `gNeedHueState` on/brightness/scene GET is **not** done inside the gesture. If a PUT from the previous page is still alive, it must not paint that page's fill/scene over the new one.

The ~20 s Ready poll may stay; it must not block touch either.

### 5.5 Errors

If the in-flight job fails and the user has **not** changed page or fired another command that replaces it: `UI_ERROR` as before and back to Ready. If there is already a newer command, the old one's failure doesn't overwrite the new one's ack.

### 5.6 Pages spec — done

`hue-switch-console/docs/round-pages.md` already says the GET+PUT happens and the circle does not freeze. No rewrite needed beyond aligning a new detail.

---

## 6. Out of scope

- Web console, "Saving pages" spinners, or any HTTP to Vercel.
- `hue-simple-switch` (GPIO; its double-click state machine is untouched).
- Clip v2 eventstream (`/eventstream`) in v1.
- A queue of N gestures, undo, or "quick tap = full on" à la Lutron Maestro (a second tap is **the tap recipe**, not an extra meaning).
- Slide animation between pages, ring pulse as busy in Ready, or new copy.
- Measuring this Bridge's typical latency (doesn't block the verdict).
- Changing numeric timeouts unless needed so a worker doesn't hang.
- Re-pair (BOOT 3 s) and system screens (Wi‑Fi, pairing, error): may stay synchronous.

---

## 7. Relation to pages

Unchanged: max 6 pages, tap and double, no hold, local swipe, page = one room/zone, `dim` §8.2 (already in 0.5.7), scene lists, Ready without how-to, ASCII, closed themes.

The Hue **work** (GET active + PUT next, dim group/lights) stays; the **user's wait** doesn't. Group/dim don't replace this feature: they make it more urgent because there are more GETs in the same loop.

---

## 8. Definition of done

This feature is **done** when, in Ready:

- A tap (or double) fires the recipe and **right away** tap / double / ring / swipe work again, even if the previous PUT hasn't returned.
- The disc doesn't enter `UI_BUSY` or show `...` for a recipe or a dimmer.
- The invert, fill and scene name update locally at the gesture, not at the Bridge's 200.
- The ring stays last-wins on release; no PUT per sample; a second release wins.
- A swipe changes page at the threshold, without waiting for Hue.
- A slow PUT (or the 2.5–8 s timeout) does **not** swallow the second touch.
- `hue-simple-switch` and the console don't change.

Until the recipe, dimmer and page-refresh HTTP leave the touch loop **and** the fill/scene are painted locally at the gesture, the circle will keep losing touches and the ack will arrive late. That's the gap; not a half-done "busy screen". `gOnHueWait` in pairing doesn't close this gap.
