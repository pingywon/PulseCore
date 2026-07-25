// =====================================================================
//  pf_api.cpp
// =====================================================================
#include "pf_api.h"
#include "pf_net.h"
#include "pf_ui.h"
#include "pf_hal.h"
#include "pf_web.h"

#include <WebServer.h>
#include <WiFi.h>
#include <SD.h>
#include <esp_system.h>

namespace pf {
namespace api {

namespace {

WebServer* g_srv = nullptr;
uint32_t   g_reqs = 0;
uint32_t   g_rejects = 0;

struct Session {
  char     token[25];
  uint32_t expiresMs;
};
Session g_sessions[PF_MAX_SESSIONS];

int      g_authFails = 0;
uint32_t g_lockedUntil = 0;

char g_json[3072];

// ------------------------------------------------------------------ //
void randomToken(char* out, size_t len) {
  static const char cs[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  for (size_t i = 0; i + 1 < len; i++) out[i] = cs[esp_random() % 62];
  out[len - 1] = '\0';
}

bool tokenValid(const char* t) {
  if (!t || !*t) return false;
  uint32_t now = millis();
  for (int i = 0; i < PF_MAX_SESSIONS; i++) {
    if (g_sessions[i].token[0] && g_sessions[i].expiresMs > now &&
        strcmp(g_sessions[i].token, t) == 0) {
      return true;
    }
  }
  return false;
}

const char* issueToken() {
  uint32_t now = millis();
  int slot = 0;
  uint32_t oldest = 0xFFFFFFFF;
  for (int i = 0; i < PF_MAX_SESSIONS; i++) {
    if (!g_sessions[i].token[0] || g_sessions[i].expiresMs <= now) { slot = i; break; }
    if (g_sessions[i].expiresMs < oldest) { oldest = g_sessions[i].expiresMs; slot = i; }
  }
  randomToken(g_sessions[slot].token, sizeof(g_sessions[slot].token));
  g_sessions[slot].expiresMs = now + PF_SESSION_TTL_MS;
  return g_sessions[slot].token;
}

// Auth check. Mutations must present the token in a header: a
// cross-origin form post cannot set custom headers, so this is also the
// CSRF defence. GET file downloads may pass ?t= because the browser
// navigates to them directly.
bool authed(bool allowQueryToken = false) {
  if (!app.settings.authRequired) return true;
  String h = g_srv->header("X-PF-Token");
  if (h.length() && tokenValid(h.c_str())) return true;
  if (allowQueryToken && g_srv->hasArg("t") && tokenValid(g_srv->arg("t").c_str())) return true;
  return false;
}

bool requireAuth(bool allowQueryToken = false) {
  if (authed(allowQueryToken)) return true;
  g_rejects++;
  g_srv->send(401, "application/json", "{\"error\":\"auth_required\"}");
  return false;
}

bool requirePost() {
  if (g_srv->method() == HTTP_POST) return true;
  g_rejects++;
  g_srv->send(405, "application/json", "{\"error\":\"post_required\"}");
  return false;
}

void sendJson(JsonOut& j) {
  g_srv->sendHeader("Cache-Control", "no-store");
  g_srv->send(200, "application/json", j.c_str());
}

void sendOk(const char* msg = "ok") {
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj(); j.kvBool("ok", true); j.kvStr("msg", msg);
  j.kvStr("status", app.statusMsg);
  j.endObj();
  sendJson(j);
}

void sendErr(int code, const char* err) {
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj(); j.kvBool("ok", false); j.kvStr("error", err); j.endObj();
  g_srv->send(code, "application/json", j.c_str());
}

int argInt(const char* k, int fallback) {
  if (!g_srv->hasArg(k)) return fallback;
  return g_srv->arg(k).toInt();
}

void gzAsset(const char* mime, const uint8_t* data, size_t len) {
  g_srv->sendHeader("Content-Encoding", "gzip");
  g_srv->sendHeader("Cache-Control", "public, max-age=86400");
  g_srv->send_P(200, mime, (const char*)data, len);
}

// ------------------------------------------------------------------ //
//  Handlers
// ------------------------------------------------------------------ //
void hIndex() { g_reqs++; gzAsset("text/html", PF_INDEX_GZ, PF_INDEX_GZ_LEN); }

// Static metadata the browser fetches once. Deliberately unauthenticated
// so the login screen can render, and deliberately free of any machine
// state or identifying information.
void hMeta() {
  g_reqs++;
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj();
  j.kvStr("product", kProduct);
  j.kvStr("model", kModel);
  j.kvStr("version", kVersion);
  j.kvNum("api", 1);
  j.kvBool("authRequired", app.settings.authRequired);

  j.key("limits"); j.beginObj();
  j.kvNum("ppmMin", Limits::kPpmMin);   j.kvNum("ppmMax", Limits::kPpmMax);
  j.kvNum("ratioMin", Limits::kRatioMin); j.kvNum("ratioMax", Limits::kRatioMax);
  j.kvNum("motorStep", Limits::kMotorStep);
  j.kvNum("speedMin", Limits::kSpeedMin); j.kvNum("speedMax", Limits::kSpeedMax);
  j.kvNum("slots", Limits::kRhythmSlots);
  j.kvNum("customBase", kCustomBase);
  j.endObj();

  // The rhythm table comes from the firmware, so the browser cannot
  // drift out of step with the device the way v43's hardcoded JS list did.
  j.key("rhythms"); j.beginArr();
  for (int i = 1; i <= kBuiltinCount; i++) {
    j.beginObj();
    j.kvNum("id", i);
    j.kvStr("name", builtinName(i));
    j.kvStr("dots", builtinDots(i));
    j.endObj();
  }
  j.endArr();
  j.endObj();
  sendJson(j);
}

void hAuth() {
  g_reqs++;
  if (!requirePost()) return;

  uint32_t now = millis();
  if (g_lockedUntil && now < g_lockedUntil) {
    g_rejects++;
    sendErr(429, "locked_out");
    return;
  }

  String pin = g_srv->arg("pin");
  if (pin.length() == strlen(app.pin) && strcmp(pin.c_str(), app.pin) == 0) {
    g_authFails = 0;
    g_lockedUntil = 0;
    const char* tok = issueToken();
    JsonOut j(g_json, sizeof(g_json));
    j.beginObj(); j.kvBool("ok", true); j.kvStr("token", tok); j.endObj();
    log::event("web-login");
    sendJson(j);
    return;
  }

  g_authFails++;
  g_rejects++;
  if (g_authFails >= PF_AUTH_MAX_FAILS) {
    g_lockedUntil = now + PF_AUTH_LOCKOUT_MS;
    g_authFails = 0;
    log::event("web-login-lockout");
  }
  // Constant-ish response time; no hint about how wrong the PIN was.
  delay(250);
  sendErr(401, "bad_pin");
}

void hState() {
  g_reqs++;
  if (!requireAuth()) return;

  Outputs o = app.engine.outputs();
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj();

  j.kvBool("running", app.engine.running());
  j.kvBool("estop", app.engine.estop());
  j.kvNum("stopReason", (long)app.engine.lastStop());
  j.kvNum("runSec", (long)app.engine.runSeconds(hal::nowUs()));
  j.kvNum("pulses", (long)app.engine.pulseCount());
  j.kvReal("phase", app.engine.pulsePhase(), 3);

  j.kvNum("vac", app.settings.vacTarget);
  j.kvReal("vacActual", app.engine.vacActual(), 1);
  j.kvNum("ramp", app.settings.vacRamp);
  j.kvNum("ppm", app.settings.ppm);
  j.kvNum("ratio", app.settings.ratio);
  j.kvNum("motor", app.settings.motor);
  j.kvNum("motorSoftMs", app.settings.motorSoftMs);
  j.kvNum("runLimitMin", app.settings.runLimitMin);
  j.kvBool("autoStopOnDisconnect", app.settings.autoStopOnDisconnect);

  j.kvNum("rhythm", app.settings.rhythmId);
  j.kvStr("rhythmName", rhythmLabel(app.settings.rhythmId));
  j.kvStr("rhythmDots", rhythmNotation(app.settings.rhythmId));
  j.kvNum("rhythmSpeed", app.settings.rhythmSpeed);

  j.key("out"); j.beginObj();
  j.kvBool("vac", o.vacOn); j.kvBool("pulse", o.pulseOn); j.kvBool("motor", o.motorOn);
  j.kvNum("motorDuty", o.motorDuty);
  j.endObj();

  j.key("slots"); j.beginArr();
  for (int i = 0; i < Limits::kRhythmSlots; i++) {
    j.beginObj();
    j.kvNum("id", kCustomBase + i);
    j.kvStr("name", app.custom[i].name);
    j.kvStr("data", app.custom[i].data);
    j.kvBool("valid", app.customs[i].valid());
    j.endObj();
  }
  j.endArr();
  j.kvBool("recording", app.recording);
  j.kvNum("recordSlot", app.recordSlot);

  j.key("net"); j.beginObj();
  j.kvStr("ssid", net::ssidLabel());
  j.kvStr("ip", net::ipLabel());
  j.kvStr("mode", net::modeLabel());
  j.kvStr("share", net::shareUrl());
  j.kvNum("bars", net::signalBars());
  j.kvBool("ap", net::apActive());
  j.kvStr("apSsid", app.apSsid);
  j.endObj();

  j.key("sys"); j.beginObj();
  j.kvUNum("heap", ESP.getFreeHeap());
  j.kvUNum("uptime", (unsigned long)((hal::nowUs() - app.bootUs) / 1000000ULL));
  j.kvBool("sd", log::cardPresent());
  j.kvUNum("logWritten", log::written());
  j.kvUNum("logDropped", log::dropped());
  j.kvUNum("nvsWrites", store::writeCount());
  j.kvBool("dark", app.settings.darkTheme);
  j.kvNum("backlight", app.settings.backlight);
  j.kvStr("version", kVersion);
  j.endObj();

  j.kvStr("status", app.statusMsg);
  j.endObj();

  if (j.overflow()) { sendErr(500, "state_too_large"); return; }
  sendJson(j);
}

void hControl() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;
  String a = g_srv->arg("action");

  if      (a == "start") { if (!cmdStart("web")) { sendErr(423, "estop_latched"); return; } }
  else if (a == "stop")  cmdStop("web", STOP_OPERATOR);
  else if (a == "estop") { cmdEstop("web"); ui::setScreen(ui::SCR_ESTOP_CONFIRM); }
  else if (a == "release") { hal::allOff(); log::event("estop-release(web)"); ui::setScreen(ui::SCR_ESTOP_ACTIVE); }
  else if (a == "clear") { cmdClearEstop("web"); ui::setScreen(ui::SCR_HOME); }
  else { sendErr(400, "unknown_action"); return; }

  ui::invalidate();
  sendOk();
}

void hSettings() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;

