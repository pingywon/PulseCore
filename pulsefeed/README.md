# PulseFeed — Pluto 9000

**A programmable three-channel vacuum, pulsation and motor controller for M5Stack CoreS3.**

Firmware v2.0.0 · API level 1 · 89 host tests · compiles to 1.23 MB (38% flash, 23% RAM)

---

## What this is

You handed me `pulsefeed/` containing one directory, one 2,887-line Arduino sketch and a
changelog. I audited it, concluded what the product was trying to be, and rebuilt it.

This document is the audit, the reconstruction, and the reasoning along the way. It is
long because you asked for the thought process, and because one of the findings is that
the previous version's own changelog was confidently wrong about the thing it claimed to
have fixed.

**Everything here builds and runs.** The firmware compiles against the real ESP32-S3
toolchain. The engine is covered by tests that execute. The simulator and the bridge are
running processes I exercised end to end, not sketches.

---

## Contents

| Path | What |
|---|---|
| `firmware/pulsefeed/` | The firmware. 19 files, ~5,100 lines. |
| `tests/test_core.cpp` | 89 host assertions against the engine the firmware ships. |
| `sim/pulsefeed-sim.cpp` | Host simulator: real engine, real API, real UI, no hardware. |
| `bridge/` | Safety-gated reverse proxy + Cloudflare/nginx/Caddy/systemd configs. |
| `web/src/` | Dashboard and report page. Gzipped into the firmware at build time. |
| `site/` | Marketing site and technical documentation. |
| `tools/` | Build driver, web-asset packer, archive packer. |
| `../software/43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck/` | The original v43 sketch, untouched, for diffing. Lives once, in the archive. |

---

## Part 1 — The audit

### What I concluded the product is

The vocabulary settles it: three channels driving a **vacuum solenoid**, a **pulsator**
measured in **pulses per minute**, and a **motor** — with a milk-bottle mascot called
Milky in the boot animation. This is a milking-system controller. More generally it is a
programmable vacuum-and-pulsation rig, and I have written the marketing copy to describe
the capability rather than narrow it to one animal.

I kept the identity you already had and reconciled the three names in the codebase:
**PulseFeed** is the product, **Pluto 9000** is the controller model. The directory you
gave me was `pulsefeed`, so that became the top-level name.

### Finding 0 — the firmware did not build

This is the one that reframes everything else.

`CHANGES_v43.txt` states the version's purpose was to *"reduce compile risk in Arduino IDE
by adding explicit forward declarations for functions used before they are defined"*, and
lists `changeRhythmSpeed()` among the functions it declared.

`changeRhythmSpeed()` is declared on line 236. It is called on lines 1733 and 1734. **It
is never defined anywhere in the file.**

