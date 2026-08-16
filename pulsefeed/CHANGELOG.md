# Changelog

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
