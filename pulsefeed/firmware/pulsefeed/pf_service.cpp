// =====================================================================
//  pf_service.cpp -- Service / Test modes
// =====================================================================
#include "pf_service.h"
#include "pf_app.h"

#include <Arduino.h>
#include <string.h>

namespace pf {
namespace service {

namespace {

Mode     g_mode = SVC_OFF;
uint32_t g_lastTouchMs = 0;

// Per-channel hold deadlines. 0 = not held.
uint32_t g_until[hal::CH_COUNT] = { 0, 0, 0, 0 };

// sweep
uint32_t g_sweepStartMs = 0;
int      g_sweepPct     = 0;
int      g_sweepMark    = -1;

// demo
int      g_demoId       = 1;
uint32_t g_demoStepMs   = 0;
uint32_t g_demoLastMs   = 0;

char     g_status[72] = "";

void touch() { g_lastTouchMs = millis(); }

void setStatus(const char* s) {
  strncpy(g_status, s, sizeof(g_status) - 1);
  g_status[sizeof(g_status) - 1] = '\0';
}

}  // namespace

// ------------------------------------------------------------------ //
bool enter(Mode m) {
  // Never while the machine is actually running a session. Direct pin
  // control and a live cadence are two hands on the same wheel.
  if (app.engine.running()) {
    setStatus("Stop the session before entering service");
    return false;
  }
  if (app.engine.estop()) {
    setStatus("Clear the E-STOP first");
    return false;
  }
  g_mode = m;
  touch();
  allChannelsOff();
  g_sweepPct = 0;
  g_sweepMark = -1;
  g_demoStepMs = 0;
  switch (m) {
    case SVC_MANUAL: setStatus("Manual valve test - hold to fire"); break;
    case SVC_SWEEP:  setStatus("Pump sweep - mark where it runs clean"); break;
    case SVC_DEMO:   setStatus("Demo mode - cycling patterns"); break;
    default:         setStatus(""); break;
  }
  log::event(m == SVC_MANUAL ? "service-manual"
           : m == SVC_SWEEP  ? "service-sweep"
           : m == SVC_DEMO   ? "service-demo" : "service-off");
  return true;
}

void exit() {
  if (g_mode == SVC_OFF) return;
  g_mode = SVC_OFF;
  allChannelsOff();
  hal::allOff();
  setStatus("Service mode off");
  log::event("service-exit");
}

bool active() { return g_mode != SVC_OFF; }
Mode mode()   { return g_mode; }

// ------------------------------------------------------------------ //
//  Manual
// ------------------------------------------------------------------ //
void allChannelsOff() {
  for (int i = 0; i < hal::CH_COUNT; i++) {
    g_until[i] = 0;
    hal::setChannelRaw(i, false);
  }
}

void hold(int ch, bool on) {
  if (g_mode != SVC_MANUAL) return;
  if (ch < 0 || ch >= hal::CH_COUNT) return;
  touch();
  if (on) {
    g_until[ch] = millis() + kMaxHoldMs;
    hal::setChannelRaw(ch, true);
  } else {
    g_until[ch] = 0;
    hal::setChannelRaw(ch, false);
  }
}

void pulse(int ch, uint32_t ms) {
  if (g_mode != SVC_MANUAL) return;
  if (ch < 0 || ch >= hal::CH_COUNT) return;
  touch();
  ms = (uint32_t)clampi((int)ms, 20, (int)kMaxHoldMs);
  g_until[ch] = millis() + ms;
  hal::setChannelRaw(ch, true);
}

// ------------------------------------------------------------------ //
//  Pump sweep
// ------------------------------------------------------------------ //
void sweepStart() {
  if (g_mode != SVC_SWEEP) return;
  touch();
  g_sweepStartMs = millis();
  g_sweepPct = 0;
  g_sweepMark = -1;
  setStatus("Sweeping - listen for where it stops squealing");
}

void sweepMark() {
  if (g_mode != SVC_SWEEP) return;
  touch();
  g_sweepMark = g_sweepPct;
  // A little headroom under the marked point, because the operator
  // marks where it *starts* sounding right. v2.0 has nowhere to store
  // this yet -- pumpFloor arrives with the v3 cadence core -- so it is
  // reported rather than silently written to a field that would not be
  // read.
  int floorPct = clampi(g_sweepMark - 5, 0, 90);
  char buf[72];
  snprintf(buf, sizeof(buf), "Marked %d%% - suggested pump floor %d%%",
           g_sweepMark, floorPct);
  setStatus(buf);
  log::event("service-sweep-mark");
}

int sweepPercent() { return g_sweepPct; }
int sweepMarked()  { return g_sweepMark; }

// ------------------------------------------------------------------ //
//  Demo jukebox
// ------------------------------------------------------------------ //
void demoNext() {
  if (g_mode != SVC_DEMO) return;
  touch();
  g_demoId++;
  if (g_demoId > kBuiltinCount) g_demoId = 1;
  g_demoStepMs = millis();
  char buf[72];
  snprintf(buf, sizeof(buf), "%d/%d  %s", g_demoId, kBuiltinCount, builtinName(g_demoId));
  setStatus(buf);
}

int demoRhythm() { return g_demoId; }

// ------------------------------------------------------------------ //
//  Tick -- runs inside the 1 kHz engine task
// ------------------------------------------------------------------ //
bool tick(uint64_t nowUs) {
  if (g_mode == SVC_OFF) return false;

  // E-STOP wins over everything, always.
  if (app.engine.estop()) {
    allChannelsOff();
    hal::allOff();
    g_mode = SVC_OFF;
    setStatus("Service mode dropped by E-STOP");
    return true;
  }

  uint32_t now = millis();

  // Whole-mode idle timeout.
  if (now - g_lastTouchMs > kIdleTimeoutMs) {
    exit();
    setStatus("Service mode timed out");
    return true;
  }

  switch (g_mode) {
    case SVC_MANUAL: {
      // Release anything whose hold has expired. This is the backstop
      // that makes a dropped connection or a wedged UI safe: nothing
      // stays energised because someone stopped asking for it.
      for (int i = 0; i < hal::CH_COUNT; i++) {
        if (g_until[i] && (int32_t)(now - g_until[i]) >= 0) {
          g_until[i] = 0;
          hal::setChannelRaw(i, false);
        }
      }
      break;
    }

    case SVC_SWEEP: {
      if (g_sweepStartMs == 0) { hal::setMotorRaw(0); break; }
      uint32_t el = now - g_sweepStartMs;
      if (el >= kSweepMs) {
        g_sweepPct = 100;
        hal::setMotorRaw(255);
        // Do not sit at full forever.
        if (el >= kSweepMs + 3000) {
          g_sweepStartMs = 0;
          g_sweepPct = 0;
          hal::setMotorRaw(0);
          if (g_sweepMark < 0) setStatus("Sweep finished - not marked");
        }
      } else {
        g_sweepPct = (int)(el * 100 / kSweepMs);
        hal::setMotorRaw((uint8_t)clampi(g_sweepPct * 255 / 100, 0, 255));
      }
      break;
    }

    case SVC_DEMO: {
      // Walk each built-in pattern for a few seconds on CH2 only. No
      // vacuum, no motor -- this is a showcase, not a session.
      if (g_demoStepMs == 0) { g_demoStepMs = now; }
      if (now - g_demoStepMs > 6000) demoNext();

      const RhythmPattern& pat = app.builtins[g_demoId - 1];
      bool on = false;
      if (pat.valid()) {
        uint64_t el = (uint64_t)(now - g_demoStepMs) * 1000ULL;
        on = pat.pulseAt(el, app.settings.rhythmSpeed);
      }
      hal::setChannelRaw(hal::CH_PULSE, on);
      break;
    }

    default: break;
  }

  (void)nowUs;
  return true;
}

const char* modeName() {
  switch (g_mode) {
    case SVC_MANUAL: return "MANUAL";
    case SVC_SWEEP:  return "SWEEP";
    case SVC_DEMO:   return "DEMO";
    default:         return "OFF";
  }
}

const char* statusLine() { return g_status; }

}  // namespace service
}  // namespace pf
