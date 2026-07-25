// =====================================================================
//  pf_core.h  --  PulseFeed portable core
// ---------------------------------------------------------------------
//  This header and pf_core.cpp contain ZERO Arduino / ESP32 / M5 code.
//  They compile identically on the device and on a host machine, which
//  is what lets the pulsation timing be unit-tested (see tests/).
//
//  Everything safety-critical lives here:
//    * Settings model + validation      (Settings)
//    * Rhythm pattern compiler + clock  (RhythmPattern)
//    * The channel engine               (Engine)
//    * Allocation-free JSON writer      (JsonOut)
//
//  Time base: monotonic microseconds as uint64_t. The host supplies it
//  from std::chrono, the device from esp_timer_get_time(). Using 64-bit
//  micros removes every millis()-rollover class of bug outright.
// =====================================================================
#pragma once

#include <stdint.h>
#include <stddef.h>

namespace pf {

// ------------------------------------------------------------------ //
//  Identity
// ------------------------------------------------------------------ //
static const char* const kVersion   = "2.0.0";
static const char* const kProduct   = "PulseFeed";
static const char* const kModel     = "Pluto 9000";
static const char* const kApiLevel  = "1";

// ------------------------------------------------------------------ //
//  Limits.  Single source of truth: firmware, simulator, web UI and
//  the REST API all clamp against these exact numbers.
// ------------------------------------------------------------------ //
struct Limits {
  static const int kVacMin        = 0;
  static const int kVacMax        = 100;

  static const int kPpmOff        = 0;
  static const int kPpmMin        = 30;    // below this the channel is off
  static const int kPpmMax        = 400;

  // Pulsation ratio: percent of each pulse period spent in the ON phase.
  // Dairy pulsators run 50:50 to 70:30. v43 hardcoded 50. Now tunable.
  static const int kRatioMin      = 30;
  static const int kRatioMax      = 70;
  static const int kRatioDefault  = 50;

  static const int kMotorMin      = 0;
  static const int kMotorMax      = 100;
  static const int kMotorStep     = 5;     // v43 forced 10; 5 is still safe

  static const int kRampMin       = 1;
  static const int kRampMax       = 10;

  static const int kSpeedMin      = 25;    // rhythm speed %
  static const int kSpeedMax      = 400;

  static const int kVacWindowMin  = 200;   // ms, solenoid duty window
  static const int kVacWindowMax  = 2000;

  static const int kSoftStartMin  = 0;     // ms, motor soft-start ramp
  static const int kSoftStartMax  = 5000;

  static const int kBacklightMin  = 1;
  static const int kBacklightMax  = 10;

  // Safety: hard ceiling on a single uninterrupted run, minutes.
  // 0 disables the limit (explicit opt-out, not the default).
  static const int kRunLimitMin   = 0;
  static const int kRunLimitMax   = 480;

  static const int kRhythmSlots   = 8;     // v43 had 5
  static const int kRhythmNameLen = 24;
  static const int kRhythmDataLen = 320;
  static const int kMaxSteps      = 48;    // compiled steps per pattern
};

// Built-in rhythm ids are 1..kBuiltinCount. 0 is "off".
// Custom slots occupy kCustomBase .. kCustomBase+kRhythmSlots-1.
static const int kBuiltinCount = 24;
static const int kCustomBase   = 32;      // gap left so builtins can grow
static const int kRhythmIdMax  = kCustomBase + Limits::kRhythmSlots - 1;

// ------------------------------------------------------------------ //
//  Small helpers
// ------------------------------------------------------------------ //
int   clampi(int v, int lo, int hi);
long  clampl(long v, long lo, long hi);
float clampf(float v, float lo, float hi);
int   snapTo(int v, int step);

// PPM is a gapped range: exactly 0, or kPpmMin..kPpmMax.
int normalizePpm(int v);
// Motor snaps to kMotorStep, with 0 preserved as a true off.
int normalizeMotor(int v);

// ------------------------------------------------------------------ //
//  JsonOut -- bounded, allocation-free JSON writer.
//
//  v43 built JSON with Arduino String concatenation and a hand-rolled
//  escaper that missed control characters, so a WiFi SSID containing a
//  newline or a custom rhythm name containing a quote produced invalid
//  JSON and broke the whole dashboard. This writer escapes properly and
//  can never overflow its buffer -- it sets `overflow()` and truncates.
// ------------------------------------------------------------------ //
class JsonOut {
 public:
  JsonOut(char* buf, size_t cap);

