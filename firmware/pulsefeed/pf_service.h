// =====================================================================
//  pf_service.h -- Service / Test modes
//
//  The original design notes asked for exactly this and it was never
//  built. From the old-controller teardown:
//
//      "That would let an ESP32/CoreS3 control: pulse speed, pulse hold,
//       release time, duty cycle, test firing, different modes"
//
//  and from the website brief:
//
//      "Service/check screens: future addition for firing individual
//       valves, verifying pump behavior, and checking release function."
//
//  Three things live here:
//
//    1. MANUAL   fire any channel by hand, momentary or timed. This is
//                how you verify wiring and polarity per channel before
//                you ever connect a load -- including the spare pin that
//                v43 left dead, which is the release valve in the
//                four-solenoid design.
//
//    2. SWEEP    ramp the pump from zero to full while you listen. The
//                reference pump "squeals and is useless until at least
//                75% power"; this finds that floor for YOUR hardware
//                instead of guessing it. Mark the clean point and it
//                writes the value back into settings.
//
//    3. DEMO     a jukebox that walks every rhythm pattern in turn, with
//                the mascot reacting. No load required, nothing to set
//                up -- for showing the thing off.
//
//  SAFETY ENVELOPE
//  Service mode bypasses the cadence engine and drives pins directly, so
//  it is fenced:
//    * refuses to start while a session is running
//    * every channel auto-releases after kMaxHoldMs
//    * the whole mode times out after kIdleTimeoutMs
//    * E-STOP cuts it exactly like anything else
//    * leaving the mode de-energises everything unconditionally
// =====================================================================
#pragma once

#include "pf_core.h"
#include "pf_hal.h"

namespace pf {
namespace service {

enum Mode {
  SVC_OFF = 0,
  SVC_MANUAL,
  SVC_SWEEP,
  SVC_DEMO,
};

// No single channel stays energised longer than this without the
// operator re-asserting it.
static const uint32_t kMaxHoldMs      = 8000;
// The whole mode drops out if nothing is touched for this long.
static const uint32_t kIdleTimeoutMs  = 180000;
static const uint32_t kSweepMs        = 12000;

bool enter(Mode m);          // false if a session is running
void exit();
bool active();
Mode mode();

// --- manual -------------------------------------------------------
void hold(int ch, bool on);            // momentary, auto-releases
void pulse(int ch, uint32_t ms);       // timed one-shot
void allChannelsOff();

// --- sweep --------------------------------------------------------
void sweepStart();
void sweepMark();                      // "it runs clean here"
int  sweepPercent();                   // current ramp position, 0-100
int  sweepMarked();                    // -1 until marked

// --- demo ---------------------------------------------------------
void demoNext();
int  demoRhythm();

// Called from the engine task. Returns true if service owns the outputs
// this tick, in which case it has already driven them.
bool tick(uint64_t nowUs);

const char* modeName();
const char* statusLine();

}  // namespace service
}  // namespace pf
