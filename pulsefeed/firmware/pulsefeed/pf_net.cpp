// =====================================================================
//  pf_net.cpp
// =====================================================================
#include "pf_net.h"
#include "pf_ui.h"
#include "pf_hal.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>

namespace pf {
namespace net {

namespace {

DNSServer  g_dns;
IPAddress  g_apIp(192, 168, 4, 1);
bool       g_ap        = false;
bool       g_dnsUp     = false;
bool       g_connecting = false;
bool       g_saveOnOk  = false;
uint32_t   g_connectStart = 0;
char       g_pendSsid[33] = "";
char       g_pendPass[65] = "";

int        g_scanState = 0;      // 0 idle, 1 running, 2 ready
uint32_t   g_scanAt    = 0;

char g_ssidLbl[34] = "Local only";
char g_ipLbl[20]   = "0.0.0.0";
char g_modeLbl[28] = "No network";
char g_share[40]   = "";

void deriveIdentity() {
  uint64_t mac = ESP.getEfuseMac();
  uint32_t lo = (uint32_t)(mac & 0xFFFFFF);

  snprintf(app.apSsid, sizeof(app.apSsid), PF_AP_SSID_PREFIX "%06X", (unsigned)lo);

  // A key that is stable per device, printable, and not guessable from
  // the SSID alone without knowing the mixing constants.
  static const char cs[] = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
  uint32_t h = (uint32_t)(mac ^ (mac >> 29)) * 2654435761u;
  for (int i = 0; i < 8; i++) {
    app.apPass[i] = cs[h % 32];
    h = h / 32 + 0x9E3779B9u + (uint32_t)i * 7919u;
  }
  app.apPass[8] = '\0';
}

void refreshLabels() {
  bool sta = WiFi.status() == WL_CONNECTED;

  if (sta)                   snprintf(g_ssidLbl, sizeof(g_ssidLbl), "%s", WiFi.SSID().c_str());
  else if (g_connecting)     snprintf(g_ssidLbl, sizeof(g_ssidLbl), "Joining %s", g_pendSsid);
  else if (g_ap)             snprintf(g_ssidLbl, sizeof(g_ssidLbl), "%s", app.apSsid);
  else                       snprintf(g_ssidLbl, sizeof(g_ssidLbl), "Local only");

  if (sta)      snprintf(g_ipLbl, sizeof(g_ipLbl), "%s", WiFi.localIP().toString().c_str());
  else if (g_ap) snprintf(g_ipLbl, sizeof(g_ipLbl), "%s", g_apIp.toString().c_str());
  else          snprintf(g_ipLbl, sizeof(g_ipLbl), "0.0.0.0");

  if (sta && g_ap)   snprintf(g_modeLbl, sizeof(g_modeLbl), "LAN + setup AP");
  else if (sta)      snprintf(g_modeLbl, sizeof(g_modeLbl), "LAN");
  else if (g_ap)     snprintf(g_modeLbl, sizeof(g_modeLbl), "Setup AP");
  else               snprintf(g_modeLbl, sizeof(g_modeLbl), "No network");

  if (sta || g_ap) {
    if (app.settings.webPort == 80)
      snprintf(g_share, sizeof(g_share), "http://%s", g_ipLbl);
    else
      snprintf(g_share, sizeof(g_share), "http://%s:%d", g_ipLbl, app.settings.webPort);
  } else {
    snprintf(g_share, sizeof(g_share), "no web address yet");
  }

  ui::setNetLines(g_ssidLbl, g_ipLbl, g_modeLbl, signalBars(), g_ap);
}

void onEvent(arduino_event_t* e) {
  if (!e) return;
  switch (e->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      g_connecting = false;
      if (g_saveOnOk) {
        store::saveWifi(g_pendSsid, g_pendPass);
        strncpy(app.staSsid, g_pendSsid, sizeof(app.staSsid) - 1);
        strncpy(app.staPass, g_pendPass, sizeof(app.staPass) - 1);
        g_saveOnOk = false;
      }
      setStatus("Joined LAN");
      log::event("wifi-connected");
      // Time zone from settings rather than a hardcoded US Eastern.
      configTime(app.settings.tzOffsetMin * 60,
                 app.settings.dstEnabled ? 3600 : 0,
                 "pool.ntp.org", "time.nist.gov");
      refreshLabels();
      ui::invalidate();
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (!g_connecting) {
        setStatus(g_ap ? "LAN lost - setup AP still up" : "LAN lost - local control ready");
        // Optional safety interlock: stop the machine if the operator
        // asked for remote supervision and the link goes away.
        if (app.settings.autoStopOnDisconnect && app.engine.running()) {
          cmdStop("auto", STOP_FAULT);
          setStatus("Stopped: network lost");
        }
      }
      refreshLabels();
      ui::invalidate();
      break;

    default: break;
  }
}

}  // namespace

// ------------------------------------------------------------------ //
void begin() {
  deriveIdentity();
  store::loadWifi(app.staSsid, sizeof(app.staSsid), app.staPass, sizeof(app.staPass));

  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.onEvent(onEvent);

  startSetupAp();

  if (app.staSsid[0]) joinNetwork(app.staSsid, app.staPass);

  if (MDNS.begin(PF_MDNS_HOST)) {
    MDNS.addService("http", "tcp", app.settings.webPort);
  }
  refreshLabels();
}

void startSetupAp() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(g_apIp, g_apIp, IPAddress(255, 255, 255, 0));
  // WPA2 with a per-device key. v43 called softAP(ssid) with no
  // password, which puts full machine control on an open radio.
  bool ok = WiFi.softAP(app.apSsid, app.apPass);
  g_ap = ok;
  if (ok) {
    if (!g_dnsUp) { g_dns.start(PF_DNS_PORT, "*", g_apIp); g_dnsUp = true; }
    setStatus("Setup AP up");
    log::event("ap-start");
  } else {
    setStatus("Setup AP failed");
    log::event("ap-fail");
  }
  refreshLabels();
}

void stopSetupAp() {
  if (!g_ap) return;
  if (g_dnsUp) { g_dns.stop(); g_dnsUp = false; }
  WiFi.softAPdisconnect(true);
  g_ap = false;
  refreshLabels();
}

void joinNetwork(const char* ssid, const char* pass) {
  if (!ssid || !*ssid) return;
  strncpy(g_pendSsid, ssid, sizeof(g_pendSsid) - 1); g_pendSsid[sizeof(g_pendSsid) - 1] = 0;
  strncpy(g_pendPass, pass ? pass : "", sizeof(g_pendPass) - 1); g_pendPass[sizeof(g_pendPass) - 1] = 0;
  g_saveOnOk = true;
  g_connecting = true;
  g_connectStart = millis();

  WiFi.mode(g_ap ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(g_pendSsid, g_pendPass);
  setStatus("Joining network in background");
  refreshLabels();
}

void forgetNetwork() {
  store::clearWifi();
  app.staSsid[0] = '\0';
  app.staPass[0] = '\0';
  g_connecting = false;
  WiFi.disconnect(true, true);
  setStatus("Saved network cleared");
  refreshLabels();
}

// ------------------------------------------------------------------ //
void requestScan() {
  if (g_scanState == 1) return;
  if (g_scanState == 2 && (millis() - g_scanAt) < PF_SCAN_MAX_AGE_MS) return;
  WiFi.scanDelete();
  // async = true. This is the whole point: the call returns immediately
  // and the radio does the work in the background.
  WiFi.scanNetworks(true, true, false, 200);
  g_scanState = 1;
}

bool scanReady()   { return g_scanState == 2; }
bool scanRunning() { return g_scanState == 1; }

void scanJson(JsonOut& j) {
  j.beginObj();
  j.kvBool("scanning", g_scanState == 1);
  j.key("networks");
  j.beginArr();
  if (g_scanState == 2) {
    int n = WiFi.scanComplete();
    if (n < 0) n = 0;
    if (n > 24) n = 24;
    for (int i = 0; i < n; i++) {
      j.beginObj();
      j.kvStr("ssid", WiFi.SSID(i).c_str());
      j.kvNum("rssi", WiFi.RSSI(i));
      j.kvBool("open", WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      j.endObj();
    }
  }
  j.endArr();
  j.endObj();
}

void processDns() { if (g_dnsUp) g_dns.processNextRequest(); }

// ------------------------------------------------------------------ //
void service() {
  if (g_scanState == 1) {
    int n = WiFi.scanComplete();
    if (n >= 0) { g_scanState = 2; g_scanAt = millis(); }
    else if (n == WIFI_SCAN_FAILED) { g_scanState = 0; }
  }

  if (g_connecting && (millis() - g_connectStart) > PF_STA_TIMEOUT_MS) {
    g_connecting = false;
    g_saveOnOk = false;
    setStatus("Network not found - local control ready");
    log::event("wifi-timeout");
    refreshLabels();
    ui::invalidate();
  }

  static uint32_t lastLabels = 0;
  if (millis() - lastLabels > 2000) { lastLabels = millis(); refreshLabels(); }
}

bool staConnected() { return WiFi.status() == WL_CONNECTED; }
bool apActive()     { return g_ap; }

int signalBars() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int r = WiFi.RSSI();
  if (r >= -55) return 4;
  if (r >= -67) return 3;
  if (r >= -75) return 2;
  if (r >= -85) return 1;
  return 0;
}

const char* ssidLabel() { return g_ssidLbl; }
const char* ipLabel()   { return g_ipLbl; }
const char* modeLabel() { return g_modeLbl; }
const char* shareUrl()  { return g_share; }

}  // namespace net
}  // namespace pf
