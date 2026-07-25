// =====================================================================
//  pf_config.h -- board wiring and build-time policy
//
//  Everything hardware-specific is gathered here so porting to another
//  board is a single-file job. v43 scattered pin numbers, magic screen
//  coordinates and behavioural constants through 2,900 lines.
// =====================================================================
#pragma once

#include <Arduino.h>

// ------------------------------------------------------------------ //
//  Target: M5Stack CoreS3 / CoreS3 SE  (ESP32-S3, 320x240 touch)
// ------------------------------------------------------------------ //
#define PF_SCREEN_W 320
#define PF_SCREEN_H 240

// microSD (CoreS3 wiring, unchanged from v43 -- verified against the
// M5Stack CoreS3 schematic pinout)
static const int PF_SD_CS   = 4;
static const int PF_SD_SCK  = 36;
static const int PF_SD_MISO = 35;
static const int PF_SD_MOSI = 37;
static const uint32_t PF_SD_HZ = 20000000;   // 25 MHz was marginal on long ribbons

// ------------------------------------------------------------------ //
//  Output channels
//
//  CH1 G5  vacuum solenoid
//  CH2 G6  pulsator solenoid   <- the product's core function
//  CH3 G7  12 V motor
//  G8      reserved, held OFF
// ------------------------------------------------------------------ //
static const int PF_PIN_VAC     = 5;
static const int PF_PIN_PULSE   = 6;
static const int PF_PIN_MOTOR   = 7;
static const int PF_PIN_SPARE   = 8;

// ---------------------------------------------------------------------
//  OUTPUT POLARITY -- READ BEFORE CHANGING
// ---------------------------------------------------------------------
//  The v43 hardware notes state the driver board is ground-switched
//  through an optocoupler: the GPIO pulls the input's ground side LOW
//  to turn a channel ON, and OFF is left high-impedance because the
//  author did not want the MCU sourcing current into the opto input.
//
//  I have no way to verify that board here, and the failure mode of
//  guessing wrong is "channels energise unexpectedly", so the default
//  below reproduces v43's electrical behaviour EXACTLY.
//
//  PF_OFF_MODE options:
//    PF_OFF_HIGHZ     pinMode(INPUT)        <- v43 behaviour, default
//    PF_OFF_PULLUP    pinMode(INPUT_PULLUP) weak hold at the OFF level;
//                     safer against floating inputs, ~70 uA through the
//                     internal 45k -- far under any opto's turn-on
//                     current, but confirm on real hardware first
//    PF_OFF_DRIVEN    digitalWrite(HIGH)    required for hardware PWM
// ---------------------------------------------------------------------
#define PF_OFF_HIGHZ   0
#define PF_OFF_PULLUP  1
#define PF_OFF_DRIVEN  2

#ifndef PF_OFF_MODE
#define PF_OFF_MODE PF_OFF_HIGHZ
#endif

// Channels are active LOW.
#define PF_ACTIVE_LEVEL LOW

// ------------------------------------------------------------------ //
//  Hardware PWM (LEDC)
//
//  Only usable when the channel can be driven push-pull, because PWM
//  has no high-impedance state. Off by default for the same reason as
//  above; the operator opts in per channel from Settings once they have
//  confirmed their driver board is happy being driven high.
// ------------------------------------------------------------------ //
static const int      PF_LEDC_MOTOR_CH   = 0;
static const int      PF_LEDC_VAC_CH     = 1;
static const uint32_t PF_LEDC_MOTOR_HZ   = 20000;   // above audible range
static const uint32_t PF_LEDC_VAC_HZ     = 200;     // proportional valves are slow
static const uint8_t  PF_LEDC_BITS       = 8;

// ------------------------------------------------------------------ //
//  Engine task
// ------------------------------------------------------------------ //
static const uint32_t PF_ENGINE_HZ       = 1000;
static const int      PF_ENGINE_CORE     = 1;   // Arduino loop's core; WiFi owns core 0
static const int      PF_ENGINE_PRIO     = 19;  // above loopTask(1), below WiFi(23)
static const int      PF_ENGINE_STACK    = 4096;

// ------------------------------------------------------------------ //
//  Networking
// ------------------------------------------------------------------ //
#define PF_AP_SSID_PREFIX "PulseFeed-"
#define PF_MDNS_HOST      "pulsefeed"
static const uint16_t PF_DNS_PORT        = 53;
static const uint32_t PF_STA_TIMEOUT_MS  = 20000;
static const uint32_t PF_SCAN_MAX_AGE_MS = 30000;

// Session tokens issued to authenticated browsers.
static const int      PF_MAX_SESSIONS    = 4;
static const uint32_t PF_SESSION_TTL_MS  = 12UL * 3600UL * 1000UL;
static const int      PF_PIN_LEN         = 6;

// Brute-force damping on the PIN endpoint.
static const int      PF_AUTH_MAX_FAILS  = 5;
static const uint32_t PF_AUTH_LOCKOUT_MS = 60000;

// ------------------------------------------------------------------ //
//  Storage
// ------------------------------------------------------------------ //
#define PF_NVS_NAMESPACE "pulsefeed"
#define PF_LOG_ROOT      "/log"
#define PF_CFG_BACKUP    "/pulsefeed.cfg"

// Async log ring. Entries are queued by any task and drained by the
// supervisor; nothing ever blocks on the SD card from a control path.
static const int      PF_LOG_QUEUE_LEN   = 48;
static const int      PF_LOG_LINE_MAX    = 224;
static const uint32_t PF_LOG_SAMPLE_MS   = 2500;
static const uint32_t PF_LOG_FLUSH_MS    = 1000;