```
undefined reference to `_Z17changeRhythmSpeedi'
collect2: error: ld returned 1 exit status
```

That is a real link failure from the real toolchain, on your unmodified sketch. Two
consequences:

1. The version named `CompileCheck` cannot be compiled, so it was never running on
   hardware in this form.
2. **Adding the prototype is what hid the bug.** Without it the compiler would have said
   "not declared in this scope" at line 1733 and pointed straight at the problem. With it,
   the compiler was satisfied and the error moved to the linker, where the message names a
   mangled symbol and no line in the source. The stated fix caused the failure it was
   meant to prevent.

The buttons wired to that missing function were SLOW and FAST on the Rhythm screen.

I verified this the boring way — installed `arduino-cli`, the ESP32 core and the M5
libraries, and built your sketch as-is before I wrote a line of my own. Establishing that
baseline was worth the 2m44s.

### The rest of the audit

Ordered by how much I'd want them fixed before connecting a load.

#### Safety and correctness

**1. The E-STOP did not stop anything.** The top-bar STOP button called
`setScreen(SCREEN_ESTOP_CONFIRM)` and nothing else. All three channels stayed energised
through the confirmation screen until a second tap. A control labelled E-STOP that opens a
menu is worse than no control, because an operator will trust it.

**2. Nothing stopped a hung loop.** Pulse timing, `server.handleClient()`, SD writes and
screen repaints all lived in the same `loop()`. If any of them wedged, the solenoids stayed
in whatever state they were last written. There was no watchdog, no deadman, no timeout.

**3. A WiFi scan froze the machine mid-pulse.** `handleWifiScan()` called
`WiFi.scanNetworks(false, true, false, 350)` — synchronous, ~350 ms per channel across 14
channels. `loop()` does not run during that. If the machine was running, the solenoids held
their last state for several seconds.

**4. Pulse timing was hostage to the UI.** Even without a scan, `updateProgramControl()`
shared a loop with a full-screen repaint and blocking SD writes. Pulsation rate *is* the
product; it was the least protected thing in the design.

**5. Boot-time output window.** `configureOutputPins()` ran after `M5.begin()`, which
brings up the PMIC, display and I2C. Until then the four channel GPIOs sat at their
power-on default.

**6. Every touch wrote to the SD card.** `handleTouch()` opened with
`logButtonPress("screen touch")`, which reached `appendCsvLine()` → 19 `print()` calls →
`close()`. And `dailyLogPath()` called `getLocalTime(&timeinfo, 25)` — a **25 ms blocking
wait** — on every line. Every screen tap cost tens of milliseconds in the loop that was
also timing the solenoids.

**7. 32-bit overflow in the custom-rhythm clock.**
`scaled = (elapsed * rhythmSpeedPct) / 100UL` in 32-bit arithmetic. At 300% speed it wraps
after roughly four hours and the pattern jumps to a random position. Now 64-bit, with a
test that walks 24 simulated hours.

**8. Custom patterns were re-parsed on every tick.** `customRhythmPulseNow()` ran
`indexOf`/`substring` over an Arduino `String` roughly 30 times a second, allocating each
time. On an ESP32 that fragments the heap until the web server can no longer allocate a
response buffer — a device that works fine for an hour and then stops serving pages.

**9. Dangling pointer across contexts.** `rhythmName()` returned
`customRhythmName[slot].c_str()` — a pointer into a live `String` that a web rename could
reallocate while the display was printing it.

#### Security

**10. No authentication of any kind.** Twenty endpoints, all open. `/api/run?state=1`,
`/api/estop`, `/api/set`, `/api/sdformat`. Anyone on the LAN could start the machine.

**11. Every state change was a GET.** So they are forgeable cross-site: any page in any
other tab could have started the machine with `<img src="http://.../api/run?state=1">`.
You asked for internet exposure. These two together made that unsafe to build.

**12. The setup access point was open.** `WiFi.softAP(SETUP_AP_SSID)` with no password —
full machine control to anyone in radio range.

**13. Broken JSON escaping.** `safeJsonString()` escaped `\` and `"` only. A scanned SSID
containing a control byte — which happens — produced invalid JSON, the dashboard's
`poll()` died in its empty `catch`, and the whole UI silently froze with no error shown.
The custom rhythm name in `handleState()` wasn't escaped at all.

#### Function and UX

**14. Two screens were unreachable.** `drawSettingsScreen()` and `drawLiveScreen()` are
fully implemented. Nothing on the touchscreen navigates to `SCREEN_SETTINGS` or
`SCREEN_LIVE`. `drawMoreScreen()` exists and is never called by `drawCurrentScreen()` at
all. Theme, backlight and the live graphs were device-unreachable.

**15. Coordinates duplicated by hand.** `drawThemeModeButton(182, 39, 124, 40)` in the
draw path; `insideRect(x, y, 182, 39, 124, 40)` in the touch path, 1,000 lines away. Four
literals per control with nothing keeping them in step.

**16. Dead settings.** `largeButtons`, `mobileMode`, `simpleHome`, `showAdvancedWeb`,
`screenScale`, `graphSpeed`, `releaseMs`, `webRefreshSec` and the five `show*` flags were
all loaded, saved, exposed over the API — and read by nothing.