  void beginObj();
  void endObj();
  void beginArr();
  void endArr();

  void key(const char* k);              // raw key, caller opens a value
  void kvNum(const char* k, long v);
  void kvUNum(const char* k, unsigned long v);
  void kvReal(const char* k, float v, int decimals);
  void kvBool(const char* k, bool v);
  void kvStr(const char* k, const char* v);
  void kvNull(const char* k);

  void arrNum(long v);
  void arrStr(const char* v);

  size_t      length()   const { return len_; }
  bool        overflow() const { return ovf_; }
  const char* c_str()    const { return buf_; }

 private:
  void raw(char c);
  void raw(const char* s);
  void comma();
  void escaped(const char* s);

  char*  buf_;
  size_t cap_;
  size_t len_;
  bool   ovf_;
  bool   need_comma_;
};

// ------------------------------------------------------------------ //
//  RhythmPattern -- compiled, allocation-free pulse pattern.
//
//  v43 re-parsed the custom-rhythm String on EVERY engine tick (~30/s),
//  allocating substrings each time. On an ESP32 that fragments the heap
//  until the web server can no longer allocate a response buffer. Here a
//  pattern is compiled once, on change, into a fixed step array.
// ------------------------------------------------------------------ //
struct RhythmStep {
  uint16_t onMs;
  uint16_t offMs;
};

class RhythmPattern {
 public:
  RhythmPattern();

  void clear();

  // ".  . - . ."  dot = 1 unit on / 1 off, dash = 3 on / 1 off,
  // space = 1 extra unit of rest. unitMs scales the whole pattern.
  bool compileDots(const char* pattern, uint16_t unitMs);

  // "120,300;80,150;"  -> explicit onMs,offMs pairs (tap-recorded).
  bool compileCsv(const char* csv);

  bool     valid()   const { return count_ > 0 && totalMs_ > 0; }
  uint8_t  count()   const { return count_; }
  uint32_t totalMs() const { return totalMs_; }
  const RhythmStep& step(int i) const { return steps_[i]; }

  // Is the channel energised at `elapsedUs` into the loop, played at
  // speedPct? 64-bit math throughout: v43 overflowed its 32-bit scaled
  // position after ~4 hours at 300% and the pattern visibly jumped.
  bool pulseAt(uint64_t elapsedUs, int speedPct) const;

  // Index of the step being played, or -1. Used by the UI beat display.
  int stepAt(uint64_t elapsedUs, int speedPct) const;

 private:
  bool push(uint16_t onMs, uint16_t offMs);
  void finalize();

  RhythmStep steps_[Limits::kMaxSteps];
  uint32_t   cum_[Limits::kMaxSteps];   // cumulative end-of-step ms
  uint8_t    count_;
  uint32_t   totalMs_;
};

// Built-in pattern table (pf_rhythms.cpp).
const char* builtinName(int id);
const char* builtinDots(int id);
bool        isBuiltin(int id);
bool        isCustom(int id);
int         customSlot(int id);        // -1 when not a custom id

// ------------------------------------------------------------------ //
//  Settings -- everything the operator can change and we persist.
//  validate() is idempotent and is applied on load, on every API write
//  and before every save, so out-of-range values can never reach the
//  engine regardless of how they arrived.
// ------------------------------------------------------------------ //
struct Settings {
  // Channels
  int  vacTarget;
  int  vacRamp;
  int  vacWindowMs;
  bool vacProportional;   // true: hardware PWM valve, false: duty window
  int  ppm;
  int  ratio;
  int  motor;
  int  motorSoftMs;

