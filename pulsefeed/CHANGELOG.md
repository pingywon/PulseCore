# Changelog

## 2.2.0 -- web responsiveness, adjustable rhythms, version drift

### Fixed
- **The dashboard's request pattern, not the device, was the lag.** CH2
  timing already runs in a dedicated 1 kHz task nothing in the UI or
  network path can preempt -- physical behaviour was never the problem.
  The web page polled `/api/v1/state` on a plain `setInterval(poll,1000)`
  with no guard against overlap, and the ESP32's WebServer handles
  exactly one TCP connection at a time by design (confirmed against the
  vendored library source, not assumed). When a round trip ran long the
  next tick fired anyway, so requests backed up and were served late, in
  whatever order they happened to queue in -- including E-STOP, which had
  no priority over a stale poll sitting in the same line.
  `web/src/index.html` now sends everything through one FIFO with exactly
  one request on the wire, chains each poll off the previous one's
  completion instead of a fixed interval, times out a wedged request
  after 4 s instead of hanging on it, and gives E-STOP a dedicated path
  that clears the line and aborts whatever's in flight before firing.
- `kVersion` had been hardcoded to `"2.0.0"` since before the 2.1.0 cycle
  started -- the boot screen, the web dashboard's System card and the API
  all reported a build that hadn't been true for two releases.
  `tools/sync_version.py` now stamps it from `VERSION` before every build
  (`tools/build.sh` runs it unconditionally, first), so this can't drift
  again. The existing boot-screen Milky animation already displayed
  whatever `kVersion` held; it was just wrong.

### Added
- 6 more built-in rhythms (24 -> 30): Fast Flutter, Slow Drip, Uneven,
  Pyramid, Micro Burst, Long Hold.
- Custom slots 8 -> 10.
- Built-in rhythms are no longer fixed forever: `POST /api/v1/custom
  action=seed&slot=N&from=<builtin id>` copies a preset's compiled timing
  into a slot as an editable tap-recorder pattern (`RhythmPattern::toCsv`,
  the inverse of the existing `compileCsv`), so it can be renamed,
  re-tapped over, or just played at a different Speed like any
  tap-recorded slot. Wired into the dashboard's renamed "Custom slots —
  adjustable" card as a preset picker + COPY TO SLOT button.

### Changed
- `hState`'s JSON scratch buffer 3072 B -> 6144 B. 10 slots at up to 320 B
  of raw pattern data each already exceeds the old buffer before the rest
  of the state object is counted; it was sitting at the edge even at 8.

## Unreleased -- consolidated into the PulseCore repo

No firmware behaviour change. Repository and build-tooling only.

### Changed
- Moved into `PulseCore/pulsefeed/`. The standalone `pingywon/pulsefeed` repo
  it used to live in was deleted; this tree was the only surviving copy.
- The v43 baseline is no longer duplicated here. It lives once, at
  `../software/43_Pulse_CoreS3_WebCustomRhythms_SDLog_CompileCheck/`, which is
  the more complete copy (it carries a README the old duplicate lacked).
  `tools/package.py` still ships it in the archive, as `v43_original/`.
- The 41 mascot renders and 4 UI/wiring SVGs are likewise no longer duplicated;
  `tools/build_assets.py` reads `../docs/assets` by default.

### Fixed
- `tools/package.py` had the archive version hardcoded to `2.0.0`, so it kept
  naming the zip `pulsefeed-2.0.0.zip` after VERSION moved to 2.1.0. It now
  reads `VERSION`.
- `tools/build_web.py` now gzips with `mtime=0`. Without it every build stamped
  a fresh timestamp into the gzip header, so `pf_web.h` showed as modified on
  every build even when no HTML had changed.

## 2.1.0 -- service modes + Milky

### Added
- **Service / test modes** (`pf_service.*`), long-specified and never built:
  manual per-channel firing including the spare/release pin, a pump sweep that
  finds the squeal floor for real hardware, and a demo jukebox over all 24
  rhythms. Reachable from the device (TEST on home) and `POST /api/v1/service`.
