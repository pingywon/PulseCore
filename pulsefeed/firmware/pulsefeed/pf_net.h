// =====================================================================
//  pf_net.h -- WiFi, setup AP, mDNS, captive portal
//
//  Changes that matter versus v43:
//
//  * The setup AP was OPEN. Anyone within radio range could drive the
//    machine. It now runs WPA2 with a per-device key derived from the
//    chip ID and displayed on the Network screen.
//
//  * WiFi.scanNetworks() ran synchronously inside an HTTP handler. A
//    full scan is several seconds during which loop() does not run --
//    which in v43 meant the solenoid timing froze mid-pulse. The scan
//    is now asynchronous and polled from the supervisor.
//
//  * Network reconfiguration triggered from inside a request handler
//    tore down the socket that was mid-response. Requests now set a
//    flag and the supervisor does the work between requests.
// =====================================================================
#pragma once

#include "pf_app.h"

namespace pf {
namespace net {

void begin();
void service();                 // from the supervisor loop only

bool staConnected();
bool apActive();
int  signalBars();
const char* ssidLabel();
const char* ipLabel();
const char* modeLabel();
const char* shareUrl();

void startSetupAp();
void stopSetupAp();
void joinNetwork(const char* ssid, const char* pass);
void forgetNetwork();

// Asynchronous scan. requestScan() returns immediately; scanReady()
// becomes true when results are available.
void requestScan();
bool scanReady();
bool scanRunning();
void scanJson(JsonOut& j);

void processDns();

}  // namespace net
}  // namespace pf
