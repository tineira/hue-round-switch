# Round Display — stability and responsiveness

**Product requirements** document for `hue-round-switch` (firmware only). The console (`hue-switch-console`) and `hue-simple-switch` are **out of scope**. Nothing here changes what the firmware sends to or expects from the console (`docs/device-api.md`). One point (§4.8) brings the firmware back in line with the contract.

**Status:** in progress. Written from a code review of firmware **0.5.26** (`672b2c4`) and implemented in **0.5.27**. Not yet checked on a board: §7 is the test list, and §6 lists the logs that confirm it.

Builds on `finished/input-during-hue.md` (Hue HTTP off the touch loop, last-wins) and `finished/idle-display.md` (screen sleep). Those rules still hold. This spec fixes what they left out.

---

## 1. What the user sees

Compared with the Simple switch, the Round switch:

1. **Sometimes shows a Bridge or token error** as if the key were not recognized. A reboot often fixes it.
2. **Paints the result of a tap late.** The fill or scene name changes seconds after the tap.
3. **Sometimes takes a tap and does nothing.** The circle shows the press, but the lights do not change.
4. **Is slow to change scenes.** Tapping quickly through a scene list repeats a scene instead of moving on.

## 2. Verdict

The Round is less stable than the Simple for four structural reasons. It is not one bug.

1. **Every Hue request opens a new TLS connection** to the Bridge, and the one worker runs its jobs one after another. Background state reads (after a wake, a swipe, and every 20 s) make up to **9 sequential handshakes**. A tap posted during that read waits for all of them.
2. **The UI acts on cached state that may be missing or stale.** A toggle guesses the target from `gLightOn`. That value is unknown right after a swipe and can be 20 s old. The scene cycle advances its cursor only when the job result arrives, and it throws that result away when a newer tap has already been posted.
3. **The Hue and console error states are sticky, and they block touch.** A single timeout at boot or reconnect is treated as a bad key. A single console `401` disables the switch for up to an hour. Nothing retries.
4. **Several threads share state without a clear owner.** The Bridge is reached at the same time by the Hue worker, the console worker (snapshot) and the loop (BOOT re-pair). Some flags and Strings are shared across cores with no lock.

The Simple switch hides most of this because it fires one GET+PUT per button press and has no background reads, optimistic screen, or scene cursor.

---

## 3. Findings

Each finding links to the symptom in §1 it explains. File references are to 0.5.26.

### 3.1 Bridge / token errors that a reboot fixes (symptom 1)

**A. A timeout at boot is treated as a bad key.** `hueEnsureReady()` (`hue_discover.h:361`) calls `hueKeyWorks()`. That function returns false for **any** non-200: timeout, `-1`, 503. Then it runs `huePairAppKey()`, and the screen says *Press Bridge button* for 90 s before it falls to *No Bridge*. The first TLS request after Wi-Fi associates is the one most likely to fail.

**B. After a failed setup, nothing retries.** `afterWifiUp()` runs only on a Wi-Fi up edge (`hue-round-switch.ino:358`). If `hueEnsureReady()` fails, `gHueReady` stays false. `loop()` then never calls `uiPollTouch()`, and nothing tries the Bridge again until Wi-Fi drops or the board reboots.

**C. A 401/403 sticks with nothing to clear it.** `hueNoteAuth()` sets `gHueAuthRejected` on one 401/403 and clears it only on a 200 with the key. While the flag is set, `applyStickyScreens()` drops gestures and forces *No Bridge*, and Ready refreshes stop because `gUi` is no longer Ready. The only request that still uses the key is the hourly console register snapshot (`hueClipStream`). The screen can therefore stay on *No Bridge* for up to an hour.

**D. A console 401 blocks the lights.** `consoleNoteHttp()` sets `gConsoleAuthRejected` on one `401`. `loop()` (`hue-round-switch.ino:366`) then stops reading touch, and the screen shows *Token rejected*. With recipes in NVS, the next poll is **1 h** away (`kPollArmedMs`). `device-api.md` §Auth says: "Recipes already in NVS keep running on the LAN." The Round does not follow that. The Simple does.

**E. Discovery runs again on every Wi-Fi reconnect, in the loop.** `afterWifiUp()` → `hueEnsureReady()` → `hueFindBridge()` always tries mDNS **first**, before the cached IP, and runs a key GET. This happens synchronously in `loop()` (touch is dead meanwhile) on every Wi-Fi blip. Each blip also re-registers with a full snapshot (`consoleBootSync`).