**17. The motor was not speed-controlled.** A 500 ms software PWM window is a 2 Hz square
wave. That is not speed control; it is chugging, and it is hard on brushes.

**18. Pulsation ratio was fixed at 50:50.** Hardcoded `onMs = period / 2`. Ratio is a
first-class parameter of any real pulsator and there was no way to set it.

**19. Flash wear.** `saveSettingsNow()` rewrote ~30 NVS keys every time, and it was called
on every button release and 900 ms after every slider move. NVS is rated around 100k erase
cycles.

**20. The web UI duplicated the firmware's rhythm table** as a hardcoded JS array.
Inserting one rhythm in the firmware would have silently mislabelled every later entry in
the dropdown.

**21. `handleSdFormat()` does not format anything.** It deletes two files. The name
promises something the code does not do.

**22. 22 KB of HTML rebuilt per request.** `htmlPage()` concatenated an Arduino `String`
with `reserve(22000)`, then `server.send()` copied it again — two large heap allocations
per page load, on the heap the control loop shares.

---

## Part 2 — The reconstruction

### The decision everything else follows from

**Split the safety-critical core out of Arduino entirely.**

`pf_core.h` / `pf_core.cpp` contain the settings model, the rhythm compiler, the channel
engine and the JSON writer. They include `<stdint.h>` and `<math.h>` and nothing else. No
`Arduino.h`, no `String`, no `M5`, no allocation.

That one constraint bought a lot:

- The engine compiles on this Linux box, so **pulse timing is testable**. I can assert
  that 400 PPM produces 200 pulses in 30 seconds instead of hoping.
- The simulator links the same `.cpp` the firmware links. Not a reimplementation that
  drifts — the same object code.
- It forced the hardware coupling out into a HAL, which is what made the engine task
  possible.

Time is `uint64_t` microseconds throughout, supplied by `esp_timer_get_time()` on the
device and `steady_clock` on the host. That deletes the entire `millis()`-rollover bug
class by construction rather than by careful comparison.

### Architecture

```
Core 1, prio 19   pf_engine task, 1 kHz — the only writer of the output pins
Core 1, prio  1   loop() — UI, touch, HTTP, DNS, SD flush, persistence
Core 0, prio 23   WiFi / lwIP (Arduino default)
```

`loop()` calls `engine.feed()` every pass. If it stops for 2.5 s the engine stops the
machine itself and records `STOP_WATCHDOG`. The supervisor's liveness is now a machine
input, so a wedged UI or network stack cannot leave a channel energised.

I chose core 1 over core 0 deliberately: core 0 hosts the WiFi stack at priority 23, and I
did not want the engine and the radio contending. On core 1 the engine only has to preempt
`loopTask`, which it does trivially.

**On the settings race.** The engine task reads `Settings` while `loop()` may be writing
it, with no lock. That is deliberate. Every field is independently clamped *before* it is
stored, so any interleaving of old and new fields is itself a valid configuration — the
worst case is one 1 ms tick at a slightly stale rate. The one place that isn't true is the
compiled rhythm patterns, because `compileCsv()` clears the step array before refilling it;
`recompileCustom()` therefore takes a pattern off the air before rebuilding it. A mutex at
1 kHz to protect against a benign interleaving would have been cargo-cult.

### What changed

