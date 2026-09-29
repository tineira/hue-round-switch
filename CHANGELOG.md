# Changelog

User-facing release notes for the Round switch. One `### <version> — <date>` heading per `FIRMWARE_VERSION`, written in the same commit that bumps `FIRMWARE_VERSION`, in the wording of the person using the switch. CI sends the entry for the built version to the console with the upload.

Start a bullet with `Important: ` when the person must know or do something before or right after updating (a button to press, a setting to redo). The console shows those first, with an Important label, when Setup offers the update. Do not use it for new features.

### 0.6.1 — 2026-09-28

- An update from Switches is less likely to fail when the Wi-Fi signal drops for a moment during the download.

### 0.6.0 — 2026-09-28

- Later versions can be installed from Switches in the console, without a USB cable. Press Update now, and at its next check-in, once nobody has touched it for a few seconds, the switch shows Updating with the ring filling up, then restarts on the new version. Your pages, Wi-Fi and Hue pairing stay as they are.
- If an update can't finish (for example, the power goes out during it), the switch keeps working on the version it had, Switches shows that the update failed, and it tries again later.

### 0.5.32 — 2026-09-27

- During setup, the list of Wi-Fi networks can now include hidden networks that answer with their name, as on the Simple switch.
- If the screen reports a touch that never ends (the same point, unmoving, for a full minute), the switch now lets it go. Before, it could stop picking up changes from the console until it was restarted. A finger held still on the circle for that long does not count as a tap.

### 0.5.31 — 2026-09-27

- If the switch loses power or runs out of storage while saving new pages or gestures from the console, it now gets them again on its next check-in. Before, it could keep its old ones while the console showed it as up to date.

### 0.5.30 — 2026-09-26

- The Token rejected screen now points to Setup, the console page's current name.

### 0.5.29 — 2026-09-26

- The console shows whether the switch has your latest settings.
- Changes you save in the console reach the switch in about 30 seconds while you are editing it, and within about 5 minutes otherwise, instead of up to an hour.

### 0.5.28 — 2026-09-24

- A quick double tap counts as a double tap instead of a single tap.

### 0.5.27 — 2026-09-23

- Tapping right after the screen wakes, or right after a swipe, changes the lights at once instead of after a few seconds.
- Tapping quickly through a scene list moves to the next scene on every tap, and the new scene's name shows as you tap.
- A toggle does the right thing even if the lights were just changed from the Hue app or another switch.
- If the Bridge or Wi-Fi drops for a while, the switch reconnects by itself. It no longer asks you to press the Bridge button or needs a restart after a short outage.
- If the Bridge really stops accepting the switch, the screen asks you to press the Bridge button and pairs again by itself.
- A rejected console token no longer stops the switch. The lights keep working, and a small red dot at the bottom of the screen shows the token needs replacing in Devices.
- A command that fails makes the ring flash red briefly instead of showing Hue error, so the next tap is not lost.
- Touches near the edge of the button are no longer ignored.

### 0.5.26 — 2026-09-23

- The switch remembers the Wi-Fi network you saved during setup after it restarts, instead of showing No Wi-Fi.

### 0.5.25 — 2026-09-23

- The screen keeps showing Token rejected or No Bridge until the problem is fixed, instead of flickering back.

### 0.5.24 — 2026-09-22

- Pairing with the Bridge no longer fails if the first check right after pairing does not go through.

### 0.5.23 — 2026-09-22

- The switch ignores a console token that is not a real API key.

### 0.5.22 — 2026-09-22

- Devices can read the switch's status, start pairing, and clear its saved settings over USB.

### 0.5.21 — 2026-09-21

- The Wi-Fi network list during setup tries again if the first scan fails.

### 0.5.20 — 2026-09-21

- USB setup responds sooner after the switch starts.

### 0.5.19 — 2026-09-20

- The Wi-Fi network list during setup is more reliable while the switch is starting.

### 0.5.18 — 2026-09-20

- The switch answers a Wi-Fi scan request during setup straight away.

### 0.5.17 — 2026-09-20

- The Wi-Fi network list during setup is no longer empty when the scan is slow.

### 0.5.16 — 2026-09-20

- You can set up the switch from the console over USB: Wi-Fi and the console link are saved on the switch.

### 0.5.15 — 2026-09-20

- Touch and page swipes keep working while the switch checks in with the console, and the dimmer ring follows the lights chosen in the console.

### 0.5.13 — 2026-09-20

- The screen goes fully dark when it sleeps.

### 0.5.12 — 2026-09-20

- Fixed the screen staying lit when it should sleep.

### 0.5.11 — 2026-09-20

- The screen sleeps after a set time without a touch. The first touch only wakes it, so it never changes a light by accident.

### 0.5.10 — 2026-09-20

- After a scene is recalled, the dimmer ring shows the new brightness.

### 0.5.9 — 2026-09-20

- When tap and double tap control two different lights, the disc is split in half to show each one.

### 0.5.8 — 2026-09-20

- The screen keeps responding to touch while a command is on its way to the Bridge.

### 0.5.7 — 2026-09-20

- Restored double tap and dimming behaviour that an earlier build had broken.

### 0.5.0 — 2026-09-20

- Each page belongs to a room or zone, and the dimmer ring controls that group or the page's lights.

### 0.4.2 — 2026-09-20

- More reliable saving of settings and USB connection.

### 0.4.0 — 2026-09-20

- Pages: swipe between rooms, each with its own colours and scene cycling.

### 0.3.0 — 2026-09-20

- The centre button shows whether the lights are on, and the dimmer ring stays in sync with the Hue app.

### 0.2.0 — 2026-09-20

- Brightness ring, a loading screen, and better touch.

### 0.1.0 — 2026-09-20

- First Round switch firmware.