**F. BOOT re-pair runs in the loop and races the worker.** `bootPoll()` calls `hueRePair()` synchronously (`channels.h:61`). `hueRePair()` clears the RAM key first, while the `hueJob` task may be sending a request with it. The USB `HUEPAIR` path already runs in the worker. BOOT should use the same path.

### 3.2 Slow ack and slow scene changes (symptoms 2 and 4)

**G. A new TLS handshake for every request.** `hueHttp()` and `hueClipStream()` build a fresh `NetworkClientSecure` + `HTTPClient` on each call (`hue.h:96`, `hue.h:154`). On the S3, an ECDHE handshake to the Bridge costs hundreds of ms. The actual Clip v2 request is tens of ms. The Bridge supports HTTP/1.1 keep-alive.

**H. Refresh is long and cannot be interrupted.** `hueJobRunRefresh()` makes one state GET (4 s timeout), then one `GET scene/<rid>` per scene in the list: up to `kMaxScenes` = 8 at 2.5 s each. A refresh is queued on **every wake** (`uiIdleWake` sets `gNeedHueState`), on every swipe, when leaving the error screen, and every 20 s. The worker is serial, and "last wins" only replaces the *pending* slot. A tap posted during a refresh waits for the whole refresh. The usual case is wake the screen, then tap. That tap waits for the wake refresh (about 9 handshakes), and the tap looks ignored.

**I. The scene recipe does extra work after the PUT.** After a recall, `hueJobRunRecipe()` runs `delay(400)`, then a dimmer readback: one GET for a group, or one per light (`hue_job.h:429`). Only then is the result (scene name, cursor) posted. The worker stays busy for all of it.

**J. The scene name painted at the tap is the old one.** `uiFireEvent()` → `uiApplyLastSceneName()` reads `pagesLastSceneRid()`. That is the scene **before** this tap, because the cursor moves only when the result arrives (`uiHueJobPoll`, `pagesSetLastSceneRid`). The right name shows only after the whole of (I).

**K. Fast taps repeat a scene.** Tap 1 picks scene *n+1* from cursor *n*. Tap 2 comes before tap 1's result and also picks *n+1*, because the cursor has not moved. `hueJobTakeResult()` then discards tap 1's result as stale, so the cursor never records it. Two taps recall the same scene.

**L. The cursor is written to flash on every tap.** `pagesSetLastSceneRid()` opens NVS and calls `putString` in the loop on every scene result and on every *off*. Each write stalls the loop for a few ms and wears flash for no user benefit.

**M. Bridge access competes.** The hourly console register (`consoleRegister` → `hueBuildSnapshot`) streams `light`, `room`, `zone` and `scene` (20 s timeout each) from the console task. That runs alongside the Hue worker, on the same core, against the same Bridge, alongside the TLS session to Vercel. A tap during a snapshot competes for CPU, heap and Bridge sockets.

### 3.3 A tap that does nothing (symptom 3)

**N. A toggle guesses its target from state it may not have.** `hueJobArmRecipe()` for `toggle`: `nextOn = !(gLightOnKnown && gLightOn)`. After a swipe, `uiOnPageChanged()` clears `gLightOnKnown`. A toggle before the refresh returns (see H) therefore always sends **on**. If the lights were already on, nothing changes. The same happens when the lights were changed from the Hue app or another switch within the last 20 s. The Simple does GET then PUT (`hueToggle`), so it never has this bug.

**O. A tap waits behind a refresh.** See H. The press shows at once, but the PUT can go out several seconds later.

**P. Tap-release detection can add up to 400 ms.** In `uiPollTouch()`, if the CHSC6X keeps reporting a point after the finger lifts (`t[0]==1`, INT high), the lift is inferred only after 400 ms without movement (`ui.h:969`). On a page with a double-tap recipe, the 350 ms double window adds to that. Needs a log on the board to confirm how often the chip does this.

**Q. The touch range locks for good after one bad read.** `touchMapRaw()` sets `gTouchFullRange = true` for good on the first raw value above 127 (`touch.h:54`). One corrupted frame switches every later touch to unscaled coordinates, so hits drift toward the upper left until the next reboot.

**R. A dead band between the button and the ring.** A touch that starts between `kBtnRadius` (88) and `kRingGrabInner` (96) is dropped with no feedback (`ui.h:1015`). On a page without a dimmer, a touch starting anywhere outside 88 px is dropped.

