// =====================================================================
//  pf_app.h -- the single application state and command surface
//
//  In v43 the touch handler and each of the 20 web handlers mutated the
//  same pile of loose globals directly, with slightly different rules.
//  Starting from the screen logged a different event than starting from
//  the web page; the web START path did not check the e-stop latch the
//  same way; changing a value from the web marked settings dirty but
//  changing it from a hold-repeat did not.
//
//  Everything now goes through the cmd*() functions below, so there is
//  exactly one implementation of "what happens when the machine starts".
// =====================================================================
#pragma once

#include "pf_core.h"
#include "pf_store.h"
#include "pf_log.h"

namespace pf {

struct App {
  // --- control -----------------------------------------------------
  Settings      settings;
  Engine        engine;
  RhythmPattern builtins[kBuiltinCount];
  RhythmPattern customs[Limits::kRhythmSlots];
  CustomRhythm  custom[Limits::kRhythmSlots];

  // --- tap recorder -------------------------------------------------
  bool     recording;
  int      recordSlot;
  char     recordBuf[Limits::kRhythmDataLen];
  uint32_t recordDownMs;
  uint32_t recordLastEndMs;

  // --- identity / network ------------------------------------------
  char apSsid[33];
  char apPass[17];
  char pin[PF_PIN_LEN + 1];
  char staSsid[33];
  char staPass[65];
  char statusMsg[72];

  // --- housekeeping -------------------------------------------------
  bool     sdOk;
  uint64_t bootUs;
  bool     settingsDirty;
  uint32_t settingsDirtyAtMs;
  uint32_t nvsWrites;

  // Deferred work. Web handlers must never restart the network stack or
  // repaint the display from inside a request -- v43 did both, which
  // dropped the very response that was being written.
  volatile bool reqWebRestart;
  volatile bool reqWifiScan;
  volatile bool reqReboot;
};

extern App app;

// ------------------------------------------------------------------ //
//  Lifecycle
// ------------------------------------------------------------------ //
void appBegin();
void appService();              // called every supervisor loop

// ------------------------------------------------------------------ //
//  Commands -- the ONLY sanctioned way to change machine state.
//  `src` is a short origin tag ("screen", "web", "auto") recorded in
//  the session log so a run can be traced back to what asked for it.
// ------------------------------------------------------------------ //
bool cmdStart(const char* src);
void cmdStop(const char* src, StopReason why);
void cmdEstop(const char* src);
void cmdClearEstop(const char* src);

void cmdSetVac(int value, const char* src);
void cmdSetPpm(int value, const char* src);
void cmdSetRatio(int value, const char* src);
void cmdSetMotor(int value, const char* src);
void cmdSetRamp(int value, const char* src);
void cmdSetRhythm(int id, const char* src);
void cmdSetRhythmSpeed(int pct, const char* src);
void cmdSetBacklight(int step, const char* src);
void cmdSetTheme(bool dark, const char* src);

// Nudge helpers used by the touch hold-repeat.
void cmdNudgeVac(int delta, const char* src);
void cmdNudgePpm(int delta, const char* src);
void cmdNudgeMotor(int steps, const char* src);
void cmdNudgeRatio(int delta, const char* src);
void cmdNudgeRhythmSpeed(int delta, const char* src);
void cmdNudgeBacklight(int delta, const char* src);

// Rhythm slots
bool cmdRecordStart(int slot, const char* src);
void cmdRecordDown(uint32_t atMs);
bool cmdRecordUp(uint32_t atMs);
void cmdRecordStop();
bool cmdRecordSave(int slot, const char* src);
bool cmdRecordClear(int slot, const char* src);
bool cmdRenameSlot(int slot, const char* name);
bool cmdSeedSlot(int slot, int builtinId, const char* src);
bool recompileCustom(int slot);

// Persistence
void markSettingsDirty();
void saveSettingsNow(const char* src);

// Helpers shared by the UI and the API
const char* rhythmLabel(int id);
const char* rhythmNotation(int id);
void        applyEngineSettings();
void        buildSnapshot(log::Snapshot& s);
void        setStatus(const char* msg);

}  // namespace pf
