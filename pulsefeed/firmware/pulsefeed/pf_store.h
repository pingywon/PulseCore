// =====================================================================
//  pf_store.h -- persistent settings (NVS) with change-only writes
//
//  v43 rewrote all ~30 NVS keys on every save, and it saved on every
//  slider move (after a 900 ms idle) and on every button release. NVS
//  sectors are rated around 100k erase cycles; a demo session dragging
//  the vacuum slider could burn through thousands of writes in an hour.
//  This layer keeps a shadow of what is actually on flash and writes
//  only the keys that genuinely changed.
// =====================================================================
#pragma once

#include "pf_config.h"
#include "pf_core.h"
#include <Arduino.h>

namespace pf {

struct CustomRhythm {
  char name[Limits::kRhythmNameLen];
  char data[Limits::kRhythmDataLen];
};

namespace store {

void begin();

void loadSettings(Settings& s);
// Returns the number of NVS keys actually written.
int  saveSettings(const Settings& s);

void loadCustom(CustomRhythm* slots);
int  saveCustom(const CustomRhythm* slots);

// WiFi credentials live apart from Settings so they are never emitted
// by the state API by accident.
void     loadWifi(char* ssid, size_t ssidLen, char* pass, size_t passLen);
void     saveWifi(const char* ssid, const char* pass);
void     clearWifi();

// Web access PIN. Generated on first boot, shown on the device screen.
void loadPin(char* pin, size_t len);
void savePin(const char* pin);

void factoryReset();

// Telemetry: how many NVS key writes have happened since boot.
uint32_t writeCount();

}  // namespace store
}  // namespace pf