**S. The error screen swallows touches.** A failed recipe shows *Hue error* for 2 s, and `uiPollTouch()` ignores every touch while `gUi == UI_ERROR`.

### 3.4 Shared state across tasks

**T. Unsynchronized cross-core state.** Some examples:

- `gConsoleConfigBody` (a String) is written by the console task on core 0 and read by the loop on core 1. The hand-off is a plain `bool` with no lock. `consoleForget()` clears it from the loop while the task may still be writing it.
- `gHueAuthGraceUntil` is written from the loop (pairing) and from the worker, with no lock.
- `gHueBridgeId` is read under `hueStrLock`, but `hueRePair()` reads `gHueAppKey` without the lock (`hueLooksLikeKey(gHueAppKey)`).
- Wake and sleep, `gNeedHueState`, `gNeedFullPaint`, and the page and recipe arrays are written by the loop only. That is correct, and it should be written down as a rule.

---

## 4. Required behavior

### 4.1 One Bridge client, kept alive

- The Hue worker owns **one** persistent HTTPS client to the Bridge (keep-alive, `setReuse(true)`). It reconnects only when the socket closes or a request fails.
- **All** Clip v2 traffic goes through the worker: recipe, dimmer, refresh, snapshot streams, key check, and re-pair. No other task or the loop opens a connection to the Bridge.
- At most one TLS session to the Bridge exists at a time. The console TLS session to Vercel is separate.
- Target: a PUT on a warm connection finishes in under 150 ms on the LAN. The handshake happens only after an idle close or an error.

### 4.2 User commands come first

- The worker has **two** slots: *user* (recipe or dimmer, last-wins) and *background* (refresh or snapshot step).
- A user job **preempts** background work between HTTP requests. A refresh or snapshot checks the user slot before each request and gives way if it is set. A refresh is dropped and re-queued. A snapshot resumes where it stopped.
- A tap that arrives while a refresh is on the wire waits for **at most one** request, not a whole refresh.

### 4.3 Cheaper, quieter refresh

- A refresh is **one** state GET: `grouped_light` for a group page, or the lights of the page. The active-scene check reads only the **cached** scene rid (at most one extra GET). Only when that scene reports inactive may the other rids be scanned, as preemptible background work.
- No refresh while the screen is asleep (as `idle-display.md` §5.2 already says). On wake, do not queue the refresh until the wake lift, so the wake contact never competes with it.
- Drop the fixed `delay(400)` and dimmer readback from the scene recipe job. If the ring needs the new brightness after a scene, queue a background refresh at least 500 ms later. It must not hold the worker.
- Keep the 20 s Ready poll, as background work.

### 4.4 Correct optimistic state

- **Scene cursor:** advance the page's cursor in RAM **when the tap is armed**, before the PUT. The tap paints the name of the scene it is about to recall. Fast taps walk the list: *n+1*, *n+2*, and so on. A failed recall moves the cursor back only if no newer tap has advanced it. Write the cursor to NVS lazily (debounced about 5 s, or on sleep). Do not write it on every tap.
- **Toggle:** if the loop knows the target's on state from a read less than 5 s old, use it (optimistic paint as today). Otherwise the worker resolves the toggle with a GET then a PUT on the warm connection, and the loop paints once the result arrives. The worker always posts the final on state, and the loop applies it even when it differs from the guess.
- **Results are never lost for bookkeeping.** A result marked stale for *painting* (newer job, different page) must still update the cursor and the on/off cache when it belongs to the same page and nothing newer has set them.

### 4.5 Hue readiness state machine (no dead ends)

Replace the one-shot `afterWifiUp()` → `hueEnsureReady()` with a state machine owned by the worker. The loop only reads the state and paints it.

Implemented as `HueLink` in `hue.h`:

| State | Entered when | Screen | Touch | Leaves by |
| --- | --- | --- | --- | --- |
| `LINK_START` | Boot, or after `HUECLR` | *Loading* | off | Bridge answers a keyed GET (→ `READY`), no key (→ `PAIRING`), or a second miss (→ `SEARCHING`) |
| `LINK_SEARCHING` | The Bridge did not answer twice | *No Bridge* | off | Bridge answers (→ `READY`), retried with backoff |
| `LINK_PAIRING` | No key; `HUEPAIR` / BOOT 3 s; or **two** consecutive 401/403 with the key, outside the grace window | *Press Bridge button* | off | A new key from the Bridge button, or the old key working again (→ `READY`) |
| `LINK_READY` | A keyed request returned 200 | Ready | on | See below |
| `LINK_UNREACHABLE` | Timeout / `-1` / 5xx with a key after being `READY` | Ready stays; the ring flashes red on a failed command | on | Any keyed 200 |

