// =====================================================================
//  pf_log.h -- non-blocking SD session logger
//
//  v43 wrote to the SD card synchronously from wherever the event
//  happened. logButtonPress("screen touch") fired on EVERY touch and
//  went straight to SD.open/print x19/close, and the timestamp helper
//  called getLocalTime(&tm, 25) -- a 25 ms blocking wait -- on every
//  single line. Touching the screen therefore stalled the main loop for
//  tens of milliseconds, and the main loop was also what drove the
//  solenoid timing.
//
//  Here nothing on a control path touches the card. Producers enqueue a
//  fixed-size record; the supervisor drains the queue in batches with a
//  single open/close per flush.
// =====================================================================
#pragma once

#include "pf_config.h"
#include "pf_core.h"
#include <Arduino.h>

namespace pf {
namespace log {

struct Snapshot {
  int16_t  vacTarget;
  int16_t  vacActual;
  int16_t  ppm;
  int16_t  ratio;
  int16_t  motor;
  int16_t  rhythmId;
  uint8_t  flags;        // bit0 running, 1 vac, 2 pulse, 3 motor, 4 estop
  uint32_t heap;
};

enum : uint8_t {
  F_RUNNING = 1 << 0,
  F_VAC     = 1 << 1,
  F_PULSE   = 1 << 2,
  F_MOTOR   = 1 << 3,
  F_ESTOP   = 1 << 4,
};

// Bring up the card. Safe to call when no card is present.
bool begin();
bool cardPresent();
bool loggingReady();

// Enqueue an event. Never blocks, never allocates, safe from any task
// including the engine task. Drops the record (and counts the drop)
// rather than waiting if the queue is full.
void event(const char* name, const Snapshot& s);
void event(const char* name);          // uses the last published snapshot

// The supervisor publishes engine state here once per loop so that
// event() from a control path does not have to gather it.
void publish(const Snapshot& s);

// Periodic telemetry row while running.
void sample();

// Drain the queue to the card. Call from the supervisor loop only.
void service();

// --- introspection for the API ------------------------------------
uint32_t queued();
uint32_t written();
uint32_t dropped();
const char* currentPath();

// Enumerate /log/<date>/<date>.log files into a JSON array.
void listJson(JsonOut& j);

// Resolve an API-supplied log name to a real path. Returns false if the
// name is not a plain YYYY-MM-DD.log under /log.
bool resolvePath(const char* name, char* out, size_t outLen);

}  // namespace log
}  // namespace pf