  if (g_srv->hasArg("vac"))   cmdSetVac(argInt("vac", app.settings.vacTarget), "web");
  if (g_srv->hasArg("ppm"))   cmdSetPpm(argInt("ppm", app.settings.ppm), "web");
  if (g_srv->hasArg("ratio")) cmdSetRatio(argInt("ratio", app.settings.ratio), "web");
  if (g_srv->hasArg("motor")) cmdSetMotor(argInt("motor", app.settings.motor), "web");
  if (g_srv->hasArg("ramp"))  cmdSetRamp(argInt("ramp", app.settings.vacRamp), "web");

  if (g_srv->hasArg("backlight")) {
    cmdSetBacklight(argInt("backlight", app.settings.backlight), "web");
    ui::applyBacklight();
  }
  if (g_srv->hasArg("dark")) cmdSetTheme(argInt("dark", 1) == 1, "web");

  if (g_srv->hasArg("motorSoftMs")) {
    app.settings.motorSoftMs = clampi(argInt("motorSoftMs", app.settings.motorSoftMs),
                                      Limits::kSoftStartMin, Limits::kSoftStartMax);
    markSettingsDirty();
  }
  if (g_srv->hasArg("runLimitMin")) {
    app.settings.runLimitMin = clampi(argInt("runLimitMin", app.settings.runLimitMin),
                                      Limits::kRunLimitMin, Limits::kRunLimitMax);
    markSettingsDirty();
  }
  if (g_srv->hasArg("autoStop")) {
    app.settings.autoStopOnDisconnect = argInt("autoStop", 0) == 1;
    markSettingsDirty();
  }
  if (g_srv->hasArg("vacProportional")) {
    app.settings.vacProportional = argInt("vacProportional", 0) == 1;
    hal::configureOutputs(false, app.settings.vacProportional);
    markSettingsDirty();
  }
  if (g_srv->hasArg("authRequired")) {
    app.settings.authRequired = argInt("authRequired", 1) == 1;
    markSettingsDirty();
  }

