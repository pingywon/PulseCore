# Pluto 9000 v3 "Cadence" — recovered design intent and build spec

Status: **specified, not built.** The header draft is at
`docs/design/pf_core-v3-cadence.h.draft`. Firmware on `main` is still v2.0.0
and passing 89 tests.

Everything below was recovered from `github.com/pingywon/plusecore` — the old
dev's repo — not invented. Source transcripts are in `docs/original/`.

---

## 1. The architecture we've been building wrong

**Pluto 9000 was designed around four solenoids plus a pump. v43 collapsed it
to three channels and called the third one "motor".**

From `assets/four-solenoids-diagram.svg` and the website brief:

| # | Solenoid | Its job, in the original words |
|---|---|---|
| 1 | Main vacuum control | "steadies the pull" |
| 2 | Pulse rhythm control | "creates the squeeze-and-rest rhythm" |
| 3 | **Comfort trim control** | "refines comfort" |
| 4 | **Release control** | "manages the soft let-go release" |

Plus the pump motor as a separate output.

What v43 actually shipped — and what v2.0 inherited from it:

```
G5 = VAC      -> correct (solenoid 1)
G6 = PULSE    -> correct (solenoid 2)
G7 = MOTOR    -> WRONG. This is the comfort trim valve.
G8 = unused   -> WRONG. This is the release valve, held off permanently.
```

This explains a loose end I flagged in the v2.0 audit but misread. I listed
`releaseMs` as a dead setting — "stored, loaded, saved, and read by nothing."
It was not dead. **It was the release stage's timing parameter, for a valve
that had been disconnected.** I deleted a setting that was waiting for its
hardware. That's the single most useful correction to come out of this.

## 2. The five-part cadence

The product is not a pulser. The brief is explicit: *"a control system for the
whole milking rhythm."*

> pressure → **pull** → **hold** → **pulse** → **release**

- **Primary suction** — the main pull level
- **Pulse strength** — how pronounced the squeeze-and-rest feels (drives the
  comfort trim valve)
- **Pulse speed** — cadence in PPM
- **Pulse hold** — the dwell between pull and pulse
- **Release** — the controlled let-go

The stated differentiator, verbatim: *"Any milker can pull. Pluto 9000 is
built to pull and let go with intention."* and *"The release is the
difference."*

v2.0 implements exactly two of these five (suction, pulse speed) plus ratio.

## 3. The pump curve — a real defect in v2.0

From the old-controller teardown transcript:

> "the motor squeals and is useless until it gets to at lease 75% power"
> …
> "our app should probably hide that useless range from the user"
> "pump should get a strong enough start pulse"

**v2.0's motor soft-start does the opposite of this.** It ramps duty linearly
from 0 to the set point over `motorSoftMs`, which means it deliberately spends
time crossing the stall-and-squeal band on every single start. I built a
feature that walks the pump through the exact region the hardware notes say to
avoid.

The fix, in the draft header:

```c
int pumpFloor;    // duty below which the pump makes no useful vacuum (~45)
int pumpKickMs;   // full-duty kick to break stiction, then drop to mapped duty
```

Operator 0–100% maps onto `[pumpFloor..100]`, not onto raw duty. 0 still means
genuinely off.

## 4. On "features that entertain the udder"

Taking that as: *make the session better for the animal, not just mechanically
adequate.* That is what the comfort trim and release stages are for, and it is
also where the genuinely useful new feature lives:

**A stimulate lead-in.** Fast, shallow pulsation before the working pull. In
dairy practice this is what prompts letdown; going straight to full suction on
a cold udder is both less effective and less kind to the animal. It is in the
draft as `stimSec` / `stimPpm` / `stimSuction`.

I want to be straight about the boundary I held here. I did not add novelty
patterns or randomised "entertainment" modes. This device applies vacuum to
living tissue, and the established dairy-safe envelope — rate, ratio, vacuum
level — exists because deviating from it causes teat damage and mastitis. So
the expansion is *more control over comfort within that envelope*, which is
what the original brief was already asking for. Everything stays clamped by
`Limits`. If you did mean something more playful, say so and I'll scope it —
but I'd want it on the UI and the mascot, not on the valve timings.

## 5. Build order

The draft header is complete. Remaining work, roughly in dependency order:

1. `pf_core.cpp` — phase state machine, pump curve, 5-channel `Outputs`.
   (I had this written when the session hit the spend cap; it needs redoing.)
2. `tests/test_core.cpp` — phase sequencing, release decay shape, pump floor
   mapping, "release always reaches zero", stimulate→pull handoff.
3. `pf_config.h` / `pf_hal.*` — five channel roles with configurable pins.
   **Pin assignment for the 5th output is unverified — do not guess it.**
4. `pf_app.*`, `pf_api.cpp`, `pf_log.cpp` — field renames + new state fields.
   API goes to level 2.
5. `pf_ui.cpp` — a cadence screen showing the five phases; Milky mood tied to
   session phase.
6. `web/src/index.html` — phase strip, release control, Milky.
7. `sim/` — mirrors the new API.

## 6. Brand (for the site rebuild)

- **PulseCore** is the company, **Pluto 9000** is the product.
- **Milky** — the bottle. "clean, blue, and unmistakable." Keeps the rhythm.
- **Bessie** — the cow. "the reason the rhythm matters." Keeps it interesting.
- Palette: Milky blue, cream, black. Not startup gradients.
- Milky has a deliberate **fatigue arc** down the page: happy → standing →
  tired → drink → relaxed → exhausted-seated → exhausted-dizzy, closing on
  *"He gave it everything."*
- Approved lines: *"Control the session, not just the motor."* ·
  *"Any machine can pull. Pluto knows when to let go."* · *"pull · pulse ·
  hold · release"*
- Avoid: copy that describes the page instead of the product; over-explaining
  Milky past the hero.

## 7. Assets — done and in the repo

`tools/build_assets.py` is built and run.

- 40 source renders (40 MB) → `site/assets/mascot/*.webp`, **680 KB total**
  (2% of source), alpha-trimmed and height-normalised.
- 6 moods → `firmware/pulsefeed/pf_milky.h`, RGB565 + 1bpp mask, **83 KB** of
  flash: idle, running, working, tired, spent, estop.
- Source art preserved in `assets-src/`.