Rules:

- **Pairing starts only** with no saved key, after a BOOT 3 s hold, on `HUEPAIR`, or after two real 401/403 answers. A timeout, a `-1` or a 5xx **never** starts pairing and never clears the key.
- `SEARCHING` tries the **cached IP first**. mDNS and cloud discovery run only if the cached IP does not answer. Retry with backoff: 2 s, 5 s, 10 s, 30 s, 60 s cap. Never stop.
- A rejected key is **kept** in NVS. After a rejection, `PAIRING` re-tries the old key with `GET /clip/v2/resource/bridge` every 30 s, between pairing POSTs. A 200 goes back to `READY` without a reboot. This was decided during implementation, instead of showing *No Bridge*: the screen tells the user what fixes it (press the Bridge button), and nothing is lost if the rejection was a Bridge glitch.
- Pairing POSTs every 500 ms for 90 s, then every 3 s, until a key arrives. It never ends in a dead end.
- On a Wi-Fi reconnect with a known Bridge, go straight to `READY` after one keyed 200. No mDNS, no full re-setup, and no forced re-register. The regular poll still runs.
- BOOT re-pair posts to the worker (same path as `HUEPAIR`). The loop never calls `hueRePair()`.

### 4.6 Wi-Fi

- `loop()` never blocks on Wi-Fi: no `wifiWait()` outside `setup()`.
- Let the STA stack's automatic reconnect run. Call `WiFi.disconnect()` + `begin()` only if it has not reconnected after 30 s. Do not call them every 10 s.

### 4.7 Touch

- Infer the lift from INT going high, not from 400 ms without movement. 0.5.27 kept the old 80 / 400 ms rules. Once taps got faster, a quick double tap landed inside that wait and merged into one long press, which fired as a single tap. **0.5.28** ends the touch after 30 ms of INT high (`kTouchLiftMs`), the signal Seeed's own driver uses (1 ms there). The re-arm gap after a lift drops from 40 to 15 ms. The double-tap window grows from 350 to 400 ms, counted from the detected lift, so slow double taps still reach it (the old effective window was about 80 + 350 ms from the real lift). The 80 ms no-point rule stays as a fallback.
- Decide the coordinate range from the chip at boot, or after at least 3 consistent frames above 127. Never switch it on one frame, and reject frames with out-of-range values.
- Treat a start between 88 and 96 px as center on a page without a dimmer, and as ring on a page with one. No dead band.
- ~~On the error screen, a touch dismisses the error.~~ Dropped: a failed command no longer shows the error screen (§10.2), so nothing swallows touches.

### 4.8 Console token

- A console `401` **does not stop local control.** Ready, recipes, dimmer and swipe keep working from NVS, as `device-api.md` §Auth says.
- The rejected token shows as a quiet marker in Ready: a small error-colored dot at 6 o'clock, in the gap of the dimmer ring (§10.1). Do not replace the whole screen with *Token rejected* while pages exist. With no recipes in NVS, the full *Token rejected* screen stays.
- The poll cadence does not change (1 h armed, 1 min empty). A new `HUESET` token clears the state at once, as it does today.

### 4.9 Ownership and locking

Write this rule down in the code:

- **Loop (core 1)** owns the UI, the gesture state, and the page and recipe arrays in RAM. It is the only writer of NVS for pages and recipes.
- **Hue worker** owns the Bridge client, the readiness state, and the Bridge IP, key and id Strings (written under `hueStrLock`, read only through a copy).
- **Console worker** owns the Vercel client. It hands the config body to the loop through a FreeRTOS queue (or under a mutex), not through a shared String plus a `bool`.
- Flags that cross cores are `std::atomic` or read and written under the job spinlock. No bare `bool` or `unsigned long` shared between tasks.
- The snapshot streams for register run **on the Hue worker** as preemptible background steps. The console worker asks for them and gets the JSON back.

---

## 5. Out of scope

- The Clip v2 eventstream (`/eventstream`). It is the real long-term fix for "state changed elsewhere" (Hue app, other switches). It needs a second long-lived TLS session and its own spec. With §4.1–4.3 in place, the 20 s poll is cheap enough for now.
- Any change to the console contract, poll cadence, NVS keys written over USB, or the register payload.
- New screen copy beyond the token marker (§4.8).
- The double-tap window length (350 ms). It is a product choice, not a bug.
- `hue-simple-switch`. See §8 for the divergences found.