  app.settings.validate();
  applyEngineSettings();
  ui::invalidate();
  sendOk();
}

void hRhythm() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;
  if (g_srv->hasArg("speed")) cmdSetRhythmSpeed(argInt("speed", app.settings.rhythmSpeed), "web");
  if (g_srv->hasArg("id"))    cmdSetRhythm(argInt("id", 0), "web");
  if (g_srv->hasArg("fav")) {
    int slot = clampi(argInt("fav", 1), 1, 3) - 1;
    app.settings.fav[slot] = app.settings.rhythmId;
    markSettingsDirty();
  }
  ui::invalidate();
  sendOk();
}

void hCustom() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;

  int slot = clampi(argInt("slot", 1), 1, Limits::kRhythmSlots) - 1;
  String a = g_srv->arg("action");

  if (g_srv->hasArg("name")) cmdRenameSlot(slot, g_srv->arg("name").c_str());

  if      (a == "arm")   cmdRecordStart(slot, "web");
  else if (a == "down")  cmdRecordDown(millis());
  else if (a == "up")    { if (!cmdRecordUp(millis())) { sendErr(409, "not_recording_or_full"); return; } }
  else if (a == "stop")  cmdRecordStop();
  else if (a == "save")  { if (!cmdRecordSave(slot, "web")) { sendErr(400, "nothing_recorded"); return; } }
  else if (a == "clear") cmdRecordClear(slot, "web");
  else if (a == "play")  cmdSetRhythm(kCustomBase + slot, "web");
  else if (a == "rename") saveSettingsNow("web");
  else if (a != "") { sendErr(400, "unknown_action"); return; }

