// =====================================================================
//  pf_hal.cpp -- output driving and clock
// =====================================================================
#include "pf_hal.h"
#include <esp_timer.h>

namespace pf {
namespace hal {

namespace {

bool    g_ready      = false;
bool    g_motorPwm   = false;
bool    g_vacPwm     = false;
Outputs g_last       = { false, false, false, 0, 0 };

const int kInactiveLevel = (PF_ACTIVE_LEVEL == LOW) ? HIGH : LOW;

// Put one channel into its de-energised state, honouring PF_OFF_MODE.
inline void pinOff(int pin) {
#if PF_OFF_MODE == PF_OFF_HIGHZ
  pinMode(pin, INPUT);
#elif PF_OFF_MODE == PF_OFF_PULLUP
  // Weak hold at the inactive level. Prevents a floating opto input
  // from drifting, without sourcing enough current to turn it on.
  if (PF_ACTIVE_LEVEL == LOW) pinMode(pin, INPUT_PULLUP);
  else                        pinMode(pin, INPUT_PULLDOWN);
#else
  pinMode(pin, OUTPUT);
  digitalWrite(pin, kInactiveLevel);
#endif
}

inline void pinOn(int pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, PF_ACTIVE_LEVEL);
}

// Duty is expressed 0..255 as "amount of ON". With active-low channels
// the LEDC comparator value has to be inverted.
inline uint32_t ledcDutyFor(uint8_t on) {
  return (PF_ACTIVE_LEVEL == LOW) ? (uint32_t)(255 - on) : (uint32_t)on;
}

}  // namespace

uint64_t nowUs() { return (uint64_t)esp_timer_get_time(); }
uint32_t nowMs() { return (uint32_t)(nowUs() / 1000ULL); }

void bootSafeOutputs() {
  pinOff(PF_PIN_VAC);
  pinOff(PF_PIN_PULSE);
  pinOff(PF_PIN_MOTOR);
  pinOff(PF_PIN_SPARE);
  g_last = Outputs{ false, false, false, 0, 0 };
}

void configureOutputs(bool motorPwm, bool vacPwm) {
  // Tear down any previous LEDC attachment before re-deciding.
  if (g_motorPwm) ledcDetach(PF_PIN_MOTOR);
  if (g_vacPwm)   ledcDetach(PF_PIN_VAC);

  g_motorPwm = motorPwm;
  g_vacPwm   = vacPwm;

  bootSafeOutputs();

  if (g_motorPwm) {
    ledcAttachChannel(PF_PIN_MOTOR, PF_LEDC_MOTOR_HZ, PF_LEDC_BITS, PF_LEDC_MOTOR_CH);
    ledcWrite(PF_PIN_MOTOR, ledcDutyFor(0));
  }
  if (g_vacPwm) {
    ledcAttachChannel(PF_PIN_VAC, PF_LEDC_VAC_HZ, PF_LEDC_BITS, PF_LEDC_VAC_CH);
    ledcWrite(PF_PIN_VAC, ledcDutyFor(0));
  }

  // The spare channel is held off unconditionally and never exposed.
  pinOff(PF_PIN_SPARE);
  g_ready = true;
}

void applyOutputs(const Outputs& o) {
  // CH1 vacuum
  if (g_vacPwm) {
    ledcWrite(PF_PIN_VAC, ledcDutyFor(o.vacDuty));
  } else {
    if (o.vacOn) pinOn(PF_PIN_VAC); else pinOff(PF_PIN_VAC);
  }

  // CH2 pulsator -- always discrete. A pulsator is a bang-bang valve;
  // PWM here would be meaningless and would blur the pulse edges that
  // are the entire point of the product.
  if (o.pulseOn) pinOn(PF_PIN_PULSE); else pinOff(PF_PIN_PULSE);

  // CH3 motor
  if (g_motorPwm) {
    ledcWrite(PF_PIN_MOTOR, ledcDutyFor(o.motorDuty));
  } else {
    if (o.motorOn) pinOn(PF_PIN_MOTOR); else pinOff(PF_PIN_MOTOR);
  }

  g_last = o;
}

void allOff() {
  if (g_motorPwm) ledcWrite(PF_PIN_MOTOR, ledcDutyFor(0));
  if (g_vacPwm)   ledcWrite(PF_PIN_VAC, ledcDutyFor(0));
  if (!g_motorPwm) pinOff(PF_PIN_MOTOR);
  if (!g_vacPwm)   pinOff(PF_PIN_VAC);
  pinOff(PF_PIN_PULSE);
  pinOff(PF_PIN_SPARE);
  g_last = Outputs{ false, false, false, 0, 0 };
}

Outputs lastApplied() { return g_last; }
bool ready() { return g_ready; }

}  // namespace hal
}  // namespace pf