| | v43 | v2.0 |
|---|---|---|
| Build | does not link | clean; 1.23 MB flash, 23% RAM |
| Structure | 1 file, 2,887 lines | 19 files, ~5,100 lines |
| Pulse timing | in `loop()` with HTTP and SD | dedicated 1 kHz pinned task |
| Tests | none | 89 assertions on the shipped engine |
| E-stop | opened a menu | de-energises on press, then latches |
| Watchdog | none | deadman + run limit + task WDT |
| Motor | 2 Hz bit-bang | 20 kHz LEDC PWM + soft start |
| Ratio | fixed 50:50 | 30:70 – 70:30 |
| Rhythms | 20 built-in + 5 custom | 24 built-in + 8 custom |
| Custom patterns | re-parsed ~30×/s | compiled once on change |
| Rhythm clock | 32-bit, wrapped at ~4 h | 64-bit, tested to 24 h |
| Web auth | none | PIN + bearer tokens + lockout |
| Mutations | GET (CSRF-forgeable) | POST + header token |
| Setup AP | open | WPA2, per-device key |
| Web assets | 22 KB String per request | 11 KB gzipped in flash, zero heap |
| SD logging | blocking, on every touch | queued, batched, drop-counted |
| NVS | ~30 keys per save | changed keys only |
| Screens | 2 unreachable | persistent tab bar, all reachable |
| Button coords | duplicated by hand | one layout table, draw + hit-test |
| Rendering | direct, visible tearing | full-frame canvas, single push |

### Core functions

**Pulsation ratio.** The genuinely new capability. `ratio` sets the percentage of each
period spent in the on-phase, 30–70%. Measured duty is asserted at five settings to within
0.5%.

**Real motor control.** LEDC at 20 kHz — above audible — with a configurable soft-start
ramp so the motor doesn't slam to full. The engine emits a duty value; the HAL decides
whether that becomes PWM or a discrete pin.

**Time-based vacuum ramp.** v43 applied a fixed fraction per 35 ms poll, so the physical
ramp rate changed with UI load. Now it's a first-order lag with a real time constant
(`2200 ms / vacRamp`). Tested at 200 Hz and 2 kHz tick rates: within 5% of each other.

**Rhythm compiler.** Dot/dash and tap-recorded CSV both compile to explicit millisecond
steps, once, on change. Notation semantics preserved exactly so existing patterns read the
same.

**Run limit.** Defaults to 60 minutes. Disabling it is an explicit choice, not the default.

### Safety, honestly

The firmware guarantees: pins safe before any other init; e-stop cuts hardware before
touching the model; a stopped or latched engine returns all-off ahead of every other
computation in `tick()`; every setting clamped on load, on write and before save; deadman
and run limit both stop and record why.

It cannot guarantee: anything about a shorted driver, a welded relay or a failed MOSFET.
**Anything that must fail safe needs a hardware interlock that does not pass through the
microcontroller.** This is prototype software with no third-party assessment.

**One thing I deliberately did not change.** `PF_OFF_MODE` defaults to `PF_OFF_HIGHZ`,
reproducing v43's exact electrical behaviour. Your comments say the board is
ground-switched through an optocoupler and that OFF is high-impedance because you didn't
want the MCU sourcing current into the opto input. I can't verify that board from here, and
the failure mode of guessing wrong is *channels energise unexpectedly*. So I left it,
documented the alternatives (`PF_OFF_PULLUP`, `PF_OFF_DRIVEN`), and flagged that hardware
PWM requires push-pull because PWM has no high-impedance state. **Confirm the polarity on a
meter with the loads disconnected before enabling PWM on a channel.**

### The device UI

Three fixed bands: a 34 px top bar (back, title, signal, and an E-STOP that is always in
the same place), a 160 px body, and a 46 px tab bar that is always visible —
`HOME · VAC · PULSE · MOTOR · RHYTHM`. The four things you adjust constantly are one tap
away from anywhere; Graphs, Network and Settings live on Home. That fixes the unreachable
screens and gives the layout a reason.

Every control is a row in a table with an id, a rect, a label and a style. `emit()` draws it
and registers it for hit-testing in the same call, so a control cannot be drawn somewhere it
can't be pressed. The whole frame composes into an off-screen canvas and pushes once.

New on-device: a **tap recorder** screen. v43 could only record custom rhythms from the web
page; now the big pad on the device records press duration and inter-tap gaps directly.

### The web stack

`index.html` is 23 KB of self-contained dashboard, gzipped to 8.1 KB and served straight
from flash with `send_P`. Zero heap. It fetches `/api/v1/meta` once for the rhythm table —
**from the firmware**, so the browser can't drift out of step the way the hardcoded JS array
did.

