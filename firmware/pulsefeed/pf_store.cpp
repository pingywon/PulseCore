// =====================================================================
//  pf_store.cpp
// =====================================================================
#include "pf_store.h"
#include <Preferences.h>
#include <string.h>

namespace pf {
namespace store {

namespace {

Preferences  g_prefs;
Settings     g_shadow;
CustomRhythm g_shadowCustom[Limits::kRhythmSlots];
bool         g_shadowValid = false;
uint32_t     g_writes = 0;

// --- change-only primitives ---------------------------------------
inline bool putIntIfChanged(const char* k, int v, int& shadow) {
  if (g_shadowValid && shadow == v) return false;
  g_prefs.putInt(k, v);
  shadow = v;
  g_writes++;
  return true;
}

inline bool putBoolIfChanged(const char* k, bool v, bool& shadow) {
  if (g_shadowValid && shadow == v) return false;
  g_prefs.putBool(k, v);
  shadow = v;
  g_writes++;
  return true;
}

inline bool putStrIfChanged(const char* k, const char* v, char* shadow, size_t cap) {
  if (g_shadowValid && strncmp(shadow, v, cap) == 0) return false;
  g_prefs.putString(k, v);
  strncpy(shadow, v, cap - 1);
  shadow[cap - 1] = '\0';
  g_writes++;
  return true;
}

void copyStr(char* dst, size_t cap, const String& src) {
  size_t n = src.length();
  if (n > cap - 1) n = cap - 1;
  memcpy(dst, src.c_str(), n);
  dst[n] = '\0';
}

}  // namespace

void begin() {
  g_shadowValid = false;
}

void loadSettings(Settings& s) {
  s.defaults();
  g_prefs.begin(PF_NVS_NAMESPACE, true);

  s.vacTarget       = g_prefs.getInt("vac", s.vacTarget);
  s.vacRamp         = g_prefs.getInt("ramp", s.vacRamp);
  s.vacWindowMs     = g_prefs.getInt("vwin", s.vacWindowMs);
  s.vacProportional = g_prefs.getBool("vprop", s.vacProportional);
  s.ppm             = g_prefs.getInt("ppm", s.ppm);
  s.ratio           = g_prefs.getInt("ratio", s.ratio);
  s.motor           = g_prefs.getInt("motor", s.motor);
  s.motorSoftMs     = g_prefs.getInt("msoft", s.motorSoftMs);

  s.rhythmId        = g_prefs.getInt("rid", s.rhythmId);
  s.rhythmSpeed     = g_prefs.getInt("rspd", s.rhythmSpeed);
  s.fav[0]          = g_prefs.getInt("fav0", s.fav[0]);
  s.fav[1]          = g_prefs.getInt("fav1", s.fav[1]);
  s.fav[2]          = g_prefs.getInt("fav2", s.fav[2]);

  s.runLimitMin          = g_prefs.getInt("rlim", s.runLimitMin);
  s.autoStopOnDisconnect = g_prefs.getBool("asod", s.autoStopOnDisconnect);

  s.darkTheme  = g_prefs.getBool("dark", s.darkTheme);
  s.backlight  = g_prefs.getInt("bl", s.backlight);

  s.webEnabled   = g_prefs.getBool("web", s.webEnabled);
  s.webPort      = g_prefs.getInt("port", s.webPort);
  s.authRequired = g_prefs.getBool("auth", s.authRequired);
  s.tzOffsetMin  = g_prefs.getInt("tz", s.tzOffsetMin);
  s.dstEnabled   = g_prefs.getBool("dst", s.dstEnabled);

  g_prefs.end();

  // Anything that was stored out of range -- by an older firmware, a
  // corrupted sector or a hand-edited backup -- is corrected here, once,
  // before it can ever reach the engine.
  s.validate();

  g_shadow = s;
  g_shadowValid = true;
}

int saveSettings(const Settings& in) {
  Settings s = in;
  s.validate();

  uint32_t before = g_writes;
  g_prefs.begin(PF_NVS_NAMESPACE, false);

  putIntIfChanged("vac",   s.vacTarget,   g_shadow.vacTarget);
  putIntIfChanged("ramp",  s.vacRamp,     g_shadow.vacRamp);
  putIntIfChanged("vwin",  s.vacWindowMs, g_shadow.vacWindowMs);
  putBoolIfChanged("vprop", s.vacProportional, g_shadow.vacProportional);
  putIntIfChanged("ppm",   s.ppm,         g_shadow.ppm);
  putIntIfChanged("ratio", s.ratio,       g_shadow.ratio);
  putIntIfChanged("motor", s.motor,       g_shadow.motor);
  putIntIfChanged("msoft", s.motorSoftMs, g_shadow.motorSoftMs);

  putIntIfChanged("rid",   s.rhythmId,    g_shadow.rhythmId);
  putIntIfChanged("rspd",  s.rhythmSpeed, g_shadow.rhythmSpeed);
  putIntIfChanged("fav0",  s.fav[0],      g_shadow.fav[0]);
  putIntIfChanged("fav1",  s.fav[1],      g_shadow.fav[1]);
  putIntIfChanged("fav2",  s.fav[2],      g_shadow.fav[2]);

  putIntIfChanged("rlim",  s.runLimitMin, g_shadow.runLimitMin);
  putBoolIfChanged("asod", s.autoStopOnDisconnect, g_shadow.autoStopOnDisconnect);

  putBoolIfChanged("dark", s.darkTheme,   g_shadow.darkTheme);
  putIntIfChanged("bl",    s.backlight,   g_shadow.backlight);

  putBoolIfChanged("web",  s.webEnabled,  g_shadow.webEnabled);
  putIntIfChanged("port",  s.webPort,     g_shadow.webPort);
  putBoolIfChanged("auth", s.authRequired, g_shadow.authRequired);
  putIntIfChanged("tz",    s.tzOffsetMin, g_shadow.tzOffsetMin);
  putBoolIfChanged("dst",  s.dstEnabled,  g_shadow.dstEnabled);

  g_prefs.end();
  g_shadowValid = true;
  return (int)(g_writes - before);
}

void loadCustom(CustomRhythm* slots) {
  g_prefs.begin(PF_NVS_NAMESPACE, true);
  for (int i = 0; i < Limits::kRhythmSlots; i++) {
    char nk[8], dk[8];
    snprintf(nk, sizeof(nk), "cn%d", i);
    snprintf(dk, sizeof(dk), "cd%d", i);

    char def[Limits::kRhythmNameLen];
    snprintf(def, sizeof(def), "Custom %d", i + 1);

    copyStr(slots[i].name, sizeof(slots[i].name), g_prefs.getString(nk, def));
    copyStr(slots[i].data, sizeof(slots[i].data), g_prefs.getString(dk, ""));
    if (slots[i].name[0] == '\0') strncpy(slots[i].name, def, sizeof(slots[i].name) - 1);
  }
  g_prefs.end();
  memcpy(g_shadowCustom, slots, sizeof(g_shadowCustom));
}

int saveCustom(const CustomRhythm* slots) {
  uint32_t before = g_writes;
  g_prefs.begin(PF_NVS_NAMESPACE, false);
  for (int i = 0; i < Limits::kRhythmSlots; i++) {
    char nk[8], dk[8];
    snprintf(nk, sizeof(nk), "cn%d", i);
    snprintf(dk, sizeof(dk), "cd%d", i);
    putStrIfChanged(nk, slots[i].name, g_shadowCustom[i].name, sizeof(g_shadowCustom[i].name));
    putStrIfChanged(dk, slots[i].data, g_shadowCustom[i].data, sizeof(g_shadowCustom[i].data));
  }
  g_prefs.end();
  return (int)(g_writes - before);
}

void loadWifi(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
  g_prefs.begin(PF_NVS_NAMESPACE, true);
  copyStr(ssid, ssidLen, g_prefs.getString("wssid", ""));
  copyStr(pass, passLen, g_prefs.getString("wpass", ""));
  g_prefs.end();
}

void saveWifi(const char* ssid, const char* pass) {
  g_prefs.begin(PF_NVS_NAMESPACE, false);
  g_prefs.putString("wssid", ssid ? ssid : "");
  g_prefs.putString("wpass", pass ? pass : "");
  g_writes += 2;
  g_prefs.end();
}

void clearWifi() { saveWifi("", ""); }

void loadPin(char* pin, size_t len) {
  g_prefs.begin(PF_NVS_NAMESPACE, true);
  copyStr(pin, len, g_prefs.getString("pin", ""));
  g_prefs.end();
}

void savePin(const char* pin) {
  g_prefs.begin(PF_NVS_NAMESPACE, false);
  g_prefs.putString("pin", pin ? pin : "");
  g_writes++;
  g_prefs.end();
}

void factoryReset() {
  g_prefs.begin(PF_NVS_NAMESPACE, false);
  g_prefs.clear();
  g_prefs.end();
  g_shadowValid = false;
}

uint32_t writeCount() { return g_writes; }

}  // namespace store
}  // namespace pf
