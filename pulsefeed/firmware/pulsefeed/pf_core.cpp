// =====================================================================
//  pf_core.cpp  --  PulseFeed portable core implementation
//  No Arduino headers. Compiles for the device and for the host tests.
// =====================================================================
#include "pf_core.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

namespace pf {

// ------------------------------------------------------------------ //
//  Helpers
// ------------------------------------------------------------------ //
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
long clampl(long v, long lo, long hi) { return v < lo ? lo : (v > hi ? hi : v); }
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int snapTo(int v, int step) {
  if (step <= 1) return v;
  int half = step / 2;
  return ((v + half) / step) * step;
}

int normalizePpm(int v) {
  if (v <= 0) return Limits::kPpmOff;
  if (v < Limits::kPpmMin) return Limits::kPpmMin;
  return clampi(v, Limits::kPpmMin, Limits::kPpmMax);
}

int normalizeMotor(int v) {
  v = clampi(v, Limits::kMotorMin, Limits::kMotorMax);
  if (v <= 0) return 0;
  int snapped = snapTo(v, Limits::kMotorStep);
  return clampi(snapped, Limits::kMotorStep, Limits::kMotorMax);
}

// ------------------------------------------------------------------ //
//  JsonOut
// ------------------------------------------------------------------ //
JsonOut::JsonOut(char* buf, size_t cap)
    : buf_(buf), cap_(cap), len_(0), ovf_(false), need_comma_(false) {
  if (cap_ > 0) buf_[0] = '\0';
}

void JsonOut::raw(char c) {
  if (len_ + 1 >= cap_) { ovf_ = true; return; }
  buf_[len_++] = c;
  buf_[len_] = '\0';
}

void JsonOut::raw(const char* s) {
  while (*s) raw(*s++);
}

void JsonOut::comma() {
  if (need_comma_) raw(',');
  need_comma_ = true;
}

// Emit a JSON string body with correct escaping.
//
// v43's safeJsonString() only escaped backslash and quote. A control
// character -- which absolutely does appear in scanned WiFi SSIDs --
// produced invalid JSON and the dashboard's poll() silently died in its
// catch block, so the whole UI froze with no error. We escape control
// characters and validate UTF-8, substituting U+FFFD for invalid bytes
// so the output is always parseable.
void JsonOut::escaped(const char* s) {
  const unsigned char* p = (const unsigned char*)s;
  while (*p) {
    unsigned char c = *p;
    if (c == '"')        { raw("\\\""); p++; }
    else if (c == '\\')  { raw("\\\\"); p++; }
    else if (c == '\n')  { raw("\\n");  p++; }
    else if (c == '\r')  { raw("\\r");  p++; }
    else if (c == '\t')  { raw("\\t");  p++; }
    else if (c == '\b')  { raw("\\b");  p++; }
    else if (c == '\f')  { raw("\\f");  p++; }
    else if (c < 0x20)   { char t[8]; snprintf(t, sizeof(t), "\\u%04x", c); raw(t); p++; }
    else if (c < 0x80)   { raw((char)c); p++; }
    else {
      // Validate the UTF-8 sequence before passing it through.
      int need = 0;
      if      ((c & 0xE0) == 0xC0) need = 1;
      else if ((c & 0xF0) == 0xE0) need = 2;
      else if ((c & 0xF8) == 0xF0) need = 3;
      bool ok = need > 0;
      for (int i = 1; ok && i <= need; i++) {
        if ((p[i] & 0xC0) != 0x80) ok = false;
      }
      if (ok) {
        for (int i = 0; i <= need; i++) raw((char)p[i]);
        p += need + 1;
      } else {
        raw("\\ufffd");
        p++;
      }
    }
  }
}

void JsonOut::beginObj() { comma(); raw('{'); need_comma_ = false; }
void JsonOut::endObj()   { raw('}'); need_comma_ = true; }
void JsonOut::beginArr() { comma(); raw('['); need_comma_ = false; }
void JsonOut::endArr()   { raw(']'); need_comma_ = true; }

void JsonOut::key(const char* k) {
  comma();
  raw('"'); escaped(k); raw("\":");
  need_comma_ = false;
}

void JsonOut::kvNum(const char* k, long v) {
  key(k);
  char t[24]; snprintf(t, sizeof(t), "%ld", v); raw(t);
  need_comma_ = true;
}

void JsonOut::kvUNum(const char* k, unsigned long v) {
  key(k);
  char t[24]; snprintf(t, sizeof(t), "%lu", v); raw(t);
  need_comma_ = true;
}

void JsonOut::kvReal(const char* k, float v, int decimals) {
  key(k);
  if (!(v == v) || v > 1e30f || v < -1e30f) { raw('0'); need_comma_ = true; return; }
  char fmt[8]; snprintf(fmt, sizeof(fmt), "%%.%df", clampi(decimals, 0, 6));
  char t[32]; snprintf(t, sizeof(t), fmt, (double)v); raw(t);
  need_comma_ = true;
}

void JsonOut::kvBool(const char* k, bool v) {
  key(k); raw(v ? "true" : "false"); need_comma_ = true;
}

void JsonOut::kvStr(const char* k, const char* v) {
  key(k); raw('"'); if (v) escaped(v); raw('"'); need_comma_ = true;
}

void JsonOut::kvNull(const char* k) { key(k); raw("null"); need_comma_ = true; }

void JsonOut::arrNum(long v) {
  comma();
  char t[24]; snprintf(t, sizeof(t), "%ld", v); raw(t);
}

void JsonOut::arrStr(const char* v) {
  comma(); raw('"'); if (v) escaped(v); raw('"');
}

// ------------------------------------------------------------------ //
//  RhythmPattern
// ------------------------------------------------------------------ //
RhythmPattern::RhythmPattern() { clear(); }

void RhythmPattern::clear() {
  count_ = 0;
  totalMs_ = 0;
  memset(steps_, 0, sizeof(steps_));
  memset(cum_, 0, sizeof(cum_));
}

bool RhythmPattern::push(uint16_t onMs, uint16_t offMs) {
  if (count_ >= Limits::kMaxSteps) return false;
  steps_[count_].onMs = onMs;
  steps_[count_].offMs = offMs;
  count_++;
  return true;
}

void RhythmPattern::finalize() {
  uint32_t acc = 0;
  for (int i = 0; i < count_; i++) {
    acc += (uint32_t)steps_[i].onMs + (uint32_t)steps_[i].offMs;
    cum_[i] = acc;
  }
  totalMs_ = acc;
}

// Dot/dash grammar, preserved from v43 so existing patterns still read
// the same way, but compiled to explicit milliseconds:
//   '.'  1 unit ON, 1 unit OFF
//   '-'  3 units ON, 1 unit OFF
//   ' '  1 extra unit of rest, merged into the previous step
bool RhythmPattern::compileDots(const char* pattern, uint16_t unitMs) {
  clear();
  if (!pattern || unitMs == 0) return false;

  for (const char* p = pattern; *p; ++p) {
    if (*p == '.') {
      if (!push(unitMs, unitMs)) break;
    } else if (*p == '-') {
      if (!push((uint16_t)(unitMs * 3), unitMs)) break;
    } else if (*p == ' ') {
      if (count_ > 0) {
        uint32_t widened = (uint32_t)steps_[count_ - 1].offMs + unitMs;
        steps_[count_ - 1].offMs = (uint16_t)(widened > 65535u ? 65535u : widened);
      } else {
        if (!push(0, unitMs)) break;
      }
    }
    // any other character is ignored, which makes the patterns
    // forgiving of decorative separators
  }
  finalize();
  return valid();
}

// "onMs,offMs;onMs,offMs;..."  as produced by the tap recorder.
bool RhythmPattern::compileCsv(const char* csv) {
  clear();
  if (!csv) return false;

  const char* p = csv;
  while (*p && count_ < Limits::kMaxSteps) {
    while (*p == ';' || *p == ' ') p++;
    if (!*p) break;

    long on = 0, off = 0;
    bool haveDigits = false;
    while (*p >= '0' && *p <= '9') { on = on * 10 + (*p - '0'); p++; haveDigits = true; if (on > 100000) on = 100000; }
    if (!haveDigits) { while (*p && *p != ';') p++; continue; }
    if (*p == ',') {
      p++;
      while (*p >= '0' && *p <= '9') { off = off * 10 + (*p - '0'); p++; if (off > 100000) off = 100000; }
    }
    while (*p && *p != ';') p++;

    on  = clampl(on, 20, 5000);
    off = clampl(off, 0, 10000);
    push((uint16_t)on, (uint16_t)off);
  }
  finalize();
  return valid();
}

size_t RhythmPattern::toCsv(char* out, size_t cap) const {
  size_t len = 0;
  out[0] = '\0';
  for (int i = 0; i < count_ && len + 1 < cap; i++) {
    int n = snprintf(out + len, cap - len, "%u,%u;", steps_[i].onMs, steps_[i].offMs);
    if (n < 0 || (size_t)n >= cap - len) break;   // would truncate a pair mid-number; stop clean
    len += (size_t)n;
  }
  out[len] = '\0';
  return len;
}

// Scaled clock. speedPct > 100 plays faster (matches v43's direction).
// All arithmetic is 64-bit: v43 computed `elapsed * speedPct` in 32 bits
// and wrapped after roughly four hours at 300%, which made a running
// pattern jump to a random position.
bool RhythmPattern::pulseAt(uint64_t elapsedUs, int speedPct) const {
  int idx = stepAt(elapsedUs, speedPct);
  if (idx < 0) return false;

  uint64_t scaledMs = (elapsedUs / 1000ULL) * (uint64_t)clampi(speedPct, Limits::kSpeedMin, Limits::kSpeedMax) / 100ULL;
  uint32_t pos = (uint32_t)(scaledMs % (uint64_t)totalMs_);
  uint32_t stepStart = (idx == 0) ? 0u : cum_[idx - 1];
  return (pos - stepStart) < steps_[idx].onMs;
}

int RhythmPattern::stepAt(uint64_t elapsedUs, int speedPct) const {
  if (!valid()) return -1;
  uint64_t scaledMs = (elapsedUs / 1000ULL) * (uint64_t)clampi(speedPct, Limits::kSpeedMin, Limits::kSpeedMax) / 100ULL;
  uint32_t pos = (uint32_t)(scaledMs % (uint64_t)totalMs_);
  for (int i = 0; i < count_; i++) {
    if (pos < cum_[i]) return i;
  }
  return count_ - 1;
}

// ------------------------------------------------------------------ //
//  Settings
// ------------------------------------------------------------------ //
void Settings::defaults() {
  vacTarget       = 0;
  vacRamp         = 5;
  vacWindowMs     = 1000;
  vacProportional = false;
  ppm             = 0;
  ratio           = Limits::kRatioDefault;
  motor           = 0;
  motorSoftMs     = 400;

  rhythmId        = 0;
  rhythmSpeed     = 100;
  fav[0] = 3; fav[1] = 7; fav[2] = 13;

  runLimitMin          = 60;     // a real default limit, not "off"
  autoStopOnDisconnect = false;

  darkTheme = true;
  backlight = 7;

  webEnabled   = true;
  webPort      = 80;
  authRequired = true;           // secure by default
  tzOffsetMin  = -300;           // US Eastern standard
  dstEnabled   = true;
}

void Settings::validate() {
  vacTarget   = clampi(vacTarget, Limits::kVacMin, Limits::kVacMax);
  vacRamp     = clampi(vacRamp, Limits::kRampMin, Limits::kRampMax);
  vacWindowMs = clampi(vacWindowMs, Limits::kVacWindowMin, Limits::kVacWindowMax);
  ppm         = normalizePpm(ppm);
  ratio       = clampi(ratio, Limits::kRatioMin, Limits::kRatioMax);
  motor       = normalizeMotor(motor);
  motorSoftMs = clampi(motorSoftMs, Limits::kSoftStartMin, Limits::kSoftStartMax);

  if (rhythmId != 0 && !isBuiltin(rhythmId) && !isCustom(rhythmId)) rhythmId = 0;
  rhythmSpeed = clampi(rhythmSpeed, Limits::kSpeedMin, Limits::kSpeedMax);
  for (int i = 0; i < 3; i++) {
    if (fav[i] != 0 && !isBuiltin(fav[i]) && !isCustom(fav[i])) fav[i] = 0;
  }

  runLimitMin = clampi(runLimitMin, Limits::kRunLimitMin, Limits::kRunLimitMax);
  backlight   = clampi(backlight, Limits::kBacklightMin, Limits::kBacklightMax);
  webPort     = clampi(webPort, 1, 65535);
  tzOffsetMin = clampi(tzOffsetMin, -720, 840);
}

// ------------------------------------------------------------------ //
//  Engine
// ------------------------------------------------------------------ //
Engine::Engine()
    : builtins_(0), customs_(0) {
  s_.defaults();
  reset(0);
}

void Engine::attachPatterns(RhythmPattern* builtins, RhythmPattern* customs) {
  builtins_ = builtins;
  customs_  = customs;
}

Outputs Engine::allOff() const {
  Outputs o;
  o.vacOn = false; o.pulseOn = false; o.motorOn = false;
  o.vacDuty = 0; o.motorDuty = 0;
  return o;
}

void Engine::reset(uint64_t nowUs) {
  running_       = false;
  estop_         = false;
  lastStop_      = STOP_NONE;
  startUs_       = nowUs;
  lastTickUs_    = nowUs;
  pulseEpochUs_  = nowUs;
  rhythmEpochUs_ = nowUs;
  fedUs_         = nowUs;
  motorRampUs_   = nowUs;
  vacActual_     = 0.0f;
  phase_         = 0.0f;
  pulseCount_    = 0;
  prevPulse_     = false;
  out_           = allOff();
  recomputeDerived();
}

void Engine::recomputeDerived() {
  if (s_.ppm >= Limits::kPpmMin) {
    periodUs_ = (uint32_t)(60000000UL / (uint32_t)s_.ppm);
    onUs_     = (uint32_t)((uint64_t)periodUs_ * (uint32_t)s_.ratio / 100ULL);
    if (onUs_ < 1000) onUs_ = 1000;                 // never emit a sliver
    if (onUs_ > periodUs_ - 1000) onUs_ = periodUs_ > 2000 ? periodUs_ - 1000 : periodUs_ / 2;
  } else {
    periodUs_ = 0;
    onUs_     = 0;
  }
}

void Engine::applySettings(const Settings& s, uint64_t nowUs) {
  int prevRhythm = s_.rhythmId;
  s_ = s;
  s_.validate();
  recomputeDerived();
  // Restart the pattern clock only when the pattern itself changed --
  // changing speed mid-pattern should stretch it, not jump it.
  if (s_.rhythmId != prevRhythm) {
    rhythmEpochUs_ = nowUs;
  }
}

void Engine::start(uint64_t nowUs) {
  if (estop_) return;                 // latched: refuse silently, caller reports
  if (running_) return;
  running_       = true;
  lastStop_      = STOP_NONE;
  startUs_       = nowUs;
  pulseEpochUs_  = nowUs;
  rhythmEpochUs_ = nowUs;
  motorRampUs_   = nowUs;
  fedUs_         = nowUs;
  prevPulse_     = false;
}

void Engine::stop(uint64_t nowUs, StopReason why) {
  (void)nowUs;
  if (running_) lastStop_ = why;
  running_ = false;
  out_ = allOff();
  prevPulse_ = false;
}

void Engine::triggerEstop(uint64_t nowUs) {
  estop_ = true;
  stop(nowUs, STOP_ESTOP);
  vacActual_ = 0.0f;        // model the dump valve: pressure goes now
}

bool Engine::clearEstop(uint64_t nowUs) {
  (void)nowUs;
  if (!estop_) return true;
  estop_ = false;
  lastStop_ = STOP_NONE;
  return true;
}

uint32_t Engine::runSeconds(uint64_t nowUs) const {
  if (!running_) return 0;
  if (nowUs <= startUs_) return 0;
  return (uint32_t)((nowUs - startUs_) / 1000000ULL);
}

Outputs Engine::tick(uint64_t nowUs) {
  // ---- monotonic guard -------------------------------------------
  if (nowUs < lastTickUs_) lastTickUs_ = nowUs;      // clock went backwards
  uint32_t dtUs = (uint32_t)(nowUs - lastTickUs_);
  if (dtUs > 1000000u) dtUs = 1000000u;              // long stall: cap the step
  lastTickUs_ = nowUs;

  // ---- deadman ----------------------------------------------------
  // If the supervisor stops feeding us, the machine stops. v43 had no
  // equivalent: a hung network stack left the solenoids energised.
  if (running_ && (nowUs - fedUs_) > kDeadmanUs) {
    stop(nowUs, STOP_WATCHDOG);
  }

  // ---- run limit --------------------------------------------------
  if (running_ && s_.runLimitMin > 0) {
    if (runSeconds(nowUs) >= (uint32_t)s_.runLimitMin * 60u) {
      stop(nowUs, STOP_RUN_LIMIT);
    }
  }

  // ---- vacuum model ------------------------------------------------
  // First-order lag with a time constant derived from vacRamp. Framed
  // in real time, so the ramp is identical whether tick() runs at 200 Hz
  // or 2 kHz. v43 applied a fixed per-poll fraction, which meant the
  // physical ramp rate silently changed with UI load.
  float commanded = (running_ && !estop_) ? (float)s_.vacTarget : 0.0f;
  float tauMs = 2200.0f / (float)clampi(s_.vacRamp, Limits::kRampMin, Limits::kRampMax);
  float dtMs  = (float)dtUs / 1000.0f;
  float alpha = dtMs / (tauMs + dtMs);
  vacActual_ += (commanded - vacActual_) * alpha;
  if (vacActual_ < 0.05f && commanded == 0.0f) vacActual_ = 0.0f;
  if (vacActual_ > 100.0f) vacActual_ = 100.0f;
  if (vacActual_ < 0.0f) vacActual_ = 0.0f;

  if (estop_ || !running_) {
    out_ = allOff();
    phase_ = 0.0f;
    return out_;
  }

  Outputs o = allOff();

  // ---- CH1 vacuum --------------------------------------------------
  int vacCmd = (int)(vacActual_ + 0.5f);
  if (s_.vacProportional) {
    o.vacDuty = (uint8_t)clampi(vacCmd * 255 / 100, 0, 255);
    o.vacOn   = vacCmd > 0;
  } else {
    if (vacCmd <= 0) {
      o.vacOn = false;
    } else if (vacCmd >= 100) {
      o.vacOn = true;
    } else {
      uint32_t win = (uint32_t)s_.vacWindowMs * 1000u;
      uint32_t ph  = (uint32_t)(nowUs % (uint64_t)win);
      o.vacOn = ph < (uint32_t)((uint64_t)win * (uint32_t)vacCmd / 100ULL);
    }
    o.vacDuty = o.vacOn ? 255 : 0;
  }

  // ---- CH2 pulsation / rhythm ---------------------------------------
  bool pulse = false;
  const RhythmPattern* pat = 0;
  if (s_.rhythmId != 0) {
    if (isBuiltin(s_.rhythmId) && builtins_) {
      pat = &builtins_[s_.rhythmId - 1];
    } else if (isCustom(s_.rhythmId) && customs_) {
      pat = &customs_[customSlot(s_.rhythmId)];
    }
  }

  if (pat && pat->valid()) {
    uint64_t el = nowUs - rhythmEpochUs_;
    pulse = pat->pulseAt(el, s_.rhythmSpeed);
    uint64_t scaledMs = (el / 1000ULL) * (uint64_t)s_.rhythmSpeed / 100ULL;
    phase_ = pat->totalMs() ? (float)(scaledMs % pat->totalMs()) / (float)pat->totalMs() : 0.0f;
  } else if (periodUs_ > 0) {
    uint32_t ph = (uint32_t)((nowUs - pulseEpochUs_) % (uint64_t)periodUs_);
    pulse  = ph < onUs_;
    phase_ = (float)ph / (float)periodUs_;
  } else {
    phase_ = 0.0f;
  }
  o.pulseOn = pulse;
  if (pulse && !prevPulse_) pulseCount_++;
  prevPulse_ = pulse;

  // ---- CH3 motor -----------------------------------------------------
  // Real speed control via a duty value the HAL feeds to hardware PWM,
  // plus a soft start. v43 bit-banged a 2 Hz square wave onto a 12 V
  // motor, which is not speed control -- it is audible chugging and it
  // is hard on the brushes.
  int motorCmd = s_.motor;
  if (motorCmd > 0) {
    uint32_t soft = (uint32_t)s_.motorSoftMs * 1000u;
    uint64_t since = nowUs - motorRampUs_;
    float k = 1.0f;
    if (soft > 0 && since < soft) k = (float)since / (float)soft;
    int ramped = (int)((float)motorCmd * k + 0.5f);
    o.motorDuty = (uint8_t)clampi(ramped * 255 / 100, 0, 255);
    o.motorOn   = ramped > 0;
  }

  out_ = o;
  return o;
}

}  // namespace pf
