// =====================================================================
//  pf_app.cpp -- application state, commands, persistence policy
// =====================================================================
#include "pf_app.h"
#include "pf_hal.h"

#include <Arduino.h>
#include <string.h>

namespace pf {

App app;

namespace {

// Settings are flushed after the operator stops fiddling, not on every
// change. v43 used 900 ms; 1500 ms plus change-only NVS writes reduces
// flash traffic by roughly two orders of magnitude during a slider drag.
const uint32_t kSaveIdleMs = 1500;

char g_rhythmNameBuf[Limits::kRhythmNameLen];

}  // namespace

// ------------------------------------------------------------------ //
void setStatus(const char* msg) {
  if (!msg) return;
  strncpy(app.statusMsg, msg, sizeof(app.statusMsg) - 1);
  app.statusMsg[sizeof(app.statusMsg) - 1] = '\0';
}

void buildSnapshot(log::Snapshot& s) {
  Outputs o = app.engine.outputs();
  s.vacTarget = (int16_t)app.settings.vacTarget;
  s.vacActual = (int16_t)app.engine.vacActual();
  s.ppm       = (int16_t)app.settings.ppm;
  s.ratio     = (int16_t)app.settings.ratio;
  s.motor     = (int16_t)app.settings.motor;
  s.rhythmId  = (int16_t)app.settings.rhythmId;
  s.heap      = ESP.getFreeHeap();
  s.flags = 0;
  if (app.engine.running()) s.flags |= log::F_RUNNING;
  if (o.vacOn)              s.flags |= log::F_VAC;
  if (o.pulseOn)            s.flags |= log::F_PULSE;
  if (o.motorOn)            s.flags |= log::F_MOTOR;
  if (app.engine.estop())   s.flags |= log::F_ESTOP;
}

static void logEvent(const char* what, const char* src) {
  char buf[40];
  snprintf(buf, sizeof(buf), "%s(%s)", what, src ? src : "?");
  log::Snapshot s; buildSnapshot(s);
  log::event(buf, s);
}

// ------------------------------------------------------------------ //
const char* rhythmLabel(int id) {
  if (id == 0) return "Off";
  if (isBuiltin(id)) return builtinName(id);
  int slot = customSlot(id);
  if (slot >= 0) {
    // Copy out: returning a pointer straight into the live slot -- which
    // is what v43 did -- lets a rename from the web free the string the
    // display is halfway through printing.
    strncpy(g_rhythmNameBuf, app.custom[slot].name, sizeof(g_rhythmNameBuf) - 1);
    g_rhythmNameBuf[sizeof(g_rhythmNameBuf) - 1] = '\0';
    return g_rhythmNameBuf;
  }
  return "Off";
}

const char* rhythmNotation(int id) {
  if (isBuiltin(id)) return builtinDots(id);
  int slot = customSlot(id);
  if (slot >= 0) return app.custom[slot].data[0] ? app.custom[slot].data : "(empty)";
  return "";
}

bool recompileCustom(int slot) {
  if (slot < 0 || slot >= Limits::kRhythmSlots) return false;

  // If the engine is currently playing this slot, take it off the air
  // first. compileCsv() clears the step array before refilling it, and
  // the 1 kHz engine task must never observe that half-built state.
  // Nothing else needs locking: every Settings field is independently
  // validated before it is stored, so any interleaving of an old and a
  // new field is still a configuration the engine can safely run.
  bool wasActive = (app.settings.rhythmId == kCustomBase + slot);
  if (wasActive) {
    app.settings.rhythmId = 0;
    app.engine.applySettings(app.settings, hal::nowUs());
  }

  bool ok = app.customs[slot].compileCsv(app.custom[slot].data);

  if (wasActive && ok) {
    app.settings.rhythmId = kCustomBase + slot;
    app.engine.applySettings(app.settings, hal::nowUs());
  }
  return ok;
}

void applyEngineSettings() {
  app.engine.applySettings(app.settings, hal::nowUs());
}

// ------------------------------------------------------------------ //
void appBegin() {
  memset(&app.recordBuf, 0, sizeof(app.recordBuf));
  app.recording = false;
  app.recordSlot = 0;
  app.sdOk = false;
  app.settingsDirty = false;
  app.bootUs = hal::nowUs();
  app.reqWebRestart = false;
  app.reqWifiScan = false;
  app.reqReboot = false;
  strcpy(app.statusMsg, "Local control ready");

  store::begin();
  store::loadSettings(app.settings);
  store::loadCustom(app.custom);

  for (int i = 1; i <= kBuiltinCount; i++) {
    app.builtins[i - 1].compileDots(builtinDots(i), 250);
  }
  for (int i = 0; i < Limits::kRhythmSlots; i++) recompileCustom(i);

  app.engine.attachPatterns(app.builtins, app.customs);
  app.engine.reset(hal::nowUs());
  applyEngineSettings();

  // A saved rhythm must not auto-play on power-up. v43 restored
  // rhythmMode from NVS but never started the program, so the device
  // booted claiming a rhythm was selected while nothing was running --
  // and the first START then began mid-pattern. Booting to a clean
  // stopped state with the rhythm remembered is the honest behaviour.
}

void appService() {
  if (app.settingsDirty && (millis() - app.settingsDirtyAtMs) >= kSaveIdleMs) {
    saveSettingsNow("idle");
  }
}

void markSettingsDirty() {
  app.settingsDirty = true;
  app.settingsDirtyAtMs = millis();
}

void saveSettingsNow(const char* src) {
  int n = store::saveSettings(app.settings);
  n += store::saveCustom(app.custom);
  app.settingsDirty = false;
  app.nvsWrites = store::writeCount();
  if (n > 0) logEvent("settings-saved", src);
}

// ------------------------------------------------------------------ //
//  Machine commands
// ------------------------------------------------------------------ //
bool cmdStart(const char* src) {
  if (app.engine.estop()) {
    setStatus("E-STOP latched - release before starting");
    logEvent("start-blocked-estop", src);
    return false;
  }
  app.engine.feed(hal::nowUs());
  app.engine.start(hal::nowUs());
  setStatus("Running");
  logEvent("start", src);
  return app.engine.running();
}

void cmdStop(const char* src, StopReason why) {
  bool was = app.engine.running();
  app.engine.stop(hal::nowUs(), why);
  hal::allOff();
  if (was) {
    setStatus("Stopped");
    logEvent("stop", src);
  }
}

void cmdEstop(const char* src) {
  // Cut the hardware FIRST, then update the model, then log.
  //
  // v43's STOP button only navigated to a confirmation screen; the
  // channels stayed energised until the operator tapped a second time.
  // A button labelled E-STOP that does not stop anything is worse than
  // no button, so it now de-energises on the press and the confirmation
  // screen is about releasing pressure, not about whether to stop.
  hal::allOff();
  app.engine.triggerEstop(hal::nowUs());
  hal::allOff();
  setStatus("E-STOP - outputs de-energised");
  logEvent("estop", src);
}

void cmdClearEstop(const char* src) {
  app.engine.clearEstop(hal::nowUs());
  setStatus("E-STOP cleared");
  logEvent("estop-clear", src);
}

// ------------------------------------------------------------------ //
//  Setters. Each validates, applies to the engine, marks dirty and logs
//  only when the value actually moved.
// ------------------------------------------------------------------ //
#define PF_SETTER(fn, field, transform, tag)                       \
  void fn(int value, const char* src) {                            \
    int nv = transform;                                            \
    if (app.settings.field == nv) return;                          \
    app.settings.field = nv;                                       \
    app.settings.validate();                                       \
    applyEngineSettings();                                         \
    markSettingsDirty();                                           \
    logEvent(tag, src);                                            \
  }

PF_SETTER(cmdSetVac,   vacTarget,   clampi(value, Limits::kVacMin, Limits::kVacMax),        "set-vac")
PF_SETTER(cmdSetPpm,   ppm,         normalizePpm(value),                                    "set-ppm")
PF_SETTER(cmdSetRatio, ratio,       clampi(value, Limits::kRatioMin, Limits::kRatioMax),    "set-ratio")
PF_SETTER(cmdSetMotor, motor,       normalizeMotor(value),                                  "set-motor")
PF_SETTER(cmdSetRamp,  vacRamp,     clampi(value, Limits::kRampMin, Limits::kRampMax),      "set-ramp")
PF_SETTER(cmdSetRhythmSpeed, rhythmSpeed, clampi(value, Limits::kSpeedMin, Limits::kSpeedMax), "set-speed")
#undef PF_SETTER

void cmdSetBacklight(int step, const char* src) {
  int nv = clampi(step, Limits::kBacklightMin, Limits::kBacklightMax);
  if (app.settings.backlight == nv) return;
  app.settings.backlight = nv;
  markSettingsDirty();
  logEvent("set-backlight", src);
}

void cmdSetTheme(bool dark, const char* src) {
  if (app.settings.darkTheme == dark) return;
  app.settings.darkTheme = dark;
  markSettingsDirty();
  logEvent("set-theme", src);
}

void cmdSetRhythm(int id, const char* src) {
  if (id != 0 && !isBuiltin(id) && !isCustom(id)) id = 0;

  // Selecting an empty custom slot is a no-op with an explanation
  // rather than a silent switch to a pattern that produces nothing.
  int slot = customSlot(id);
  if (slot >= 0 && !app.customs[slot].valid()) {
    setStatus("That custom slot is empty");
    logEvent("rhythm-empty", src);
    return;
  }

  if (app.settings.rhythmId == id) return;
  app.settings.rhythmId = id;
  applyEngineSettings();
  markSettingsDirty();
  logEvent(id == 0 ? "rhythm-off" : "rhythm-select", src);
}

// --- nudges ---------------------------------------------------------
void cmdNudgeVac(int d, const char* src)   { cmdSetVac(app.settings.vacTarget + d, src); }
void cmdNudgeMotor(int steps, const char* src) { cmdSetMotor(app.settings.motor + steps * Limits::kMotorStep, src); }
void cmdNudgeRatio(int d, const char* src) { cmdSetRatio(app.settings.ratio + d, src); }
void cmdNudgeBacklight(int d, const char* src) { cmdSetBacklight(app.settings.backlight + d, src); }

void cmdNudgeRhythmSpeed(int d, const char* src) {
  cmdSetRhythmSpeed(app.settings.rhythmSpeed + d, src);
}

void cmdNudgePpm(int d, const char* src) {
  // The 0 -> 30 gap needs explicit handling so a single tap crosses it
  // instead of appearing to do nothing.
  int cur = app.settings.ppm;
  int nv;
  if (d > 0 && cur == 0)                     nv = Limits::kPpmMin;
  else if (d < 0 && cur <= Limits::kPpmMin)  nv = 0;
  else                                        nv = cur + d;
  cmdSetPpm(nv, src);
}

// ------------------------------------------------------------------ //
//  Tap recorder
// ------------------------------------------------------------------ //
bool cmdRecordStart(int slot, const char* src) {
  if (slot < 0 || slot >= Limits::kRhythmSlots) return false;
  app.recording = true;
  app.recordSlot = slot;
  app.recordBuf[0] = '\0';
  app.recordDownMs = 0;
  app.recordLastEndMs = millis();
  setStatus("Recording - tap the pulse button");
  logEvent("rec-start", src);
  return true;
}

void cmdRecordDown(uint32_t atMs) {
  if (!app.recording) return;
  app.recordDownMs = atMs;
}

bool cmdRecordUp(uint32_t atMs) {
  if (!app.recording) return false;
  uint32_t down = app.recordDownMs ? app.recordDownMs : (atMs > 120 ? atMs - 120 : 0);
  int onMs  = clampi((int)(atMs - down), 20, 5000);
  int gapMs = clampi((int)(down - app.recordLastEndMs), 0, 10000);

  char frag[24];
  snprintf(frag, sizeof(frag), "%d,%d;", onMs, gapMs);
  if (strlen(app.recordBuf) + strlen(frag) >= sizeof(app.recordBuf)) {
    setStatus("Pattern full - save or clear");
    return false;
  }
  strcat(app.recordBuf, frag);
  app.recordLastEndMs = atMs;
  app.recordDownMs = 0;
  return true;
}

void cmdRecordStop() {
  app.recording = false;
  app.recordDownMs = 0;
  setStatus("Recording stopped");
}

bool cmdRecordSave(int slot, const char* src) {
  if (slot < 0 || slot >= Limits::kRhythmSlots) return false;
  if (app.recordSlot == slot && app.recordBuf[0]) {
    strncpy(app.custom[slot].data, app.recordBuf, sizeof(app.custom[slot].data) - 1);
    app.custom[slot].data[sizeof(app.custom[slot].data) - 1] = '\0';
  }
  app.recording = false;
  if (!recompileCustom(slot)) {
    setStatus("Nothing recorded for that slot");
    return false;
  }
  markSettingsDirty();
  saveSettingsNow(src);
  setStatus("Custom rhythm saved");
  logEvent("rec-save", src);
  return true;
}

bool cmdRecordClear(int slot, const char* src) {
  if (slot < 0 || slot >= Limits::kRhythmSlots) return false;
  if (app.settings.rhythmId == kCustomBase + slot) cmdSetRhythm(0, src);
  app.custom[slot].data[0] = '\0';
  app.customs[slot].clear();
  app.recordBuf[0] = '\0';
  app.recording = false;
  markSettingsDirty();
  saveSettingsNow(src);
  setStatus("Custom slot cleared");
  logEvent("rec-clear", src);
  return true;
}

bool cmdRenameSlot(int slot, const char* name) {
  if (slot < 0 || slot >= Limits::kRhythmSlots || !name) return false;
  // Strip control characters: the name is rendered on a 320px display
  // and serialised into JSON.
  char clean[Limits::kRhythmNameLen];
  size_t o = 0;
  for (const char* p = name; *p && o < sizeof(clean) - 1; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c >= 0x20 && c != 0x7F) clean[o++] = (char)c;
  }
  clean[o] = '\0';
  while (o > 0 && clean[o - 1] == ' ') clean[--o] = '\0';
  if (o == 0) return false;

  strncpy(app.custom[slot].name, clean, sizeof(app.custom[slot].name) - 1);
  app.custom[slot].name[sizeof(app.custom[slot].name) - 1] = '\0';
  markSettingsDirty();
  return true;
}

}  // namespace pf