Slider input is coalesced into one request per 90 ms. Dragging a range emits an event per
pixel, and one HTTP request per pixel is how you make an embedded server look broken.

Auth: six-digit PIN generated on first boot, shown on the device's Network screen, exchanged
for a 12-hour bearer token. Mutations require the token in an `X-PF-Token` **header** —
a cross-origin form cannot set custom headers, so that is the CSRF defence too. No cookies
anywhere, so there is no cookie to forge. Five wrong PINs locks the endpoint for a minute.

### The bridge

You asked to proxy the web server across the internet. A plain `proxy_pass` in front of a
machine that opens valves means one leaked URL is one request away from starting it, so the
bridge is more than a forwarder:

- **Its own auth**, separate from the device PIN. `--device-pin` is exchanged for a token
  once at startup, so visitors never see the PIN.
- **A mode ladder** capping what remote clients can do. `readonly` → `supervise` →
  `control` → `full`. Default is `supervise`, which **cannot start the machine**. `full`
  refuses to run without `--i-understand` on the command line.
- **Stop and e-stop work in every mode, including read-only.** If remote access can see a
  problem it must always be able to stop it.
- `supervise` verifies against live state that a setting change *reduces* output. Turning
  things down is fine remotely; turning them up is a decision for whoever is in the room.
- Per-IP rate limiting, CIDR allowlist, `X-Forwarded-For` believed only from configured
  trusted proxies, and an append-only JSONL audit log of every mutation.
- `/api/v1/wifi/*` is blocked in every mode — reconfiguring a device's network from the far
  side of the internet is never what you want.

Verified against the running simulator:

```
POST settings ppm=200 (RAISE)      -> 403  supervise mode may only reduce ppm (current 120, requested 200)
POST settings ppm=60  (LOWER)      -> 200
POST control action=start          -> 403  action 'start' not permitted in mode 'supervise'
POST control action=estop          -> 200  <- and the machine actually stopped
POST wifi/join                     -> 403  blocked_by_bridge
POST system reboot                 -> 403  requires_mode_full
```

Ready-to-use Cloudflare Tunnel, nginx, Caddy and hardened systemd configs are in
`bridge/tunnel/` and `bridge/systemd/`. **Point the tunnel at the bridge, never at the
device** — otherwise you've bypassed all of the above.

---

## Part 3 — Using it

### Service / test modes

The old dev's notes asked for these twice -- *"test firing"* in the hardware
teardown and *"Service/check screens: firing individual valves, verifying pump
behavior, and checking release function"* in the website brief -- and they had
never been built. They are on the device under **TEST** on the home screen, and
on the API.

**MANUAL** fires any channel by hand, including the spare pin v43 left dead
(the release valve in the four-solenoid design). This is how you confirm
polarity per channel *with the loads disconnected* -- which is the open item
everything else has been waiting on.

**PUMP** ramps the motor 0 to 100% over 12 s while you listen, and you tap MARK
where it stops squealing. The reference pump is useless below ~75%; this finds
that number for your hardware instead of guessing it. It reports a suggested
floor rather than writing it, because v2.0 has no `pumpFloor` field yet -- that
arrives with the v3 cadence core.

**DEMO** walks all 24 built-in rhythms on CH2 only, with Milky reacting. No
vacuum, no motor, nothing to set up.

Direct pin control is fenced: it refuses to start while a session is running,
every channel auto-releases after 8 s, the whole mode times out after 3 min,
and E-STOP drops it like anything else.

```bash
T=$(curl -s -X POST -d "pin=123456" http://pulsefeed.local/api/v1/auth | jq -r .token)
S(){ curl -s -X POST -H "X-PF-Token: $T" -d "$1" http://pulsefeed.local/api/v1/service; }

S "action=manual"
S "action=pulse&ch=3&ms=300"     # fire the spare/release valve for 300 ms
S "action=sweep"; S "action=sweepStart"
S "action=demo";  S "action=demoNext"
S "action=exit"
```