  ui::invalidate();
  sendOk();
}

void hWifiScan() {
  g_reqs++;
  if (!requireAuth()) return;
  net::requestScan();                 // returns immediately
  JsonOut j(g_json, sizeof(g_json));
  net::scanJson(j);
  sendJson(j);
}

void hWifiJoin() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;
  if (!g_srv->hasArg("ssid")) { sendErr(400, "ssid_required"); return; }
  // Copy out before responding: joinNetwork is deferred to the
  // supervisor so the response makes it out before the radio flips.
  static char ss[33], pp[65];
  strncpy(ss, g_srv->arg("ssid").c_str(), sizeof(ss) - 1); ss[sizeof(ss) - 1] = 0;
  strncpy(pp, g_srv->arg("pass").c_str(), sizeof(pp) - 1); pp[sizeof(pp) - 1] = 0;
  sendOk("joining");
  net::joinNetwork(ss, pp);
}

void hWifiForget() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;
  sendOk("forgotten");
  net::forgetNetwork();
}

void hLogs() {
  g_reqs++;
  if (!requireAuth()) return;
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj();
  j.kvBool("sd", log::cardPresent());
  j.kvStr("current", log::currentPath());
  j.kvUNum("written", log::written());
  j.kvUNum("dropped", log::dropped());
  j.key("files");
  log::listJson(j);
  j.endObj();
  sendJson(j);
}

void hLogGet() {
  g_reqs++;
  if (!requireAuth(true)) return;
  char path[64];
  String f = g_srv->arg("file");
  if (!log::resolvePath(f.c_str(), path, sizeof(path))) { sendErr(404, "no_such_log"); return; }
  File file = SD.open(path, FILE_READ);
  if (!file) { sendErr(500, "open_failed"); return; }
  g_srv->sendHeader("Content-Disposition", "inline");
  g_srv->streamFile(file, "text/csv");
  file.close();
}

