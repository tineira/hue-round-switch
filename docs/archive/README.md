# Archive (frozen)

Shipped firmware specs, kept as design history. **Frozen: never add, edit or "reopen" a file here** (fixing a broken link is fine). Code comments cite them for the reasoning behind a rule.

Where new work goes instead:

- **A change to what this firmware sends to or expects from the console** → a "Contract change" issue in [`hue-switch-console`](https://github.com/tineira/hue-switch-console/issues/new/choose); the contract itself is in the console's `docs/`.
- **Firmware-only specs and decisions** → the maintainer's internal tracker.
- **Open work** → GitHub issues.
- **How it works today** → [`AGENTS.md`](../../AGENTS.md), [`README.md`](../../README.md) and the console's [`docs/round-pages.md`](https://github.com/tineira/hue-switch-console/blob/main/docs/round-pages.md).

Archived on 2026-10-26 from `docs/specs/`.

| Spec | Summary |
|------|---------|
| [`idle-display.md`](specs/idle-display.md) | Screen sleep after the per-device timeout; the first touch only wakes. Contract: `round-pages.md` §9.1, rule 25 |
| [`input-during-hue.md`](specs/input-during-hue.md) | Touches stay live while the Bridge answers (`hue_job.h`). Contract: `round-pages.md` §8.1, §13, rule 23 |
| [`stability.md`](specs/stability.md) | Stability and responsiveness from the 0.5.26 code review, implemented in 0.5.27–0.5.28: Bridge worker, readiness state machine, touch fixes, ownership rules (§4.9) |