### Milky

The mascot art from the plusecore repo is in the build. `tools/build_assets.py`
turns 40 MB of source renders into 680 KB of WebP for the web and six RGB565
moods in 83 KB of flash. On the device Milky's mood follows the session along
the brand's fatigue arc -- fresh, running, working, tired past 20 minutes,
spent past 40, and a shrug on E-STOP.

### Run the tests

```bash
tools/build.sh test
```

89 assertions against the exact engine the firmware ships: pulse counts at 30/60/120/240/400
PPM within one pulse; measured duty at five ratios within 0.5%; pattern position exact across
24 simulated hours at 300% speed; e-stop, deadman, run limit; ramp rate independent of tick
rate; a stopped engine energising nothing at any setting; JSON escaping of control bytes and
invalid UTF-8.

> Three of these failed on the first run. All three were wrong expectations in my *test* —
> I'd hand-computed a pattern's duty ignoring that spaces widen the preceding rest. I
> verified the compiler's output independently in Python before changing anything, and fixed
> the tests, not the core. Worth stating plainly: the failures were mine, and the core was
> right.

### Try it without hardware

```bash
tools/build.sh sim
./dist/pulsefeed-sim --port 8080 --pin 246813
```

Runs the real engine at 1 kHz and serves the real dashboard and API. This is also how you
develop against the bridge.

### Build and flash

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install M5Unified M5GFX

tools/build.sh firmware
tools/build.sh flash /dev/ttyACM0
```

Edit `web/src/*.html` → run `python3 tools/build_web.py` → rebuild.

### First boot

1. The device comes up local-first — channels usable immediately, no waiting on WiFi.
2. It raises a WPA2 setup AP named `PulseFeed-XXXXXX`. **The key and the web PIN are on
   the Network screen.**
3. Join it, open `http://192.168.4.1`, enter the PIN, join your own network from the
   Network card.
4. After that it's at `http://pulsefeed.local` or its LAN address.

### Share it

```bash
./bridge/pulsefeed-bridge.py --target http://192.168.13.50 \
    --listen 127.0.0.1:8443 --mode readonly \
    --device-pin 123456 --audit /var/log/pulsefeed/audit.jsonl

cloudflared tunnel run pulsefeed
```

---

## What I'd do next

Ordered by value, and none of it is started:

1. **Verify output polarity on real hardware.** Highest priority; everything else assumes it.
2. **A hardware interlock.** A watchdog relay in series with the channel supply that needs a
   heartbeat from the MCU. Software cannot cover a shorted driver, and this product wants
   that coverage.
3. **OTA updates.** Scaffolding is there (mDNS, the deferred-command pattern); the flashing
   path isn't.
4. **Closed-loop vacuum.** `vacActual` is currently a *model*, not a measurement. With a
   pressure sensor it becomes real feedback, and that changes the product.
5. **Server-sent events** instead of 1 Hz polling, if the load ever justifies dropping
   `WebServer.h` for an async server.
6. **A pulsation-ratio limit tied to PPM.** At 400 PPM with a 30% ratio the on-phase is
   45 ms; whether that's mechanically sensible for your valves is a bench question I can't
   answer from here.

## Known limitations

- The engine is tested; the UI, the HTTP layer and the SD logger are not. They need
  hardware or a much larger harness.
- `WebServer.h` is single-connection. Two browsers polling at 1 Hz is fine; ten is not. The
  bridge's rate limiter is partly there to protect against that.
- Timestamps before NTP or an RTC sync fall back to uptime. Log rows are ordered correctly
  but not wall-clock accurate until the device has a network.
- I have no CoreS3 here. Everything hardware-facing is verified by compilation and by
  reading the M5GFX/ESP32 APIs, not by watching it run.

---

## Licence

MIT — see `LICENSE`.

**Pluto 9000 is prototype hardware and prototype software.** Not a medical device, not a
certified safety system, no warranty of fitness for any particular purpose.