// Unauthenticated liveness probe for the reverse proxy. Reveals nothing
// about the machine's state or its owner.
void hHealth() {
  g_reqs++;
  JsonOut j(g_json, sizeof(g_json));
  j.beginObj(); j.kvBool("ok", true); j.kvNum("api", 1); j.endObj();
  g_srv->send(200, "application/json", j.c_str());
}

void hSystem() {
  g_reqs++;
  if (!requirePost() || !requireAuth()) return;
  String a = g_srv->arg("action");
  if (a == "save") { saveSettingsNow("web"); sendOk("saved"); return; }
  if (a == "reboot") {
    cmdStop("web", STOP_OPERATOR);
    sendOk("rebooting");
    app.reqReboot = true;
    return;
  }
  if (a == "newpin") {
    ensurePin();      // regenerates below when forced
    sendOk("pin_rotated");
    return;
  }
  sendErr(400, "unknown_action");
}

void hStats() { g_reqs++; gzAsset("text/html", PF_STATS_GZ, PF_STATS_GZ_LEN); }

// Captive-portal probes. Answering these correctly is what makes a
// phone pop the "sign in to network" sheet automatically.
void hNotFound() {
  g_reqs++;
  String uri = g_srv->uri();
  if (uri.startsWith("/api/")) { sendErr(404, "no_such_endpoint"); return; }

  if (net::apActive()) {
    g_srv->sendHeader("Location", String("http://") + net::ipLabel() + "/", true);
    g_srv->send(302, "text/plain", "");
    return;
  }
  gzAsset("text/html", PF_INDEX_GZ, PF_INDEX_GZ_LEN);
}

}  // namespace

// ------------------------------------------------------------------ //
void ensurePin() {
  store::loadPin(app.pin, sizeof(app.pin));
  bool ok = strlen(app.pin) == PF_PIN_LEN;
  for (size_t i = 0; ok && i < strlen(app.pin); i++) {
    if (app.pin[i] < '0' || app.pin[i] > '9') ok = false;
  }
  if (ok) return;
  for (int i = 0; i < PF_PIN_LEN; i++) app.pin[i] = '0' + (esp_random() % 10);
  app.pin[PF_PIN_LEN] = '\0';
  store::savePin(app.pin);
}

void begin(uint16_t port) {
  stop();
  memset(g_sessions, 0, sizeof(g_sessions));
  g_srv = new WebServer(port);

  // WebServer only retains headers it was told to collect.
  const char* wanted[] = { "X-PF-Token" };
  g_srv->collectHeaders(wanted, 1);

  g_srv->on("/", HTTP_GET, hIndex);
  g_srv->on("/stats", HTTP_GET, hStats);

  g_srv->on("/api/v1/meta",        HTTP_GET,  hMeta);
  g_srv->on("/api/v1/health",      HTTP_GET,  hHealth);
  g_srv->on("/api/v1/auth",        HTTP_POST, hAuth);
  g_srv->on("/api/v1/state",       HTTP_GET,  hState);
  g_srv->on("/api/v1/control",     HTTP_POST, hControl);
  g_srv->on("/api/v1/settings",    HTTP_POST, hSettings);
  g_srv->on("/api/v1/rhythm",      HTTP_POST, hRhythm);
  g_srv->on("/api/v1/custom",      HTTP_POST, hCustom);
  g_srv->on("/api/v1/wifi/scan",   HTTP_GET,  hWifiScan);
  g_srv->on("/api/v1/wifi/join",   HTTP_POST, hWifiJoin);
  g_srv->on("/api/v1/wifi/forget", HTTP_POST, hWifiForget);
  g_srv->on("/api/v1/logs",        HTTP_GET,  hLogs);
  g_srv->on("/api/v1/logs/get",    HTTP_GET,  hLogGet);
  g_srv->on("/api/v1/system",      HTTP_POST, hSystem);

  g_srv->onNotFound(hNotFound);
  g_srv->begin();
}

void stop() {
  if (!g_srv) return;
  g_srv->stop();
  delete g_srv;
  g_srv = nullptr;
}

void service() { if (g_srv) g_srv->handleClient(); }
bool running()  { return g_srv != nullptr; }
uint32_t requestCount() { return g_reqs; }
uint32_t rejectCount()  { return g_rejects; }

}  // namespace api
}  // namespace pf