  // Rhythm
  int  rhythmId;
  int  rhythmSpeed;
  int  fav[3];

  // Safety
  int  runLimitMin;
  bool autoStopOnDisconnect;

  // UI
  bool darkTheme;
  int  backlight;

  // Web / net
  bool webEnabled;
  int  webPort;
  bool authRequired;
  int  tzOffsetMin;
  bool dstEnabled;

  void defaults();
  void validate();
};

// ------------------------------------------------------------------ //
//  Engine -- the safety-critical core.
//
//  Pure function of (settings, command history, time). No I/O, no
//  allocation, no blocking. The device runs tick() from a dedicated
//  high-priority FreeRTOS task; the tests run it from a loop with a
//  synthetic clock. Both get bit-identical behaviour.
// ------------------------------------------------------------------ //
struct Outputs {
  bool    vacOn;        // discrete solenoid state
  bool    pulseOn;
  bool    motorOn;
  uint8_t vacDuty;      // 0..255 for a proportional valve
  uint8_t motorDuty;    // 0..255 for the LEDC motor channel
};

enum StopReason {
  STOP_NONE = 0,
  STOP_OPERATOR,
  STOP_ESTOP,
  STOP_RUN_LIMIT,
  STOP_WATCHDOG,
  STOP_FAULT,
};

class Engine {
 public:
  Engine();

  // Wire in the pattern storage owned by the application.
  void attachPatterns(RhythmPattern* builtins, RhythmPattern* customs);

  void reset(uint64_t nowUs);
  void applySettings(const Settings& s, uint64_t nowUs);

  void start(uint64_t nowUs);
  void stop(uint64_t nowUs, StopReason why);
  void triggerEstop(uint64_t nowUs);
  bool clearEstop(uint64_t nowUs);       // false while still latched-unsafe

  // Advance the model and produce the desired output state.
  // Must be called at >= 500 Hz for accurate pulsation at 400 PPM.
  Outputs tick(uint64_t nowUs);

  // --- observation -------------------------------------------------
  bool       running()    const { return running_; }
  bool       estop()      const { return estop_; }
  StopReason lastStop()   const { return lastStop_; }
  float      vacActual()  const { return vacActual_; }
  uint32_t   runSeconds(uint64_t nowUs) const;
  Outputs    outputs()    const { return out_; }
  // 0..1 position inside the current pulse period, for the UI beat ring.
  float      pulsePhase() const { return phase_; }
  uint32_t   pulseCount() const { return pulseCount_; }

  // Deadman: the supervisor calls feed(); if tick() ever observes that
  // it has not been fed within kDeadmanUs it stops the machine. This is
  // what protects against a hung UI or network stack leaving solenoids
  // energised -- v43 had no such protection at all.
  void feed(uint64_t nowUs) { fedUs_ = nowUs; }
  // 2.5 s: long enough to ride out a slow HTTP client or an SD flush,
  // short enough that a genuinely wedged supervisor cannot leave the
  // channels energised for an operationally meaningful time.
  static const uint64_t kDeadmanUs = 2500000ULL;

 private:
  void  recomputeDerived();
  Outputs allOff() const;

  Settings       s_;
  RhythmPattern* builtins_;
  RhythmPattern* customs_;

  bool       running_;
  bool       estop_;
  StopReason lastStop_;

  uint64_t   startUs_;
  uint64_t   lastTickUs_;
  uint64_t   pulseEpochUs_;   // phase reference for PPM + rhythm
  uint64_t   rhythmEpochUs_;
  uint64_t   fedUs_;
  uint64_t   motorRampUs_;

  float      vacActual_;
  float      phase_;
  uint32_t   pulseCount_;
  bool       prevPulse_;

  uint32_t   periodUs_;       // cached from ppm
  uint32_t   onUs_;           // cached from ppm + ratio

  Outputs    out_;
};

}  // namespace pf
