// =====================================================================
//  pf_hal.h -- hardware abstraction for the output channels and clock
//
//  The engine decides WHAT the channels should do; this decides HOW to
//  make the silicon do it. Keeping them apart is what lets the engine
//  be unit-tested on a host with no hardware at all.
// =====================================================================
#pragma once

#include "pf_config.h"
#include "pf_core.h"

namespace pf {
namespace hal {

// Monotonic microseconds since boot, 64-bit. Never wraps in any
// realistic device lifetime (584,000 years), which removes the entire
// class of millis()-rollover bugs by construction.
uint64_t nowUs();
uint32_t nowMs();

// Called as the very FIRST statement in setup(), before M5.begin().
//
// v43 ran M5.begin() -- which takes tens of milliseconds and brings up
// the PMIC, display and I2C -- before it ever touched the output pins.
// Until configureOutputPins() ran, all four channel GPIOs sat in their
// power-on default. Getting the channels into a known safe state before
// anything else happens costs nothing and closes that window.
void bootSafeOutputs();

// Full configuration once settings are known. Selects discrete or LEDC
// PWM driving per channel.
void configureOutputs(bool motorPwm, bool vacPwm);

// Drive the channels. Safe to call from the engine task.
void applyOutputs(const Outputs& o);

// Unconditional de-energise. Used by e-stop, fault paths and shutdown.
// Does not consult settings and cannot fail.
void allOff();

// Reflects what was last written, for telemetry.
Outputs lastApplied();

// True once configureOutputs() has run.
bool ready();

}  // namespace hal
}  // namespace pf