- Raw channel access in the HAL (`hal::setChannelRaw`) so service mode can
  reach the spare pin, which `Outputs` deliberately has no field for.
- **Milky on the device.** Six RGB565 moods (83 KB of flash) tied to session
  state along the brand's fatigue arc.
- `tools/build_assets.py`: 40 MB of source renders -> 680 KB of WebP + the
  device sprite header.

### Changed
- `MAX_BTNS` 20 -> 32. The service screen emits ~12 controls plus chrome and
  nav; the old cap would have silently truncated the table and left a drawn
  button unpressable.
- Flash 38% -> 41% (the mascot). RAM unchanged at 23%.

### Safety
- Service mode refuses to start while a session is running, auto-releases every
  channel after 8 s, times out after 3 min idle, and is dropped by E-STOP.
- The engine still ticks during service mode so the deadman and run limit stay
  live; service only takes the pins.

## 2.0.0 — full audit and reconstruction

### Fixed (defects found in v43)
- **Firmware did not link.** `changeRhythmSpeed(int)` was declared and called but never
  defined; the SLOW/FAST rhythm buttons pointed at a missing function. The v43 forward
  declaration is what moved the error from the compiler to the linker.
- **E-STOP did not de-energise.** It only opened a confirmation screen; all three channels
  stayed live until a second tap.
- **No watchdog.** A hung `loop()` left the solenoids in their last state indefinitely.
- **Synchronous WiFi scan inside an HTTP handler** stalled the control loop for seconds
  while the machine was running.
- **Output pins configured after `M5.begin()`**, leaving a power-on window.
- **Every screen touch wrote to SD**, including a 25 ms blocking `getLocalTime()` per line.
- **32-bit overflow** in the custom-rhythm clock; patterns jumped after ~4 h at 300% speed.
- **Custom patterns re-parsed ~30×/s** with `String` allocation, fragmenting the heap.
- **`rhythmName()` returned a pointer into a live `String`** that a web rename could free.
- **No authentication** on 20 endpoints; all state changes were CSRF-forgeable GETs.
- **Open setup access point** — full machine control to anyone in radio range.
- **JSON escaping missed control characters**, silently freezing the dashboard on some SSIDs.
- **Settings and Live screens were unreachable** from the touchscreen; `drawMoreScreen()`
  was never called at all.
- **~30 NVS keys rewritten on every save**, on every button release.
- **Web UI duplicated the firmware's rhythm table**, guaranteeing drift.
- Dead settings (`largeButtons`, `mobileMode`, `simpleHome`, `screenScale`, `graphSpeed`,
  `releaseMs`, `webRefreshSec`, five `show*` flags) removed or implemented.

### Added
- Portable, Arduino-free engine core with 89 host tests.
- Dedicated 1 kHz engine task, pinned, high priority; deadman on supervisor liveness.
- Adjustable pulsation ratio, 30:70 to 70:30.
- 20 kHz hardware PWM motor control with soft start.
- Configurable maximum run time, on by default; optional stop-on-network-loss.
- REST API v1 with PIN auth, bearer tokens, lockout and POST-only mutations.
- Gzipped web dashboard served from flash; new report page.
- On-device tap recorder; 8 custom slots (was 5); 24 built-in patterns (was 20).
- Persistent tab bar; full-frame canvas rendering.
- Host simulator running the real engine and serving the real UI and API.
- Safety-gated reverse proxy with a mode ladder, rate limiting and an audit log.
- Cloudflare Tunnel, nginx, Caddy and hardened systemd configurations.
- Marketing site and technical documentation.

### Changed
- Time base is 64-bit monotonic microseconds throughout.
- Vacuum ramp is a real time constant rather than a per-poll fraction.
- SD logging is queued and batched; drops are counted, never blocking.
- `stats.html` is served from flash instead of being written to the card on every boot.

### Deliberately unchanged
- `PF_OFF_MODE` reproduces v43's electrical behaviour. The driver board could not be
  verified here and guessing wrong energises channels. Confirm before changing.
