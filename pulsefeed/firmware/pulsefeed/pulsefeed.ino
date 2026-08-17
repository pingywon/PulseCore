// =====================================================================
//   PulseFeed  --  Pluto 9000 controller firmware
//   3-channel vacuum / pulsation / motor controller for M5Stack CoreS3
//
//   CH1 G5  vacuum solenoid
//   CH2 G6  pulsator solenoid
//   CH3 G7  12 V motor
//
//   Architecture
//   ------------
//   Core 1, priority 19 : engine task, 1 kHz, owns the output pins
//   Core 1, priority 1  : this loop() -- UI, HTTP, WiFi, SD logging
//   Core 0              : the WiFi/lwIP stack (Arduino default)
//
//   The single most important change from v43 is that pulse timing no
//   longer runs in loop(). In v43 the solenoid phase was computed in the
//   same loop that called server.handleClient(), wrote to the SD card
//   and repainted the screen, so a WiFi scan or a slow HTTP client
//   stretched a pulse by however long it took. Pulsation rate IS the
//   product; it now runs in a dedicated high-priority task that nothing
//   in the UI or network path can preempt.
//
//   Files: pf_core.*   portable, unit-tested engine (no Arduino code)
//          pf_hal.*    pins, PWM, clock
//          pf_app.*    state + the single command surface
//          pf_ui.*     touchscreen
//          pf_net.*    WiFi / AP / mDNS
//          pf_api.*    HTTP + REST + auth
//          pf_log.*    async SD logging
//          pf_store.*  NVS persistence
// =====================================================================

#include <M5Unified.h>
#include <esp_task_wdt.h>

#include "pf_config.h"
#include "pf_core.h"
#include "pf_hal.h"
#include "pf_app.h"
#include "pf_ui.h"
#include "pf_net.h"
#include "pf_api.h"
#include "pf_log.h"
#include "pf_store.h"
#include "pf_service.h"

using namespace pf;

static TaskHandle_t g_engineTask = nullptr;

// --------------------------------------------------------------------
//  Engine task: the only writer of the output pins.
// --------------------------------------------------------------------
static void engineTask(void*) {
  esp_task_wdt_add(nullptr);            // no-op if the TWDT is disabled

  TickType_t last = xTaskGetTickCount();
  const TickType_t period = pdMS_TO_TICKS(1000 / PF_ENGINE_HZ);

  for (;;) {
    uint64_t now = hal::nowUs();

    // The engine always ticks, even in service mode: it keeps the
    // deadman fed, the run limit honest and the model coherent, and it
    // returns all-off while stopped. Service mode then takes the pins
    // for itself if it is active.
    Outputs o = app.engine.tick(now);
    if (!service::tick(now)) {
      hal::applyOutputs(o);
    }

    esp_task_wdt_reset();
    vTaskDelayUntil(&last, period);
  }
}

// --------------------------------------------------------------------
void setup() {
  // FIRST. Before the PMIC, the display, I2C, anything. Until this runs
  // the channel GPIOs sit in their power-on default, and M5.begin() is
  // tens of milliseconds long. v43 configured the pins after M5.begin().
  hal::bootSafeOutputs();

  auto cfg = M5.config();
  cfg.internal_spk = false;
  M5.begin(cfg);
  Serial.begin(115200);

  appBegin();
  // The web dashboard is the primary control surface now, not a
  // convenience alongside the touchscreen -- a stale "web off" persisted
  // from an earlier toggle or a prior firmware must never survive a
  // reboot, or the only way back in is a USB cable. Boot always brings
  // the server up; the on-device toggle still works for the current
  // session if someone genuinely wants it off (pf_ui.cpp, B_NET_WEB).
  app.settings.webEnabled = true;
  hal::configureOutputs(false, app.settings.vacProportional);
  hal::allOff();

  ui::begin();
  ui::bootAnimation();

  app.sdOk = log::begin();
  {
    log::Snapshot s; buildSnapshot(s);
    log::event("boot", s);
  }

  api::ensurePin();
  net::begin();
  if (app.settings.webEnabled) api::begin(app.settings.webPort);

  xTaskCreatePinnedToCore(engineTask, "pf_engine", PF_ENGINE_STACK, nullptr,
                          PF_ENGINE_PRIO, &g_engineTask, PF_ENGINE_CORE);

  setStatus("Ready");
  ui::invalidate();

  Serial.printf("\n%s %s v%s  |  AP %s / %s  |  PIN %s\n",
                kProduct, kModel, kVersion, app.apSsid, app.apPass, app.pin);
}

// --------------------------------------------------------------------
void loop() {
  M5.update();

  // Feeding the deadman is what tells the engine that the supervisor is
  // alive. If this loop wedges -- a stuck HTTP client, a hung SD write,
  // a UI deadlock -- the engine stops the machine on its own.
  app.engine.feed(hal::nowUs());

  ui::handleTouch();

  if (api::running()) api::service();
  net::processDns();
  net::service();

  // Deferred work. Anything that tears down a socket or reconfigures the
  // radio happens here, between requests, never inside a handler.
  if (app.reqWebRestart) {
    app.reqWebRestart = false;
    api::stop();
    if (app.settings.webEnabled) {
      net::startSetupAp();
      api::begin(app.settings.webPort);
      setStatus("Web server restarted");
    } else {
      setStatus("Web server off");
    }
    ui::invalidate();
  }

  // Publish state for the logger, then let it drain to the card in
  // batches. Neither call touches the SD bus from a control path.
  {
    log::Snapshot s; buildSnapshot(s);
    log::publish(s);
  }
  log::sample();
  log::service();

  ui::tick();
  appService();

  if (app.reqReboot) {
    app.reqReboot = false;
    hal::allOff();
    saveSettingsNow("reboot");
    log::service();
    delay(150);
    ESP.restart();
  }

  // Yield to the scheduler. No delay() -- the engine task is higher
  // priority and preempts us anyway, but this keeps the idle task fed
  // so the watchdog stays happy.
  vTaskDelay(1);
}