---

## 6. Measure before and after

Build with `SERIAL_DEBUG 1` and log these (one line each, with `millis()`):

1. Every Bridge request: method, path, whether it reused the connection, handshake ms, total ms, code.
2. Every worker job: kind, posted, started and finished times, and whether it was preempted or stale.
3. Touch: raw frames around each lift (INT level, `t[0]`, raw x/y) for 20 taps. This confirms or rules out finding P.
4. Every change of the readiness state (§4.5), with the reason (code or timeout).
5. Free internal heap and free PSRAM when each job starts.

Record the baseline on 0.5.26 before changing code, for: tap after a wake, tap right after a swipe, 5 fast scene taps, and a Bridge reboot while the switch runs.

## 7. Definition of done

On one board in Ready, with Wi-Fi and the Bridge up:

- [ ] Tap → PUT on the wire in **< 300 ms** at p95 (log 1–2), including a tap right after a wake or a swipe.
- [ ] Five fast taps on a scene list advance five scenes in order. Each tap paints the next scene's name at the lift, and the lights end on the fifth. Taps that arrive while a recall is on the wire replace each other (last-wins), so the Bridge may skip the scenes in between.
- [ ] A toggle right after a swipe, and a toggle after the lights were changed from the Hue app, both do the right thing.
- [ ] Unplugging the Bridge for 2 min, then plugging it back in: the switch returns to working Ready by itself. No pairing screen, no reboot.
- [ ] Rebooting the router: the same, with no mDNS or full re-setup when the Bridge IP did not change.
- [ ] A revoked console token: lights, dimmer and swipe keep working, and the marker is visible.
- [ ] A real key revocation (delete the app key in the Hue app): *Press Bridge button* after two rejections. Pressing the Bridge button recovers without a reboot.
- [ ] No two TLS sessions to the Bridge at once (log 1), including during the hourly register.
- [ ] No NVS write per tap (log or code review).
- [ ] 24 h soak with a tap every few minutes: no stuck error screen, heap flat.

## 8. Divergences with `hue-simple-switch` (not to be fixed here)

Per `AGENTS.md`, these are reported, not edited:

- The Simple has the same boot logic: `hueKeyWorks()` treats a timeout as a bad key and then pairs (finding A). It hurts less there because a button press still sends its PUT, and the 200 clears the state. It should get the same state machine (§4.5).
- The Simple also opens a new TLS session per request (finding G).

## 9. Checklist (this repo)

- [ ] §6 baseline logged on 0.5.26
- [x] §4.1 persistent Bridge client in the worker
- [x] §4.2 user / background slots with preemption
- [x] §4.3 refresh trimmed; no readback in the scene job; no refresh until the wake lift
- [x] §4.4 scene cursor at arm time, lazy NVS; toggle resolution; results kept for bookkeeping
- [x] §4.5 readiness state machine; BOOT re-pair through the worker
- [x] §4.6 Wi-Fi never blocks the loop
- [x] §4.7 touch fixes: range latch and dead band (0.5.27); lift from INT high (0.5.28)
- [x] §4.8 console 401 does not block local control
- [x] §4.9 ownership rules; queue for the config body; atomics
- [x] `FIRMWARE_VERSION` bumped (0.5.27); `## Round` changelog entry in the console tree (user-facing wording)
- [ ] Installer bins synced to the console (automatic on push to `main`)
- [ ] §7 definition of done checked on a board by the user

## 10. Decisions (were open questions)

1. **Token marker (§4.8):** a 4 px error-colored dot at 6 o'clock (y = 229). 12 o'clock is inside the dimmer ring; 6 o'clock is its gap, so the ring never paints over it. With no recipes in NVS, the full *Token rejected* screen stays.
2. **Failed command:** the button ring flashes the theme's error color for 700 ms and Ready stays. A state read follows 300 ms later and repaints what the Bridge reports. Ready no longer switches to *Hue error*.
3. **Toggle freshness:** 5 s (`kStateFreshMs`). Within 5 s of a Bridge read, or of this switch's own command, a toggle paints at once. Otherwise the worker does GET then PUT, and the fill changes when the result arrives.
4. **Phases:** shipped as one release (0.5.27). Every part touches the same worker and result path, so splitting would have meant building and testing an in-between design.
