#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// =====================================================
//  PulseCore / Pluto 9000 - CoreS3/SE Prototype v43
//  Local-first boot: VAC, PPM/Rhythm, Motor usable immediately.
// =====================================================

// Button rectangle type must be declared before any functions.
// Arduino IDE auto-prototypes functions near the top of the generated .cpp;
// keeping this type here prevents "BtnRect was not declared" compile errors.
struct BtnRect { int x; int y; int w; int h; };

// ------------------ GLOBAL APP STATE ------------------
Preferences prefs;
int webPort = 80;
WebServer* serverPtr = nullptr;
#define server (*serverPtr)
M5Canvas bootSprite(&M5.Display);

const char* APP_VERSION = "v43-web-custom-rhythms-sdlog-compilecheck";

// CoreS3 microSD wiring
const int SD_CS_PIN   = 4;
const int SD_SCK_PIN  = 36;
const int SD_MISO_PIN = 35;
const int SD_MOSI_PIN = 37;

// Current 3-channel prototype pin truth. The unused output pin is forced safe/off.
const int PIN_SOL_VAC     = 5;
const int PIN_SOL_PULSE   = 6;
const int PIN_SOL_MOTOR   = 7;
const int PIN_UNUSED_OUTPUT = 8;

const int PPM_MIN = 0;
const int PPM_ACTIVE_MIN = 30;
const int PPM_MAX = 400;
const int PPM_DEFAULT = 0;

const int SCREEN_HOME = 0;
const int SCREEN_VAC = 1;
const int SCREEN_PPM = 2;
const int SCREEN_MOTOR = 3;
const int SCREEN_RHYTHM = 4;
const int SCREEN_SETTINGS = 5;
const int SCREEN_WIFI = 6;
const int SCREEN_LIVE = 7;
const int SCREEN_ESTOP_CONFIRM = 9;
const int SCREEN_ESTOP_ACTIVE = 10;

int currentScreen = SCREEN_HOME;
int previousScreen = SCREEN_HOME;

bool darkTheme = true;
bool largeButtons = true;
bool running = false;
bool estopLatched = false;
bool webControlEnabled = true;
bool mobileMode = true;
bool simpleHome = false;
bool showAdvancedWeb = true;
int webRefreshSec = 1;
String staSsid = "";
String staPass = "";

bool showVac = true;
bool showPpm = true;
bool showLive = true;
bool showSettings = true;
bool showWifi = true;

int vacTarget = 0;            // 0-100% relative vacuum command
float vacActual = 0.0f;       // simulated slow vacuum response
int ppmTarget = PPM_DEFAULT;  // 0=off, active range 0 / 30-400 PPM
int backlightStep = 7;        // 1-10
int vacRampSpeed = 5;         // 1-10; lower = slower reaction
int motorPowerTarget = 0;     // 0-100% in 10% steps; 12V motor software PWM command on CH3/G7
int screenScale = 2;          // 1-3; demo setting for future layout tuning
int graphSpeed = 4;           // 1-10 seconds shown in PPM graph window
int releaseMs = 1200;         // future emergency release timing setting
int sdEventCount = 0;          // current boot session SD event counter
unsigned long lastSdLogMs = 0;
unsigned long lastSdSampleMs = 0;
const unsigned long SD_SAMPLE_MS = 2500;
String sessionLogPath = "";
bool wifiActive = false;
bool wifiShutdownRequested = false;
bool wifiResetRequested = false;
bool setupApActive = false;
DNSServer dnsServer;
const byte DNS_PORT = 53;
const char* SETUP_AP_SSID = "Pluto9000-Setup";
IPAddress setupApIp(192, 168, 4, 1);
String wifiLastMessage = "Local controls ready";
bool wifiStaConnecting = false;
bool pendingWifiSave = false;
String pendingWifiSsid = "";
String pendingWifiPass = "";
unsigned long wifiStaConnectStartMs = 0;
const unsigned long WIFI_BACKGROUND_TIMEOUT_MS = 18000;

// WiFi setup is AP + web portal only. The touchscreen must never wait on WiFi.
bool solVacOn = false;
bool solPulseOn = false;
bool solMotorOn = false;
bool unusedOutputOn = false;

const int MODE_IDLE = 0;
const int MODE_PROGRAM = 1;
int activeMode = MODE_IDLE;
unsigned long programStartMs = 0;
const unsigned long VACUUM_PWM_WINDOW_MS = 1000;
const unsigned long MOTOR_PWM_WINDOW_MS = 500;  // software PWM window for CH3 motor power steps
int rhythmMode = 0;
int rhythmStep = -1;
unsigned long lastRhythmMs = 0;
int rhythmSpeedPct = 100;
int favoriteRhythm1 = 3;
int favoriteRhythm2 = 7;
int favoriteRhythm3 = 13;
const unsigned long RHYTHM_BASE_TICK_MS = 250;
const int BUILTIN_RHYTHM_COUNT = 21;  // 0=Off, 1-20 are built-in CH2 pulse/rhythm patterns
const int CUSTOM_RHYTHM_SLOTS = 5;
const int CUSTOM_RHYTHM_BASE = 21;  // modes 21-25 are Custom 1-5
const int RHYTHM_MODE_COUNT = CUSTOM_RHYTHM_BASE + CUSTOM_RHYTHM_SLOTS;
const int CUSTOM_RHYTHM_MAX_CHARS = 260;
String customRhythmName[CUSTOM_RHYTHM_SLOTS] = {"Custom 1", "Custom 2", "Custom 3", "Custom 4", "Custom 5"};
String customRhythmData[CUSTOM_RHYTHM_SLOTS] = {"", "", "", "", ""};  // pulseMs,gapMs;pulseMs,gapMs;...
bool customRecording = false;
int customRecordSlot = 0;
String customRecordData = "";
unsigned long customRecordDownMs = 0;
unsigned long customLastTapEndMs = 0;
unsigned long customLoopStartMs = 0;

const int LIVE_POINTS = 96;
int liveVacHist[LIVE_POINTS];
int livePpmHist[LIVE_POINTS];
int liveMotorHist[LIVE_POINTS];
int liveHistIndex = 0;
unsigned long lastLiveSampleMs = 0;

bool sdReady = false;
bool sdDataReady = false;  // true only after Pluto log/stats files are prepared
bool settingsDirty = false;
unsigned long lastSettingsChangeMs = 0;
const unsigned long SAVE_IDLE_MS = 900;

bool uiNeedsFullRedraw = true;
bool vacValueDirty = true;
bool ppmValueDirty = true;
bool motorValueDirty = true;
bool rhythmValueDirty = true;
bool settingsValueDirty = true;
bool liveValueDirty = true;

unsigned long lastGraphMs = 0;
unsigned long lastLiveMs = 0;
unsigned long lastVacPhysicsMs = 0;
unsigned long bootMs = 0;

// Hold/repeat control. No custom structs to avoid Arduino prototype quirks.
int holdControl = 0;
unsigned long holdStartMs = 0;
unsigned long lastHoldRepeatMs = 0;
const unsigned long FIRST_REPEAT_MS = 290;
const unsigned long REPEAT_MS = 150;
const unsigned long FAST_AFTER_MS = 1000;
const unsigned long TURBO_AFTER_MS = 3000;

// Colors assigned after M5.begin() and after settings load.
uint16_t C_BG, C_PANEL, C_PANEL2, C_TEXT, C_MUTED, C_ACCENT, C_ACCENT2;
uint16_t C_WARN, C_DANGER, C_GOOD, C_LINE, C_GRID, C_BUTTON, C_BUTTON2;


// ------------------ FORWARD DECLARATIONS ------------------
// Explicit prototypes avoid Arduino IDE auto-prototype ordering issues.
void handleRoot();
void handleState();
void handleSet();
void handleRun();
void handleSave();
void handleEstop();
void handleRelease();
void handleStatsFile();
void handleLogFile();
void handleLogSnapshot();
void handleSdInfo();
void handleSdFormat();
void handleWifiOff();
void handleWifiReset();
void handleWifiScan();
void handleWifiJoin();
void onWifiEvent(arduino_event_t *event);
bool connectSavedWifi(uint32_t timeoutMs);
String wifiStatusLabel();
String wifiIpLabel();
String wifiModeLabel();
bool webServerRunning();
String shareLink();
String safeJsonString(String s);
void drawThemeModeButton(int x, int y, int w, int h);
void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t text, int textSize);
void drawMilkySideToSprite(M5Canvas& s, int cx, int cy, int spurtLen);
void updateRhythmDemo();
void setRhythmMode(int mode);
const char* rhythmName(int mode);
const char* rhythmDots(int mode);
bool isCustomRhythmMode(int mode);
bool customRhythmHasData(int slot);
bool customRhythmPulseNow(int slot, unsigned long now);
String customNamesJson();
String customDataJson();
void loadCustomRhythms();
void saveCustomRhythmsToPrefs();
bool rhythmPulseFromDots(const char* pattern, int step);
void logButtonPress(const char* label);
void logSystemProblem(const char* problem);
String entryTimeString();
String dateStamp();
String dailyLogDir();
String dailyLogPath();
// v43 explicit forward declarations to avoid Arduino preprocessor/prototype problems in this large .ino.
void allOutputsOff();
void logSdEvent(const char* eventName);
float ppmWaveValue(unsigned long now);
void changeRhythmSpeed(int delta);
void startSetupAp();
void stopSetupAp();
void startWebServer();
void stopWebServerNow();
void refreshWifiScreenNow();
void startBackgroundStaConnect(String ssid, String pass, bool saveOnSuccess);
void handleRhythmApi();
void handleCustomRhythmApi();
void handleFavoriteApi();
void handleLogList();
void handleLogData();

// ------------------ BASIC HELPERS ------------------
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool insideRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

#define hit(btn, tx, ty) insideRect((tx), (ty), (btn).x, (btn).y, (btn).w, (btn).h)
#define drawButtonRect(btn, label, fill, border, text, textSize) drawButton((btn).x, (btn).y, (btn).w, (btn).h, (label), (fill), (border), (text), (textSize))

const BtnRect BTN_TOP_HOME      = {6,   4, 72, 26};
const BtnRect BTN_TOP_STOP      = {242, 4, 72, 26};
const BtnRect BTN_HOME_VAC      = {8,   146, 96, 38};
const BtnRect BTN_HOME_PPM      = {112, 146, 96, 38};
const BtnRect BTN_HOME_MOTOR    = {216, 146, 96, 38};
const BtnRect BTN_HOME_RHYTHM   = {8,   192, 96, 38};
const BtnRect BTN_HOME_WIFI     = {112, 192, 96, 38};
const BtnRect BTN_HOME_RUN      = {216, 192, 96, 38};
const BtnRect BTN_BIG_MINUS     = {19,  178, 132, 53};
const BtnRect BTN_BIG_PLUS      = {169, 178, 132, 53};
const BtnRect BTN_WIFI_WEB      = {14,  150, 292, 38};
const BtnRect BTN_WIFI_SETUP    = {102, 202, 210, 34};
const BtnRect BTN_RHY_PREV      = {8,   146, 70, 36};
const BtnRect BTN_RHY_TOGGLE    = {88,  146, 144, 36};
const BtnRect BTN_RHY_NEXT      = {242, 146, 70, 36};
const BtnRect BTN_RHY_SLOW      = {8,   190, 70, 40};
const BtnRect BTN_RHY_FAST      = {242, 190, 70, 40};


void initPalette() {
  if (darkTheme) {
    C_BG      = M5.Display.color565(12, 16, 24);
    C_PANEL   = M5.Display.color565(24, 31, 44);
    C_PANEL2  = M5.Display.color565(34, 43, 58);
    C_TEXT    = M5.Display.color565(242, 247, 255);
    C_MUTED   = M5.Display.color565(150, 166, 184);
    C_ACCENT  = M5.Display.color565(60, 170, 255);
    C_ACCENT2 = M5.Display.color565(120, 230, 190);
    C_WARN    = M5.Display.color565(255, 188, 75);
    C_DANGER  = M5.Display.color565(230, 38, 48);
    C_GOOD    = M5.Display.color565(42, 210, 125);
    C_LINE    = M5.Display.color565(245, 205, 70);
    C_GRID    = M5.Display.color565(58, 70, 88);
    C_BUTTON  = M5.Display.color565(39, 65, 92);
    C_BUTTON2 = M5.Display.color565(47, 91, 79);
  } else {
    C_BG      = M5.Display.color565(236, 242, 248);
    C_PANEL   = M5.Display.color565(255, 255, 255);
    C_PANEL2  = M5.Display.color565(220, 231, 242);
    C_TEXT    = M5.Display.color565(20, 30, 44);
    C_MUTED   = M5.Display.color565(80, 97, 120);
    C_ACCENT  = M5.Display.color565(10, 114, 190);
    C_ACCENT2 = M5.Display.color565(35, 160, 126);
    C_WARN    = M5.Display.color565(210, 130, 28);
    C_DANGER  = M5.Display.color565(210, 35, 44);
    C_GOOD    = M5.Display.color565(12, 150, 86);
    C_LINE    = M5.Display.color565(190, 112, 0);
    C_GRID    = M5.Display.color565(178, 191, 205);
    C_BUTTON  = M5.Display.color565(205, 226, 245);
    C_BUTTON2 = M5.Display.color565(190, 230, 218);
  }
}

void applyBacklight() {
  backlightStep = clampInt(backlightStep, 1, 10);
  int brightness = map(backlightStep, 1, 10, 35, 255);
  M5.Display.setBrightness(brightness);
}

void setScreen(int s) {
  if (s != currentScreen) {
    previousScreen = currentScreen;
    currentScreen = s;
    uiNeedsFullRedraw = true;
    vacValueDirty = true;
    ppmValueDirty = true;
    motorValueDirty = true;
    rhythmValueDirty = true;
    settingsValueDirty = true;
    liveValueDirty = true;
  }
}

void markSettingsDirty() {
  settingsDirty = true;
  lastSettingsChangeMs = millis();
}

void drawCenteredText(const char* s, int x, int y, int w, int h, int textSize, uint16_t color, uint16_t bg) {
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(color, bg);
  int tw = M5.Display.textWidth(s);
  int th = 8 * textSize;
  M5.Display.setCursor(x + (w - tw) / 2, y + (h - th) / 2);
  M5.Display.print(s);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t border, uint16_t text, int textSize) {
  M5.Display.fillRoundRect(x, y, w, h, 12, fill);
  M5.Display.drawRoundRect(x, y, w, h, 12, border);
  drawCenteredText(label, x, y, w, h, textSize, text, fill);
}

int wifiSignalBars() {
  if (WiFi.status() != WL_CONNECTED) return 0;
  int rssi = WiFi.RSSI();
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  if (rssi >= -85) return 1;
  return 0;
}

void drawWifiEdgeIndicator() {
  // Small always-visible perimeter status. Green bars = LAN, amber dot = trying, blue AP = setup AP, gray X = no radio/web.
  const int x = 219;
  const int y = 6;
  M5.Display.fillRect(x - 3, 2, 22, 30, C_PANEL);
  if (WiFi.status() == WL_CONNECTED) {
    int bars = wifiSignalBars();
    for (int i = 0; i < 4; i++) {
      int bh = 5 + i * 4;
      uint16_t col = (i < bars) ? C_GOOD : C_GRID;
      M5.Display.fillRect(x + i * 4, y + 18 - bh, 3, bh, col);
    }
    return;
  }
  if (wifiStaConnecting) {
    M5.Display.fillCircle(x + 7, y + 8, 5, C_WARN);
    M5.Display.drawCircle(x + 7, y + 8, 8, C_WARN);
    return;
  }
  if (setupApActive) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_ACCENT2, C_PANEL);
    M5.Display.setCursor(x - 1, y + 5);
    M5.Display.print("AP");
    return;
  }
  M5.Display.drawLine(x + 1, y + 3, x + 14, y + 16, C_MUTED);
  M5.Display.drawLine(x + 14, y + 3, x + 1, y + 16, C_MUTED);
}

void drawHeader(const char* title) {
  M5.Display.fillRect(0, 0, 320, 34, C_PANEL);
  bool special = currentScreen == SCREEN_ESTOP_CONFIRM || currentScreen == SCREEN_ESTOP_ACTIVE;
  if (!special && currentScreen != SCREEN_HOME) {
    drawButtonRect(BTN_TOP_HOME, "HOME", C_PANEL2, C_GRID, C_TEXT, 2);
  }
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  int titleX = (currentScreen == SCREEN_HOME || special) ? 9 : 84;
  int titleW = (currentScreen == SCREEN_HOME || special) ? 198 : 126;
  String t = String(title);
  while (M5.Display.textWidth(t) > titleW && t.length() > 3) t.remove(t.length() - 1);
  int tw = M5.Display.textWidth(t);
  M5.Display.setCursor(titleX + (titleW - tw) / 2, 9);
  M5.Display.print(t);
  if (!special) {
    drawWifiEdgeIndicator();
    drawButtonRect(BTN_TOP_STOP, "STOP", C_DANGER, C_DANGER, TFT_WHITE, 2);
  }
}

void drawBackButton() {
  drawButton(4, 207, 72, 29, "BACK", C_PANEL2, C_GRID, C_TEXT, 2);
}

void outputOn(int pin) {
  // Active-low / ground-switched MOSFET input wiring:
  // GPIO pulls the input ground side LOW to turn the channel ON.
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void outputOff(int pin) {
  // OFF is high impedance. Do not drive HIGH into the optocoupler input.
  pinMode(pin, INPUT);
}

void configureOutputPins() {
  outputOff(PIN_SOL_VAC);
  outputOff(PIN_SOL_PULSE);
  outputOff(PIN_SOL_MOTOR);
  outputOff(PIN_UNUSED_OUTPUT);
  allOutputsOff();
}

void applyHardwareOutputs() {
  if (solVacOn) outputOn(PIN_SOL_VAC); else outputOff(PIN_SOL_VAC);
  if (solPulseOn) outputOn(PIN_SOL_PULSE); else outputOff(PIN_SOL_PULSE);
  if (solMotorOn) outputOn(PIN_SOL_MOTOR); else outputOff(PIN_SOL_MOTOR);
  outputOff(PIN_UNUSED_OUTPUT);
}

void allOutputsOff() {
  solVacOn = false;
  solPulseOn = false;
  solMotorOn = false;
  unusedOutputOn = false;
  rhythmMode = 0;
  rhythmStep = -1;
  outputOff(PIN_SOL_VAC);
  outputOff(PIN_SOL_PULSE);
  outputOff(PIN_SOL_MOTOR);
  outputOff(PIN_UNUSED_OUTPUT);
}

int normalizePpmInput(int v) {
  if (v <= 0) return 0;
  if (v < PPM_ACTIVE_MIN) return PPM_ACTIVE_MIN;
  return clampInt(v, PPM_ACTIVE_MIN, PPM_MAX);
}

int activePpmValue() {
  return normalizePpmInput(ppmTarget);
}

int normalizeMotorPower(int v) {
  v = clampInt(v, 0, 100);
  // Motor power is intentionally limited to 10 clear settings: 10%, 20%, ... 100%.
  // 0 remains OFF. This makes tuning safer and easier on the real 12V motor.
  if (v <= 0) return 0;
  int rounded = ((v + 5) / 10) * 10;
  return clampInt(rounded, 10, 100);
}

void stopProgramMode(const char* reason) {
  if (running || activeMode == MODE_PROGRAM) {
    logSdEvent(reason);
  }
  running = false;
  if (activeMode == MODE_PROGRAM) activeMode = MODE_IDLE;
  solVacOn = false;
  solPulseOn = false;
  solMotorOn = false;
  unusedOutputOn = false;
  applyHardwareOutputs();
  liveValueDirty = true;
}

void startProgramMode() {
  if (estopLatched) {
    logSystemProblem("start blocked by estop latch");
    return;
  }
  activeMode = MODE_PROGRAM;
  running = true;
  programStartMs = millis();
  solMotorOn = false;
  unusedOutputOn = false;
  logSdEvent("program start");
  liveValueDirty = true;
}

void updateProgramControl() {
  if (!running || activeMode != MODE_PROGRAM) return;
  unsigned long now = millis();

  // CH1/G5: vacuum solenoid uses the ramped actual value so response time is live.
  int vacuumCommand = clampInt((int)vacActual, 0, 100);
  if (vacuumCommand <= 0) solVacOn = false;
  else if (vacuumCommand >= 100) solVacOn = true;
  else {
    unsigned long phase = now % VACUUM_PWM_WINDOW_MS;
    unsigned long onMs = ((unsigned long)vacuumCommand * VACUUM_PWM_WINDOW_MS) / 100UL;
    solVacOn = phase < onMs;
  }

  // CH2/G6: normal PPM timing, unless a song/rhythm owns CH2.
  if (rhythmMode == 0) {
    int ppm = activePpmValue();
    if (ppm <= 0) solPulseOn = false;
    else {
      unsigned long period = 60000UL / (unsigned long)ppm;
      if (period < 8) period = 8;
      unsigned long onMs = period / 2;
      unsigned long phase = (now - programStartMs) % period;
      solPulseOn = phase < onMs;
    }
  }

  // CH3/G7: 12V motor software PWM.
  int motorCommand = normalizeMotorPower(motorPowerTarget);
  if (motorCommand <= 0) solMotorOn = false;
  else if (motorCommand >= 100) solMotorOn = true;
  else {
    unsigned long phase = now % MOTOR_PWM_WINDOW_MS;
    unsigned long onMs = ((unsigned long)motorCommand * MOTOR_PWM_WINDOW_MS) / 100UL;
    solMotorOn = phase < onMs;
  }

  unusedOutputOn = false;
  applyHardwareOutputs();
}

void drawNetworkStatusScreen(String title, String line1, String line2, String line3) {
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(16, 18);
  M5.Display.print(title);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 50);
  M5.Display.print("Setup uses WiFi AP + web portal only.");
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(16, 84);
  M5.Display.print(line1);
  M5.Display.setCursor(16, 116);
  M5.Display.print(line2);
  M5.Display.setCursor(16, 148);
  M5.Display.print(line3);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 202);
  M5.Display.print("Setup AP: Pluto9000-Setup");
}

bool webServerRunning() {
  return wifiActive && serverPtr != nullptr;
}

String wifiStatusLabel() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
  if (wifiStaConnecting && pendingWifiSsid.length()) return "Trying " + pendingWifiSsid;
  if (setupApActive) return String(SETUP_AP_SSID);
  return String("Local only");
}

String wifiIpLabel() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  if (setupApActive) return setupApIp.toString();
  return String("0.0.0.0");
}

String wifiModeLabel() {
  if (WiFi.status() == WL_CONNECTED && webServerRunning()) return String("LAN web portal");
  if (WiFi.status() == WL_CONNECTED) return String("LAN WiFi connected");
  if (setupApActive && webServerRunning()) return String("Setup AP web portal");
  if (setupApActive) return String("Setup AP only");
  return String("No network");
}

String shareLink() {
  if (WiFi.status() != WL_CONNECTED && !setupApActive) return String("No web IP yet");
  String link = "http://" + wifiIpLabel();
  if (webPort != 80) link += ":" + String(webPort);
  return link;
}

void drawThemeModeButton(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 10, C_BUTTON);
  M5.Display.drawRoundRect(x, y, w, h, 10, C_GRID);
  int cx = x + w / 2;
  int cy = y + h / 2;
  if (darkTheme) {
    // Quarter moon icon only. No text.
    M5.Display.fillCircle(cx, cy, 14, C_TEXT);
    M5.Display.fillCircle(cx + 7, cy - 3, 14, C_BUTTON);
  } else {
    // Sun icon only. No text.
    M5.Display.fillCircle(cx, cy, 11, C_WARN);
    for (int i = 0; i < 8; i++) {
      float ang = i * 0.785398f;
      int x1 = cx + (int)(cosf(ang) * 16);
      int y1 = cy + (int)(sinf(ang) * 16);
      int x2 = cx + (int)(cosf(ang) * 22);
      int y2 = cy + (int)(sinf(ang) * 22);
      M5.Display.drawLine(x1, y1, x2, y2, C_WARN);
    }
  }
}


// ------------------ SD LOGGING / STATS HTML ------------------
bool sdLoggingReady() {
  return sdReady && sdDataReady && sessionLogPath.length() > 0;
}

String uptimeString() {
  unsigned long total = (millis() - bootMs) / 1000;
  unsigned long h = total / 3600;
  unsigned long m = (total % 3600) / 60;
  unsigned long sec = total % 60;
  String out;
  if (h < 10) out += "0";
  out += String(h); out += ":";
  if (m < 10) out += "0";
  out += String(m); out += ":";
  if (sec < 10) out += "0";
  out += String(sec);
  return out;
}

String twoDigits(int v) {
  String out;
  if (v < 10) out += "0";
  out += String(v);
  return out;
}

int monthFromCompile(const char* m) {
  if (!strncmp(m, "Jan", 3)) return 1;
  if (!strncmp(m, "Feb", 3)) return 2;
  if (!strncmp(m, "Mar", 3)) return 3;
  if (!strncmp(m, "Apr", 3)) return 4;
  if (!strncmp(m, "May", 3)) return 5;
  if (!strncmp(m, "Jun", 3)) return 6;
  if (!strncmp(m, "Jul", 3)) return 7;
  if (!strncmp(m, "Aug", 3)) return 8;
  if (!strncmp(m, "Sep", 3)) return 9;
  if (!strncmp(m, "Oct", 3)) return 10;
  if (!strncmp(m, "Nov", 3)) return 11;
  if (!strncmp(m, "Dec", 3)) return 12;
  return 1;
}

String timestampFileStem() {
  // ISO fallback date from compile time. Used until RTC/NTP time is available.
  char mon[4] = {0};
  int day = 1;
  int year = 2026;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  String stem = String(year);
  stem += "-"; stem += twoDigits(monthFromCompile(mon));
  stem += "-"; stem += twoDigits(day);
  return stem;
}

String dateStamp() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 25) && timeinfo.tm_year > 120) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return String(buf);
  }
  return timestampFileStem();
}

String entryTimeString() {
  // HH-MM-SS entry time. For now uptime-derived so it advances while offline.
  unsigned long total = (millis() - bootMs) / 1000;
  unsigned long hh = (total / 3600) % 24;
  unsigned long mm = (total % 3600) / 60;
  unsigned long ss = total % 60;
  return twoDigits(hh) + "-" + twoDigits(mm) + "-" + twoDigits(ss);
}

String dailyLogDir() {
  return "/log/" + dateStamp();
}

String dailyLogPath() {
  String d = dateStamp();
  return "/log/" + d + "/" + d + ".log";
}

bool nameLooksLikeLog(String n) {
  n.replace("/log/", "");
  if (!n.endsWith(".log")) return false;
  return n.indexOf("..") < 0 && n.indexOf('\\') < 0;
}

void ensureStatsHtml() {
  if (!sdReady) return;
  SD.remove("/stats.html");
  File f = SD.open("/stats.html", FILE_WRITE);
  if (!f) return;

  f.println(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  f.println(F("<title>Pluto 9000 Stats</title><style>"));
  f.println(F(":root{font-family:Arial,Helvetica,sans-serif;background:#0d1320;color:#eef5ff}body{margin:0;background:linear-gradient(135deg,#0d1320,#172033 70%,#0b0f18)}.wrap{max-width:1120px;margin:auto;padding:16px}.hero,.card{background:#182235;border:1px solid #2a3a53;border-radius:22px;padding:18px;margin-bottom:14px;box-shadow:0 14px 35px #0006}.hero{display:flex;gap:18px;align-items:center}.brand{font-size:36px;font-weight:900}.sub{color:#aebed3;font-size:16px;line-height:1.45}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.big{font-size:34px;font-weight:900}.label{color:#aebed3;text-transform:uppercase;font-size:12px;letter-spacing:.08em}.note{color:#aebed3;font-size:14px;line-height:1.45}.milky{width:92px;height:130px;position:relative;flex:0 0 auto}.bottle{position:absolute;left:20px;top:25px;width:52px;height:84px;background:#fff;border-radius:14px;border:3px solid #b8d2ea}.neck{position:absolute;left:31px;top:5px;width:30px;height:28px;background:#f3fbff;border-radius:8px;border:3px solid #b8d2ea}.cap{position:absolute;left:29px;top:0;width:34px;height:9px;background:#409be6;border-radius:4px}.face{position:absolute;left:31px;top:48px;font-size:20px;color:#05080c}.tag{position:absolute;left:26px;top:82px;background:#2d8cd2;color:#fff;font-weight:900;border-radius:7px;padding:3px 7px;font-size:12px}.puddle{position:absolute;left:8px;bottom:4px;width:76px;height:14px;background:#fff;border-radius:50%;opacity:.85}.drop{position:absolute;left:44px;top:110px;width:9px;height:16px;background:#fff;border-radius:50% 50% 60% 60%}.mascot2{transform:scaleX(-1)}button,select,input{font-size:16px;border-radius:12px;border:1px solid #34465f;padding:12px;background:#22314a;color:#eef5ff}canvas{width:100%;height:230px;background:#0f1724;border:1px solid #30445f;border-radius:16px}table{width:100%;border-collapse:collapse;background:#182235;border-radius:16px;overflow:hidden}th,td{padding:9px;border-bottom:1px solid #2d405b;text-align:left;font-size:13px}th{background:#22314a}.pill{display:inline-block;border-radius:999px;background:#22314a;color:#aebed3;padding:8px 11px;margin:3px}@media(max-width:650px){.wrap{padding:10px}.hero{display:block}.milky{margin:auto}.brand{font-size:29px}.big{font-size:28px}th,td{font-size:11px;padding:7px}.hideMob{display:none}canvas{height:180px}}"));
  f.println(F("</style></head><body><div class='wrap'><div class='hero'><div class='milky'><div class='neck'></div><div class='cap'></div><div class='bottle'></div><div class='face'>^^</div><div class='tag'>milky</div><div class='drop'></div><div class='puddle'></div></div><div><div class='brand'>Pluto 9000 Stats</div><p class='sub'>This report reads the daily log files stored in <b>/log/YYYY-MM-DD/YYYY-MM-DD.log</b> on the SD card. The daily logs are the source of truth; this page turns the useful parts into readable stats.</p></div><div class='milky mascot2'><div class='neck'></div><div class='cap'></div><div class='bottle'></div><div class='face'>--</div><div class='tag'>milky</div><div class='drop'></div><div class='puddle'></div></div></div>"));
  f.println(F("<div class='card'><div class='label'>Load log</div><select id='logSelect'></select> <button onclick='loadSelected()'>Load</button><p class='note'>Open from the Pluto LAN web portal at <b>/stats.html</b>. If opened directly from the SD card, use the file picker.</p><input type='file' id='filePick' accept='.log,.csv'></div>"));
  f.println(F("<div class='grid'><div class='card'><div class='label'>Samples</div><div class='big' id='sampleCount'>0</div></div><div class='card'><div class='label'>Run time</div><div class='big' id='runTime'>00:00:00</div></div><div class='card'><div class='label'>Avg Vacuum</div><div class='big' id='avgVac'>0%</div></div><div class='card'><div class='label'>Avg PPM</div><div class='big' id='avgPpm'>0</div></div><div class='card'><div class='label'>Avg Motor</div><div class='big' id='avgMotor'>0%</div></div><div class='card'><div class='label'>Max Vacuum</div><div class='big' id='maxVac'>0%</div></div><div class='card'><div class='label'>Events</div><div class='big' id='eventCount'>0</div></div></div>"));
  f.println(F("<div class='card'><span class='pill'>Starts: <b id='starts'>0</b></span><span class='pill'>Stops: <b id='stops'>0</b></span><span class='pill'>Settings changes: <b id='changes'>0</b></span><span class='pill'>Problems: <b id='problems'>0</b></span><span class='pill'>E-stop/release: <b id='estops'>0</b></span></div>"));
  f.println(F("<div class='card'><div class='label'>Vacuum, PPM, and pulse waveform</div><canvas id='chart'></canvas><p class='note'>Green = Vacuum actual. Blue = PPM scaled. Yellow = pulse ripple. Motor is summarized in cards.</p></div><div class='card'><table><thead><tr><th>Time</th><th>Event</th><th>Vacuum target</th><th>Vacuum actual</th><th>PPM</th><th class='hideMob'>Pulse</th><th class='hideMob'>Running</th></tr></thead><tbody id='rows'></tbody></table></div>"));
  f.println(F("<script>let rows=[];function parseCsv(t){let a=t.trim().split(/\\r?\\n/).filter(Boolean);if(!a.length)return[];let h=a.shift().split(',');return a.map(line=>{let p=line.split(',');let o={};h.forEach((k,i)=>o[k]=p[i]);return o})}function hms(sec){sec=Math.max(0,Math.round(sec));let h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60),s=sec%60;return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0')}function draw(){let c=document.getElementById('chart'),r=c.getBoundingClientRect(),d=window.devicePixelRatio||1;c.width=r.width*d;c.height=r.height*d;let ctx=c.getContext('2d');ctx.scale(d,d);ctx.clearRect(0,0,r.width,r.height);ctx.strokeStyle='#2d405b';for(let i=1;i<4;i++){let y=r.height*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(r.width,y);ctx.stroke()}if(!rows.length)return;function line(key,max,color){ctx.strokeStyle=color;ctx.lineWidth=3;ctx.beginPath();rows.forEach((o,i)=>{let x=i/(rows.length-1||1)*r.width;let y=r.height-10-(Number(o[key]||0)/max)*(r.height-22);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.stroke()}line('vacActual',100,'#36d399');line('ppm',400,'#3aa8ff');line('pulseWave',100,'#ffbc4b')}function render(t){rows=parseCsv(t);let samples=rows.filter(o=>o.event&&o.event.includes('running sample'));let av=0,ap=0,am=0,maxv=0,ev=0,starts=0,stops=0,chg=0,prob=0,est=0;rows.forEach(o=>{if(o.event){ev++;let e=o.event.toLowerCase();if(e.includes('run start'))starts++;if(e.includes('run stop'))stops++;if(e.includes('change')||e.includes('button'))chg++;if(e.includes('problem')||e.includes('error'))prob++;if(e.includes('release')||e.includes('estop'))est++;}if(o.running==='1'||(o.event||'').includes('running sample')){av+=Number(o.vacActual||0);ap+=Number(o.ppm||0);am+=Number(o.motorTarget||0);maxv=Math.max(maxv,Number(o.vacActual||0));}});let n=samples.length||rows.filter(o=>o.running==='1').length||1;document.getElementById('sampleCount').textContent=rows.length;document.getElementById('runTime').textContent=hms(samples.length*2.5);document.getElementById('avgVac').textContent=Math.round(av/n)+'%';document.getElementById('avgPpm').textContent=Math.round(ap/n);document.getElementById('avgMotor').textContent=Math.round(am/n)+'%';document.getElementById('maxVac').textContent=Math.round(maxv)+'%';document.getElementById('eventCount').textContent=ev;document.getElementById('starts').textContent=starts;document.getElementById('stops').textContent=stops;document.getElementById('changes').textContent=chg;document.getElementById('problems').textContent=prob;document.getElementById('estops').textContent=est;document.getElementById('rows').innerHTML=rows.slice(-100).reverse().map(o=>`<tr><td>${o.time||o.uptime||''}</td><td>${o.event||''}</td><td>${o.vacTarget||''}%</td><td>${o.vacActual||''}%</td><td>${o.ppm||''}</td><td class='hideMob'>${o.pulseWave||''}</td><td class='hideMob'>${o.running==='1'?'yes':'no'}</td></tr>`).join('');draw()}async function init(){try{let list=await fetch('/api/loglist').then(r=>r.json());let sel=document.getElementById('logSelect');sel.innerHTML=list.files.map(f=>`<option>${f}</option>`).join('');if(list.files[0])loadSelected()}catch(e){document.getElementById('logSelect').innerHTML='<option>Open via device web page</option>'}}async function loadSelected(){let f=document.getElementById('logSelect').value;if(!f)return;let t=await fetch('/api/logdata?file='+encodeURIComponent(f)).then(r=>r.text());render(t)}document.getElementById('filePick').onchange=e=>{let file=e.target.files[0];if(file){let fr=new FileReader();fr.onload=()=>render(fr.result);fr.readAsText(file)}};window.onresize=draw;init();</script></div></body></html>"));
  f.close();
}

void beginSessionLog(bool resetFiles) {
  if (!sdReady) return;
  if (!SD.exists("/log")) SD.mkdir("/log");
  String dir = dailyLogDir();
  if (!SD.exists(dir)) SD.mkdir(dir);
  ensureStatsHtml();
  sessionLogPath = dailyLogPath();
  if (resetFiles && SD.exists(sessionLogPath)) SD.remove(sessionLogPath);

  const char* header = "ms,time,uptime,event,vacTarget,vacActual,ppm,pulseWave,motorTarget,running,theme,backlightStep,heap,solPulse,solVac,solMotor,rhythmMode,rhythmName,customRecording";

  bool newDaily = !SD.exists(sessionLogPath);
  File f = SD.open(sessionLogPath, FILE_WRITE);
  if (f) {
    if (newDaily || resetFiles) f.println(header);
    f.close();
    sdDataReady = true;
  } else {
    sdDataReady = false;
  }
}

void appendCsvLineToPath(const String& path, const char* eventName) {
  File f = SD.open(path, FILE_APPEND);
  if (!f) return;
  f.print(millis()); f.print(',');
  f.print(entryTimeString()); f.print(',');
  f.print(uptimeString()); f.print(',');
  String ev = String(eventName);
  ev.replace(',', ' ');
  f.print(ev); f.print(',');
  f.print(vacTarget); f.print(',');
  f.print((int)vacActual); f.print(',');
  f.print(ppmTarget); f.print(',');
  f.print((int)(ppmWaveValue(millis()) * 100.0f)); f.print(',');
  f.print(motorPowerTarget); f.print(',');
  f.print(running ? 1 : 0); f.print(',');
  f.print(darkTheme ? "dark" : "light"); f.print(',');
  f.print(backlightStep); f.print(',');
  f.print(ESP.getFreeHeap()); f.print(',');
  f.print(solPulseOn ? 1 : 0); f.print(',');
  f.print(solVacOn ? 1 : 0); f.print(',');
  f.print(solMotorOn ? 1 : 0); f.print(',');
  f.print(rhythmMode); f.print(',');
  String rn = String(rhythmName(rhythmMode));
  rn.replace(',', ' ');
  f.print(rn); f.print(',');
  f.println(customRecording ? 1 : 0);
  f.close();
}

void appendCsvLine(const char* eventName) {
  if (!sdLoggingReady()) return;

  String today = dailyLogPath();
  if (today != sessionLogPath) {
    sessionLogPath = today;
    beginSessionLog(false);
  }

  appendCsvLineToPath(sessionLogPath, eventName);
}

void logSdEvent(const char* eventName) {
  if (!sdLoggingReady()) return;
  sdEventCount++;
  lastSdLogMs = millis();
  appendCsvLine(eventName);
}

void logButtonPress(const char* label) {
  String ev = "button ";
  ev += label;
  logSdEvent(ev.c_str());
}

void logSystemProblem(const char* problem) {
  String ev = "problem ";
  ev += problem;
  logSdEvent(ev.c_str());
}

void writePeriodicSdSample() {
  if (!sdLoggingReady()) return;
  if (!running) return;
  unsigned long now = millis();
  if (now - lastSdSampleMs < SD_SAMPLE_MS) return;
  lastSdSampleMs = now;
  appendCsvLine("running sample");
}

String logListJson() {
  String j = "{\"files\":[";
  bool first = true;
  if (sdReady) {
    if (!SD.exists("/log")) SD.mkdir("/log");
    File root = SD.open("/log");
    File dayDir = root.openNextFile();
    while (dayDir) {
      if (dayDir.isDirectory()) {
        String dayName = String(dayDir.name());
        dayName.replace("/log/", "");
        dayName.replace("/", "");
        File file = dayDir.openNextFile();
        while (file) {
          String n = String(file.name());
          if (!file.isDirectory() && n.endsWith(".log")) {
            n.replace("/log/", "");
            if (!first) j += ",";
            j += "\"" + safeJsonString(n) + "\"";
            first = false;
          }
          file.close();
          file = dayDir.openNextFile();
        }
      }
      dayDir.close();
      dayDir = root.openNextFile();
    }
    root.close();
  }
  j += "]}";
  return j;
}

// ------------------ SETTINGS SAVE / LOAD ------------------
// ------------------ CUSTOM RHYTHM STORAGE ------------------
void loadCustomRhythms() {
  for (int i = 0; i < CUSTOM_RHYTHM_SLOTS; i++) {
    String defName = "Custom " + String(i + 1);
    String nk = "custN" + String(i);
    String dk = "custD" + String(i);
    customRhythmName[i] = prefs.getString(nk.c_str(), defName);
    customRhythmData[i] = prefs.getString(dk.c_str(), "");
    if (customRhythmName[i].length() == 0) customRhythmName[i] = defName;
    if (customRhythmData[i].length() > CUSTOM_RHYTHM_MAX_CHARS) customRhythmData[i].remove(CUSTOM_RHYTHM_MAX_CHARS);
  }
}

void saveCustomRhythmsToPrefs() {
  for (int i = 0; i < CUSTOM_RHYTHM_SLOTS; i++) {
    String nk = "custN" + String(i);
    String dk = "custD" + String(i);
    prefs.putString(nk.c_str(), customRhythmName[i]);
    prefs.putString(dk.c_str(), customRhythmData[i]);
  }
}

void saveSettingsNow() {
  prefs.begin("pluto9000", false);
  prefs.putInt("vac", vacTarget);
  prefs.putInt("ppm", ppmTarget);
  prefs.putBool("dark", darkTheme);
  prefs.putInt("bl", backlightStep);
  prefs.putInt("ramp", vacRampSpeed);
  prefs.putInt("motor", normalizeMotorPower(motorPowerTarget));
  prefs.putInt("gspd", graphSpeed);
  prefs.putInt("rel", releaseMs);
  prefs.putBool("large", largeButtons);
  prefs.putBool("web", webControlEnabled);
  prefs.putBool("mobile", mobileMode);
  prefs.putBool("simple", simpleHome);
  prefs.putBool("advweb", showAdvancedWeb);
  prefs.putInt("webref", webRefreshSec);
  prefs.putInt("webPort", webPort);
  prefs.putInt("rhySpd", rhythmSpeedPct);
  prefs.putInt("favR1", favoriteRhythm1);
  prefs.putInt("favR2", favoriteRhythm2);
  prefs.putInt("favR3", favoriteRhythm3);
  saveCustomRhythmsToPrefs();
  prefs.putString("staSsid", staSsid);
  prefs.putString("staPass", staPass);
  prefs.putBool("shVac", showVac);
  prefs.putBool("shPpm", showPpm);
  prefs.putBool("shLive", showLive);
  prefs.putBool("shSet", showSettings);
  prefs.putBool("shWifi", showWifi);
  prefs.putInt("scale", screenScale);
  prefs.end();

  if (sdLoggingReady()) {
    SD.remove("/pluto9000.cfg");
    File f = SD.open("/pluto9000.cfg", FILE_WRITE);
    if (f) {
      f.println("Pluto9000 configuration backup");
      f.print("vac="); f.println(vacTarget);
      f.print("ppm="); f.println(ppmTarget);
      f.print("dark="); f.println(darkTheme ? 1 : 0);
      f.print("backlightStep="); f.println(backlightStep);
      f.print("vacRampSpeed="); f.println(vacRampSpeed);
      f.print("motorPowerTarget="); f.println(motorPowerTarget);
      f.print("graphSpeed="); f.println(graphSpeed);
      f.print("releaseMs="); f.println(releaseMs);
      f.print("largeButtons="); f.println(largeButtons ? 1 : 0);
      f.print("webControlEnabled="); f.println(webControlEnabled ? 1 : 0);
      f.print("mobileMode="); f.println(mobileMode ? 1 : 0);
      f.print("simpleHome="); f.println(simpleHome ? 1 : 0);
      f.print("showAdvancedWeb="); f.println(showAdvancedWeb ? 1 : 0);
      f.print("webRefreshSec="); f.println(webRefreshSec);
      f.print("webPort="); f.println(webPort);
      f.print("rhythmSpeedPct="); f.println(rhythmSpeedPct);
      f.print("favoriteRhythm1="); f.println(favoriteRhythm1);
      f.print("favoriteRhythm2="); f.println(favoriteRhythm2);
      f.print("favoriteRhythm3="); f.println(favoriteRhythm3);
      for (int i = 0; i < CUSTOM_RHYTHM_SLOTS; i++) {
        f.print("customName"); f.print(i + 1); f.print("="); f.println(customRhythmName[i]);
        f.print("customData"); f.print(i + 1); f.print("="); f.println(customRhythmData[i]);
      }
      f.print("showVac="); f.println(showVac ? 1 : 0);
      f.print("showPpm="); f.println(showPpm ? 1 : 0);
      f.print("showLive="); f.println(showLive ? 1 : 0);
      f.print("showSettings="); f.println(showSettings ? 1 : 0);
      f.print("showWifi="); f.println(showWifi ? 1 : 0);
      f.print("screenScale="); f.println(screenScale);
      f.close();
    }
  }

  logSdEvent("settings saved");
  settingsDirty = false;
}

void flushSettingsIfIdle() {
  if (settingsDirty && millis() - lastSettingsChangeMs >= SAVE_IDLE_MS) {
    saveSettingsNow();
  }
}

void loadSettings() {
  prefs.begin("pluto9000", true);
  vacTarget = prefs.getInt("vac", 0);
  ppmTarget = prefs.getInt("ppm", PPM_DEFAULT);
  darkTheme = prefs.getBool("dark", true);
  backlightStep = prefs.getInt("bl", 7);
  vacRampSpeed = prefs.getInt("ramp", 5);
  motorPowerTarget = normalizeMotorPower(prefs.getInt("motor", 0));
  graphSpeed = prefs.getInt("gspd", 4);
  releaseMs = prefs.getInt("rel", 1200);
  largeButtons = prefs.getBool("large", true);
  webControlEnabled = prefs.getBool("web", true);
  mobileMode = prefs.getBool("mobile", true);
  simpleHome = prefs.getBool("simple", false);
  showAdvancedWeb = prefs.getBool("advweb", true);
  webRefreshSec = prefs.getInt("webref", 1);
  webPort = prefs.getInt("webPort", 80);
  rhythmSpeedPct = prefs.getInt("rhySpd", 100);
  favoriteRhythm1 = prefs.getInt("favR1", 3);
  favoriteRhythm2 = prefs.getInt("favR2", 7);
  favoriteRhythm3 = prefs.getInt("favR3", 13);
  loadCustomRhythms();
  staSsid = prefs.getString("staSsid", "");
  staPass = prefs.getString("staPass", "");
  showVac = prefs.getBool("shVac", true);
  showPpm = prefs.getBool("shPpm", true);
  showLive = prefs.getBool("shLive", true);
  showSettings = prefs.getBool("shSet", true);
  showWifi = prefs.getBool("shWifi", true);
  screenScale = prefs.getInt("scale", 2);
  prefs.end();

  vacTarget = clampInt(vacTarget, 0, 100);
  ppmTarget = normalizePpmInput(ppmTarget);
  backlightStep = clampInt(backlightStep, 1, 10);
  vacRampSpeed = clampInt(vacRampSpeed, 1, 10);
  motorPowerTarget = normalizeMotorPower(motorPowerTarget);
  graphSpeed = clampInt(graphSpeed, 1, 10);
  webRefreshSec = clampInt(webRefreshSec, 1, 5);
  webPort = clampInt(webPort, 80, 65535);
  screenScale = clampInt(screenScale, 1, 3);
  releaseMs = clampInt(releaseMs, 250, 5000);
  rhythmSpeedPct = clampInt(rhythmSpeedPct, 25, 300);
  favoriteRhythm1 = clampInt(favoriteRhythm1, 1, RHYTHM_MODE_COUNT - 1);
  favoriteRhythm2 = clampInt(favoriteRhythm2, 1, RHYTHM_MODE_COUNT - 1);
  favoriteRhythm3 = clampInt(favoriteRhythm3, 1, RHYTHM_MODE_COUNT - 1);
  vacActual = 0.0f;
}

// ------------------ BOOT BRANDING ------------------
void drawMilkyToSprite(M5Canvas& s, int cx, int cy, int eyeDir) {
  // Bottle body
  s.fillRoundRect(cx - 30, cy - 44, 60, 86, 14, TFT_WHITE);
  s.drawRoundRect(cx - 30, cy - 44, 60, 86, 14, M5.Display.color565(180, 205, 230));
  s.fillRoundRect(cx - 18, cy - 66, 36, 28, 8, M5.Display.color565(235, 245, 255));
  s.drawRoundRect(cx - 18, cy - 66, 36, 28, 8, M5.Display.color565(180, 205, 230));
  s.fillRect(cx - 15, cy - 73, 30, 10, M5.Display.color565(65, 155, 230));

  // Label stays attached to the bottle. It does not draw early while Milky is still off-screen.
  if (cx > 34 && cx < 286) {
    s.fillRoundRect(cx - 24, cy + 3, 48, 23, 7, M5.Display.color565(45, 140, 210));
    s.setTextSize(2);
    s.setTextColor(TFT_WHITE, M5.Display.color565(45, 140, 210));
    int labelW = s.textWidth("milky");
    s.setCursor(cx - labelW / 2, cy + 7);
    s.print("milky");
  }

  // Eyes
  s.fillCircle(cx - 12, cy - 22, 8, TFT_BLACK);
  s.fillCircle(cx + 12, cy - 22, 8, TFT_BLACK);
  s.fillCircle(cx - 12 + eyeDir, cy - 22, 3, TFT_WHITE);
  s.fillCircle(cx + 12 + eyeDir, cy - 22, 3, TFT_WHITE);
  s.drawArc(cx, cy - 8, 12, 9, 20, 160, TFT_BLACK);
}

void drawMilkySideToSprite(M5Canvas& s, int cx, int cy, int spurtLen) {
  // Upright, turned to run right. He does NOT lay down.
  uint16_t blue = M5.Display.color565(45, 140, 210);
  s.fillRoundRect(cx - 25, cy - 44, 54, 86, 14, TFT_WHITE);
  s.drawRoundRect(cx - 25, cy - 44, 54, 86, 14, M5.Display.color565(180, 205, 230));
  s.fillRoundRect(cx - 14, cy - 66, 33, 28, 8, M5.Display.color565(235, 245, 255));
  s.drawRoundRect(cx - 14, cy - 66, 33, 28, 8, M5.Display.color565(180, 205, 230));
  s.fillRect(cx - 11, cy - 73, 28, 10, M5.Display.color565(65, 155, 230));
  // Keep the chest label attached to the body only while the body is on-screen.
  // This prevents the word "milky" from wrapping back across the screen after Milky exits.
  if (cx > 34 && cx < 286) {
    s.fillRoundRect(cx - 20, cy + 3, 45, 23, 7, blue);
    s.setTextSize(2);
    s.setTextColor(TFT_WHITE, blue);
    int labelW = s.textWidth("milky");
    s.setCursor(cx - labelW / 2 + 2, cy + 7);
    s.print("milky");
  }
  // Face looking right
  s.fillCircle(cx + 6, cy - 23, 7, TFT_BLACK);
  s.fillCircle(cx + 19, cy - 20, 6, TFT_BLACK);
  s.fillCircle(cx + 9, cy - 23, 3, TFT_WHITE);
  s.fillCircle(cx + 21, cy - 20, 2, TFT_WHITE);
  s.drawArc(cx + 13, cy - 6, 10, 7, 20, 140, TFT_BLACK);
  // Legs angled like he is about to run right
  s.drawLine(cx - 8, cy + 42, cx - 20, cy + 66, TFT_BLACK);
  s.drawLine(cx + 14, cy + 42, cx + 30, cy + 66, TFT_BLACK);
  s.fillRoundRect(cx - 28, cy + 62, 22, 8, 4, TFT_BLACK);
  s.fillRoundRect(cx + 21, cy + 62, 24, 8, 4, TFT_BLACK);
  if (spurtLen > 0) {
    int sx = cx + 30;
    int sy = cy + 2;   // halfway down the sprite, shooting right
    s.drawLine(sx, sy, sx + spurtLen, sy - 2, TFT_WHITE);
    s.drawLine(sx, sy + 3, sx + spurtLen - 10, sy + 7, TFT_WHITE);
    s.fillCircle(sx + spurtLen, sy - 2, 4, TFT_WHITE);
    s.fillCircle(sx + spurtLen - 10, sy + 7, 3, TFT_WHITE);
  }
}

void drawPlutoDropFrame(int plutoY, int eyeDir) {
  bootSprite.fillScreen(C_BG);
  drawMilkyToSprite(bootSprite, 160, 132, eyeDir);

  bootSprite.setTextSize(4);
  bootSprite.setTextColor(C_ACCENT2, C_BG);
  int tw = bootSprite.textWidth("PLUTO");
  bootSprite.setCursor((320 - tw) / 2, plutoY);
  bootSprite.print("PLUTO");

  bootSprite.setTextSize(3);
  bootSprite.setTextColor(C_TEXT, C_BG);
  tw = bootSprite.textWidth("9000");
  bootSprite.setCursor((320 - tw) / 2, plutoY + 36);
  bootSprite.print("9000");

  bootSprite.pushSprite(0, 0);
}

void bootAnimation() {
  bootSprite.setColorDepth(16);
  bootSprite.createSprite(320, 240);

  // About 3 seconds total. Milky stays upright, then turns right, spurts, and runs off.
  for (int x = -60; x <= 160; x += 20) {
    bootSprite.fillScreen(C_BG);
    drawMilkyToSprite(bootSprite, x, 132, 0);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(36 / portTICK_PERIOD_MS);
  }

  for (int i = 0; i < 20; i++) {
    int eyeDir = 0;
    if (i >= 4 && i < 8) eyeDir = 4;
    else if (i >= 8 && i < 13) eyeDir = -4;
    else if (i >= 13) eyeDir = 4;
    bootSprite.fillScreen(C_BG);
    drawMilkyToSprite(bootSprite, 160, 132, eyeDir);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(38 / portTICK_PERIOD_MS);
  }

  for (int i = 0; i < 14; i++) {
    int spurtLen = (i % 2 == 0) ? (28 + (i * 5)) : 0;
    bootSprite.fillScreen(C_BG);
    drawMilkySideToSprite(bootSprite, 156, 132, spurtLen);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(58 / portTICK_PERIOD_MS);
  }

  for (int x = 160; x < 390; x += 24) {
    bootSprite.fillScreen(C_BG);
    drawMilkySideToSprite(bootSprite, x, 132, 0);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(48 / portTICK_PERIOD_MS);
  }

  bootSprite.fillScreen(C_BG);
  bootSprite.pushSprite(0, 0);
  vTaskDelay(20 / portTICK_PERIOD_MS);
  bootSprite.deleteSprite();
  M5.Display.fillScreen(C_BG);
}

void pulseCoreSplash() {
  M5.Display.fillScreen(C_BG);
  M5.Display.fillCircle(160, 84, 38, C_ACCENT);
  M5.Display.fillCircle(160, 84, 22, C_BG);
  M5.Display.fillRect(156, 43, 8, 82, C_ACCENT2);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(C_TEXT, C_BG);
  int tw = M5.Display.textWidth("PulseCore");
  M5.Display.setCursor((320 - tw) / 2, 142);
  M5.Display.print("PulseCore");
  vTaskDelay(5000 / portTICK_PERIOD_MS);
}

// ------------------ GRAPHS AND LIVE PHYSICS ------------------
void updateVacPhysics() {
  unsigned long now = millis();
  if (now - lastVacPhysicsMs < 35) return;
  lastVacPhysicsMs = now;

  float response = 0.004f + (float)vacRampSpeed * 0.0025f;
  float commandedVacuum = (running && activeMode == MODE_PROGRAM) ? (float)vacTarget : 0.0f;
  vacActual += (commandedVacuum - vacActual) * response;
  if (fabsf(vacActual - commandedVacuum) < 0.12f) vacActual = commandedVacuum;
}

float ppmWaveValue(unsigned long ms) {
  if (ppmTarget <= 0) return 0.0f;
  float periodMs = 60000.0f / (float)ppmTarget;     // one full up/down cycle per pulse
  float phase = fmod((float)ms, periodMs) / periodMs;
  // Smooth hump. Frequency changes with PPM, height stays consistent.
  float v = sinf(phase * 2.0f * PI);
  if (v < 0) v = 0;
  return v;
}

void drawGraphFrame(int x, int y, int w, int h, const char* title, bool showYAxis) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.setCursor(x + 8, y + 5);
  M5.Display.print(title);

  if (showYAxis) {
    M5.Display.setCursor(x + 6, y + 20);
    M5.Display.print("100");
    M5.Display.setCursor(x + 10, y + h - 15);
    M5.Display.print("0");
    M5.Display.drawLine(x + 29, y + 18, x + 29, y + h - 12, C_GRID);
  }
}

void updateVacGraph() {
  int x = 12, y = 92, w = 296, h = 78;
  int gx = x + 32, gy = y + 18, gw = w - 42, gh = h - 30;
  M5.Display.fillRect(gx, gy, gw, gh, C_PANEL);

  for (int i = 0; i <= 4; i++) {
    int yy = gy + (gh * i) / 4;
    M5.Display.drawLine(gx, yy, gx + gw, yy, C_GRID);
  }

  int targetY = gy + gh - map(vacTarget, 0, 100, 0, gh);
  M5.Display.drawLine(gx, targetY, gx + gw, targetY, C_WARN);

  int lastX = gx;
  int lastY = gy + gh - map((int)vacActual, 0, 100, 0, gh);
  for (int px = 1; px < gw; px++) {
    float ripple = sinf((millis() * 0.0018f) + px * 0.10f) * 2.0f;
    int vv = clampInt((int)(vacActual + ripple), 0, 100);
    int yy = gy + gh - map(vv, 0, 100, 0, gh);
    M5.Display.drawLine(lastX, lastY, gx + px, yy, C_ACCENT2);
    lastX = gx + px;
    lastY = yy;
  }
}

void updatePpmGraph() {
  int x = 12, y = 92, w = 296, h = 78;
  int gx = x + 10, gy = y + 18, gw = w - 20, gh = h - 30;
  M5.Display.fillRect(gx, gy, gw, gh, C_PANEL);

  for (int i = 0; i <= 3; i++) {
    int yy = gy + (gh * i) / 3;
    M5.Display.drawLine(gx, yy, gx + gw, yy, C_GRID);
  }

  int ppm = activePpmValue();
  float beat = ppmWaveValue(millis());
  int cx = gx + gw / 2;
  int cy = gy + gh / 2;
  uint16_t pulseColor = ppm > 0 ? C_ACCENT : C_GRID;
  M5.Display.drawCircle(cx, cy, 18 + (int)(beat * 15.0f), C_GRID);
  M5.Display.drawCircle(cx, cy, 8 + (int)(beat * 12.0f), pulseColor);
  M5.Display.fillCircle(cx, cy, 5 + (int)(beat * 6.0f), pulseColor);

  int barW = map(ppm, 0, PPM_MAX, 0, gw - 20);
  M5.Display.fillRoundRect(gx + 10, gy + gh - 10, gw - 20, 7, 4, C_PANEL2);
  M5.Display.fillRoundRect(gx + 10, gy + gh - 10, barW, 7, 4, pulseColor);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(gx + 8, gy + 5);
  M5.Display.print(ppm > 0 ? "LIVE PULSE" : "PULSE OFF");
}

void drawHorizontalBar(int x, int y, int w, int h, int value, int maxValue, const char* label, uint16_t barColor) {
  value = clampInt(value, 0, maxValue);
  M5.Display.fillRoundRect(x, y, w, h, 8, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_GRID);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(x + 8, y + 9);
  M5.Display.print(label);
  int barX = x + 8;
  int barY = y + 38;
  int barW = w - 16;
  int barH = h - 48;
  M5.Display.fillRoundRect(barX, barY, barW, barH, 6, C_PANEL2);
  int fillW = map(value, 0, maxValue, 0, barW);
  M5.Display.fillRoundRect(barX, barY, fillW, barH, 6, barColor);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(x + w - 68, y + 9);
  M5.Display.print(value);
}

void addLivePoint() {
  liveVacHist[liveHistIndex] = clampInt((int)vacActual, 0, 100);
  livePpmHist[liveHistIndex] = clampInt(ppmTarget, 0, PPM_MAX);
  liveMotorHist[liveHistIndex] = clampInt(motorPowerTarget, 0, 100);
  liveHistIndex++;
  if (liveHistIndex >= LIVE_POINTS) liveHistIndex = 0;
}

void drawLiveGraphFrame() {
  M5.Display.fillRoundRect(8, 42, 304, 160, 8, C_PANEL);
  M5.Display.drawRoundRect(8, 42, 304, 160, 8, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.setCursor(16, 49);
  M5.Display.print("Live: Vacuum + Motor + PPM ripple");
  M5.Display.setCursor(18, 67);
  M5.Display.print("Vacuum 0-100%");
  M5.Display.setCursor(210, 67);
  M5.Display.print("PPM 0/30-400");
}

void updateLiveGraphs() {
  int x = 16, y = 82, w = 288, h = 110;
  M5.Display.fillRect(x, y, w, h, C_PANEL);
  for (int i = 0; i <= 4; i++) {
    int yy = y + (h * i) / 4;
    M5.Display.drawLine(x, yy, x + w, yy, C_GRID);
  }
  int lastVacX = x;
  int lastVacY = y + h - map(liveVacHist[liveHistIndex], 0, 100, 0, h);
  int lastPpmX = x;
  int lastPpmY = y + h - map(livePpmHist[liveHistIndex], 0, PPM_MAX, 0, h);
  int lastMotorX = x;
  int lastMotorY = y + h - map(liveMotorHist[liveHistIndex], 0, 100, 0, h);
  for (int i = 1; i < LIVE_POINTS; i++) {
    int idx = liveHistIndex + i;
    if (idx >= LIVE_POINTS) idx -= LIVE_POINTS;
    int px = x + map(i, 0, LIVE_POINTS - 1, 0, w);
    int vy = y + h - map(liveVacHist[idx], 0, 100, 0, h);
    int py = y + h - map(livePpmHist[idx], 0, PPM_MAX, 0, h);
    int my = y + h - map(liveMotorHist[idx], 0, 100, 0, h);
    M5.Display.drawLine(lastVacX, lastVacY, px, vy, C_ACCENT2);
    // PPM is displayed as a ripple indicator below, not a fast line.
    M5.Display.drawLine(lastMotorX, lastMotorY, px, my, C_WARN);
    lastVacX = px; lastVacY = vy;
    lastPpmX = px; lastPpmY = py;
    lastMotorX = px; lastMotorY = my;
  }
  // Readable PPM display: real numbers plus ripple instead of a frantic line graph.
  int ppm = activePpmValue();
  float beat = ppmWaveValue(millis());
  int bx = x + w - 64;
  int by = y + 23;
  M5.Display.fillRoundRect(bx - 4, by - 8, 60, 46, 8, C_PANEL2);
  M5.Display.drawCircle(bx + 16, by + 14, 9 + (int)(beat * 8), ppm > 0 ? C_ACCENT : C_GRID);
  M5.Display.fillCircle(bx + 16, by + 14, 4 + (int)(beat * 5), ppm > 0 ? C_ACCENT : C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(bx + 34, by + 3);
  M5.Display.print(ppm);
  M5.Display.setCursor(bx + 34, by + 17);
  M5.Display.print("PPM");
  M5.Display.fillRoundRect(12, 205, 72, 31, 7, C_PANEL2);
  drawCenteredText("BACK", 12, 205, 72, 31, 2, C_TEXT, C_PANEL2);
  M5.Display.fillRoundRect(220, 205, 88, 31, 7, running ? C_WARN : C_GOOD);
  drawCenteredText(running ? "STOP" : "START", 220, 205, 88, 31, 2, TFT_BLACK, running ? C_WARN : C_GOOD);
}

// ------------------ SCREEN DRAWING ------------------
void refreshVacValue() {
  M5.Display.fillRoundRect(12, 40, 296, 42, 8, C_PANEL2);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(22, 49);
  M5.Display.print("VAC ");
  M5.Display.print(vacTarget);
  M5.Display.print("%");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL2);
  M5.Display.setCursor(210, 48);
  M5.Display.print("actual ");
  M5.Display.print((int)vacActual);
  M5.Display.print("%");
  vacValueDirty = false;
}

void refreshPpmValue() {
  M5.Display.fillRoundRect(12, 40, 296, 42, 8, C_PANEL2);
  int ppm = activePpmValue();
  float pps = ppm > 0 ? (float)ppm / 60.0f : 0.0f;
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(22, 49);
  M5.Display.print("PPM ");
  M5.Display.print(ppm);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL2);
  M5.Display.setCursor(218, 48);
  M5.Display.print(pps, 2);
  M5.Display.print(" p/s");
  M5.Display.setCursor(218, 62);
  if (ppm > 0) {
    M5.Display.print((int)(60000.0f / (float)ppm));
    M5.Display.print(" ms");
  } else {
    M5.Display.print("off");
  }
  ppmValueDirty = false;
}


void drawMiniChannelCard(int x, int y, int w, int h, const char* label, int value, int maxValue, uint16_t color, bool pulse) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_GRID);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(x + 8, y + 7);
  M5.Display.print(label);
  M5.Display.setCursor(x + 102, y + 7);
  M5.Display.print(value);
  if (strcmp(label, "PPM") != 0) M5.Display.print("%");
  int bx = x + 8;
  int by = y + h - 12;
  int bw = w - 55;
  M5.Display.fillRoundRect(bx, by, bw, 7, 4, C_PANEL2);
  int fillW = map(clampInt(value, 0, maxValue), 0, maxValue, 0, bw);
  M5.Display.fillRoundRect(bx, by, fillW, 7, 4, color);
  if (pulse) {
    bool rhythmPulse = rhythmMode > 0 && running;
    float beat = rhythmPulse ? (solPulseOn ? 1.0f : 0.0f) : ppmWaveValue(millis());
    int cx = x + w - 24;
    int cy = y + h / 2;
    uint16_t pc = (value > 0 || rhythmPulse) ? color : C_GRID;
    M5.Display.drawCircle(cx, cy, 7 + (int)(beat * 10), pc);
    M5.Display.fillCircle(cx, cy, 4 + (int)(beat * 5), pc);
  }
}

void drawHomeVisuals() {
  drawMiniChannelCard(8, 40, 304, 30, "VAC", vacTarget, 100, C_ACCENT2, false);
  drawMiniChannelCard(8, 75, 304, 30, "PPM", activePpmValue(), PPM_MAX, C_ACCENT, true);
  drawMiniChannelCard(8, 110, 304, 30, "MOTOR", motorPowerTarget, 100, C_WARN, false);
}

void updateMotorVisual() {
  M5.Display.fillRoundRect(12, 92, 296, 78, 8, C_PANEL);
  M5.Display.drawRoundRect(12, 92, 296, 78, 8, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.setCursor(22, 100);
  M5.Display.print("CH3 / G7 motor command");
  int bx = 24, by = 130, bw = 272, bh = 24;
  M5.Display.fillRoundRect(bx, by, bw, bh, 8, C_PANEL2);
  int fillW = map(motorPowerTarget, 0, 100, 0, bw);
  M5.Display.fillRoundRect(bx, by, fillW, bh, 8, C_WARN);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(24, 158);
  M5.Display.print("0  25  50  75  100");
  motorValueDirty = false;
}

void refreshMotorValue() {
  M5.Display.fillRoundRect(12, 40, 296, 42, 8, C_PANEL2);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(22, 49);
  M5.Display.print("MOTOR ");
  M5.Display.print(motorPowerTarget);
  M5.Display.print("%");
  motorValueDirty = false;
}

void updateRhythmVisual() {
  M5.Display.fillRoundRect(12, 82, 296, 56, 8, C_PANEL);
  M5.Display.drawRoundRect(12, 82, 296, 56, 8, C_GRID);
  int ppm = activePpmValue();
  bool on = solPulseOn && running && rhythmMode > 0;
  float beat = on ? 1.0f : ppmWaveValue(millis());
  uint16_t pulseColor = (rhythmMode > 0 && running) ? C_ACCENT : C_GRID;
  int cx = 48;
  int cy = 110;
  M5.Display.drawCircle(cx, cy, 13 + (int)(beat * 8), pulseColor);
  M5.Display.fillCircle(cx, cy, 6 + (int)(beat * 5), pulseColor);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(78, 88);
  M5.Display.print(rhythmName(rhythmMode));
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_WARN, C_PANEL);
  M5.Display.setCursor(78, 110);
  M5.Display.print(rhythmDots(rhythmMode));
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.setCursor(78, 123);
  M5.Display.print("CH2 only · ");
  M5.Display.print(rhythmSpeedPct);
  M5.Display.print("% · ");
  M5.Display.print(running ? "running" : "stopped");
  rhythmValueDirty = false;
}

void drawHomeScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Pluto 9000");
  drawHomeVisuals();
  drawButtonRect(BTN_HOME_VAC, "VAC", C_BUTTON2, C_ACCENT2, C_TEXT, 2);
  drawButtonRect(BTN_HOME_PPM, "PPM", C_BUTTON, C_ACCENT, C_TEXT, 2);
  drawButtonRect(BTN_HOME_MOTOR, "MOTOR", C_BUTTON, C_WARN, C_TEXT, 2);
  drawButtonRect(BTN_HOME_RHYTHM, "RHYTHM", C_PANEL2, C_GRID, C_TEXT, 2);
  drawButtonRect(BTN_HOME_WIFI, "WIFI", C_PANEL2, C_GRID, C_TEXT, 2);
  drawButtonRect(BTN_HOME_RUN, running ? "STOP" : "START", running ? C_WARN : C_GOOD, running ? C_WARN : C_GOOD, TFT_BLACK, 2);
}

void drawVacScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Vacuum");
  refreshVacValue();
  drawGraphFrame(12, 92, 296, 78, "Actual vacuum ramp", true);
  updateVacGraph();
  drawButtonRect(BTN_BIG_MINUS, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButtonRect(BTN_BIG_PLUS, "+", C_BUTTON2, C_ACCENT2, C_TEXT, 5);
}

void drawPpmScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("PPM Pulse");
  refreshPpmValue();
  drawGraphFrame(12, 92, 296, 78, "Live CH2 pulse rhythm", false);
  updatePpmGraph();
  drawButtonRect(BTN_BIG_MINUS, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButtonRect(BTN_BIG_PLUS, "+", C_BUTTON2, C_ACCENT, C_TEXT, 5);
}


void drawMotorScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Motor");
  refreshMotorValue();
  updateMotorVisual();
  drawButtonRect(BTN_BIG_MINUS, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButtonRect(BTN_BIG_PLUS, "+", C_BUTTON2, C_WARN, C_TEXT, 5);
}

void drawRhythmScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Rhythms");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(18, 44);
  M5.Display.print("Songs/rhythms drive CH2 only. VAC and MOTOR stay live.");
  updateRhythmVisual();
  drawButtonRect(BTN_RHY_PREV, "PREV", C_BUTTON, C_GRID, C_TEXT, 2);
  drawButtonRect(BTN_RHY_TOGGLE, rhythmMode > 0 ? "STOP RHYTHM" : "START RHYTHM", rhythmMode > 0 ? C_WARN : C_GOOD, rhythmMode > 0 ? C_WARN : C_GOOD, TFT_BLACK, 2);
  drawButtonRect(BTN_RHY_NEXT, "NEXT", C_BUTTON2, C_ACCENT2, C_TEXT, 2);
  drawButtonRect(BTN_RHY_SLOW, "SLOW", C_BUTTON, C_GRID, C_TEXT, 2);
  drawCenteredText("SPEED", 88, 190, 144, 16, 1, C_MUTED, C_BG);
  char speedBuf[24];
  snprintf(speedBuf, sizeof(speedBuf), "%d%%", rhythmSpeedPct);
  drawCenteredText(speedBuf, 88, 205, 144, 22, 2, C_TEXT, C_BG);
  drawButtonRect(BTN_RHY_FAST, "FAST", C_BUTTON2, C_ACCENT2, C_TEXT, 2);
}

void drawLiveScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Live");
  if (!running) {
    drawCenteredText("PRESS START", 0, 76, 320, 40, 3, C_TEXT, C_BG);
    drawCenteredText("to view live graphs", 0, 124, 320, 28, 2, C_MUTED, C_BG);
    drawButton(12, 205, 72, 31, "BACK", C_PANEL2, C_GRID, C_TEXT, 2);
    drawButton(220, 205, 88, 31, "START", C_GOOD, C_GOOD, TFT_BLACK, 2);
    return;
  }
  drawLiveGraphFrame();
  updateLiveGraphs();
}

void drawMoreScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("More");
  int y1 = 50;
  if (showLive) drawButton(10, y1, 145, 54, "LIVE", C_BUTTON, C_GRID, C_TEXT, 3);
  if (showSettings) drawButton(10, 114, 145, 54, "SET", C_PANEL2, C_GRID, C_TEXT, 3);
  drawButton(165, 114, 145, 54, "WIFI", C_PANEL2, C_GRID, C_TEXT, 3);
  drawBackButton();
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_MUTED, C_BG);
  int tw = M5.Display.textWidth(APP_VERSION);
  M5.Display.setCursor(314 - tw, 221);
  M5.Display.print(APP_VERSION);
}

void drawSettingsScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Set");

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(14, 46);
  M5.Display.print("Theme");
  drawThemeModeButton(182, 39, 124, 40);

  M5.Display.setTextSize(2);
  M5.Display.setCursor(14, 100);
  M5.Display.print("Backlight  ");
  M5.Display.print(backlightStep);
  M5.Display.print("/10");
  drawButton(20, 146, 120, 50, "-", C_BUTTON, C_GRID, C_TEXT, 4);
  drawButton(180, 146, 120, 50, "+", C_BUTTON2, C_ACCENT2, C_TEXT, 4);
  drawBackButton();
}

void drawWifiScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("WiFi");
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(12, 43);
  M5.Display.print("SSID:");
  M5.Display.setTextColor(C_ACCENT2, C_BG);
  M5.Display.setCursor(78, 43);
  String ss = wifiStatusLabel();
  if (ss.length() > 18) ss = ss.substring(0, 18);
  M5.Display.print(ss);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(12, 74);
  M5.Display.print("IP:");
  M5.Display.setTextColor(C_ACCENT, C_BG);
  M5.Display.setCursor(52, 74);
  M5.Display.print(wifiIpLabel());
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(12, 104);
  M5.Display.print("Mode:");
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(76, 104);
  String mode = wifiModeLabel();
  if (mode.length() > 20) mode = mode.substring(0, 20);
  M5.Display.print(mode);
  if (WiFi.status() == WL_CONNECTED) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_GOOD, C_BG);
    M5.Display.setCursor(238, 108);
    M5.Display.print(String(WiFi.RSSI()) + "dBm");
  }
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 128);
  String msg = wifiLastMessage;
  if (msg.length() > 44) msg = msg.substring(0, 44);
  M5.Display.print(msg);
  drawButtonRect(BTN_WIFI_WEB, webServerRunning() ? "WEB SERVER OFF" : "WEB SERVER ON", webServerRunning() ? C_WARN : C_GOOD, webServerRunning() ? C_WARN : C_GOOD, TFT_BLACK, 2);
  drawButtonRect(BTN_WIFI_SETUP, setupApActive ? "SETUP AP ON" : "START SETUP AP", setupApActive ? C_ACCENT : C_BUTTON, setupApActive ? C_ACCENT : C_GRID, setupApActive ? TFT_BLACK : C_TEXT, 2);
}

void drawEstopConfirmScreen() {
  M5.Display.fillScreen(C_DANGER);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_WHITE, C_DANGER);
  int tw = M5.Display.textWidth("E-STOP");
  M5.Display.setCursor((320 - tw) / 2, 52);
  M5.Display.print("E-STOP");
  drawCenteredText("PRESS RED SCREEN", 0, 106, 320, 28, 2, TFT_WHITE, C_DANGER);
  drawCenteredText("TO RELEASE PRESSURE", 0, 134, 320, 28, 2, TFT_WHITE, C_DANGER);
  M5.Display.fillRect(0, 216, 320, 24, C_GOOD);
  drawCenteredText("CANCEL", 0, 216, 320, 24, 2, TFT_BLACK, C_GOOD);
}

void drawEstopActiveScreen() {
  M5.Display.fillScreen(C_DANGER);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_WHITE, C_DANGER);
  int tw = M5.Display.textWidth("RELEASED");
  M5.Display.setCursor((320 - tw) / 2, 70);
  M5.Display.print("RELEASED");
  drawCenteredText("POSITIVE + NEGATIVE", 0, 118, 320, 28, 2, TFT_WHITE, C_DANGER);
  drawCenteredText("PRESSURE OFF", 0, 146, 320, 28, 2, TFT_WHITE, C_DANGER);
  M5.Display.fillRect(0, 216, 320, 24, C_GOOD);
  drawCenteredText("RETURN", 0, 216, 320, 24, 2, TFT_BLACK, C_GOOD);
}

void drawCurrentScreen() {
  initPalette();
  if (currentScreen == SCREEN_HOME) drawHomeScreen();
  else if (currentScreen == SCREEN_VAC) drawVacScreen();
  else if (currentScreen == SCREEN_PPM) drawPpmScreen();
  else if (currentScreen == SCREEN_MOTOR) drawMotorScreen();
  else if (currentScreen == SCREEN_RHYTHM) drawRhythmScreen();
  else if (currentScreen == SCREEN_LIVE) drawLiveScreen();
  else if (currentScreen == SCREEN_SETTINGS) drawSettingsScreen();
  else if (currentScreen == SCREEN_WIFI) drawWifiScreen();
  else if (currentScreen == SCREEN_ESTOP_CONFIRM) drawEstopConfirmScreen();
  else if (currentScreen == SCREEN_ESTOP_ACTIVE) drawEstopActiveScreen();
  uiNeedsFullRedraw = false;
}

void performEmergencyRelease() {
  running = false;
  activeMode = MODE_IDLE;
  estopLatched = true;
  // Emergency release affects live machine state only. It does not overwrite saved set points.
  vacActual = 0;
  allOutputsOff();
  liveValueDirty = true;
  logSdEvent("emergency release");
  setScreen(SCREEN_ESTOP_ACTIVE);
}

// ------------------ VALUE CHANGES ------------------
void changeVac(int delta) {
  int old = vacTarget;
  vacTarget = clampInt(vacTarget + delta, 0, 100);
  if (old != vacTarget) {
    vacValueDirty = true;
    liveValueDirty = true;
    markSettingsDirty();
  }
}

void changePpm(int delta) {
  int old = ppmTarget;
  int raw = ppmTarget + delta;
  if (delta > 0 && old == 0) raw = PPM_ACTIVE_MIN;
  if (delta < 0 && old <= PPM_ACTIVE_MIN) raw = 0;
  ppmTarget = normalizePpmInput(raw);
  if (old != ppmTarget) {
    ppmValueDirty = true;
    liveValueDirty = true;
    markSettingsDirty();
  }
}

void changeMotorPower(int deltaStep) {
  int old = motorPowerTarget;
  motorPowerTarget = normalizeMotorPower(motorPowerTarget + deltaStep * 10);
  if (old != motorPowerTarget) {
    motorValueDirty = true;
    liveValueDirty = true;
    markSettingsDirty();
  }
}

void changeBacklight(int delta) {
  int old = backlightStep;
  backlightStep = clampInt(backlightStep + delta, 1, 10);
  if (old != backlightStep) {
    applyBacklight();
    settingsValueDirty = true;
    markSettingsDirty();
  }
}

void applyHoldStep() {
  unsigned long held = millis() - holdStartMs;
  int step = 1;
  if (held >= TURBO_AFTER_MS) step = 10;
  else if (held >= FAST_AFTER_MS) step = 5;
  if (holdControl == 1) changeVac(-step);
  if (holdControl == 2) changeVac(step);
  if (holdControl == 3) changePpm(-step);
  if (holdControl == 4) changePpm(step);
  if (holdControl == 5) changeBacklight(-1);
  if (holdControl == 6) changeBacklight(1);
  if (holdControl == 7) changeMotorPower(-1);
  if (holdControl == 8) changeMotorPower(1);
  if (holdControl == 9) changeRhythmSpeed(-25);
  if (holdControl == 10) changeRhythmSpeed(25);
}

void beginHold(int ctrl) {
  holdControl = ctrl;
  holdStartMs = millis();
  lastHoldRepeatMs = millis();
  applyHoldStep();
}

void processHold(bool isPressed) {
  if (!isPressed || holdControl == 0) return;
  unsigned long now = millis();
  unsigned long gap = (now - holdStartMs < FIRST_REPEAT_MS) ? FIRST_REPEAT_MS : REPEAT_MS;
  if (now - lastHoldRepeatMs >= gap) {
    lastHoldRepeatMs = now;
    applyHoldStep();
  }
}

void endHold() {
  holdControl = 0;
  if (settingsDirty) saveSettingsNow(); // persist once on release, not every tick
}


bool isCustomRhythmMode(int mode) {
  return mode >= CUSTOM_RHYTHM_BASE && mode < RHYTHM_MODE_COUNT;
}

int customSlotFromMode(int mode) {
  if (!isCustomRhythmMode(mode)) return -1;
  return mode - CUSTOM_RHYTHM_BASE;
}

bool customRhythmHasData(int slot) {
  return slot >= 0 && slot < CUSTOM_RHYTHM_SLOTS && customRhythmData[slot].length() > 0;
}

String customNamesJson() {
  String j = "[";
  for (int i = 0; i < CUSTOM_RHYTHM_SLOTS; i++) {
    if (i) j += ",";
    j += "\"" + safeJsonString(customRhythmName[i]) + "\"";
  }
  j += "]";
  return j;
}

String customDataJson() {
  String j = "[";
  for (int i = 0; i < CUSTOM_RHYTHM_SLOTS; i++) {
    if (i) j += ",";
    j += "\"" + safeJsonString(customRhythmData[i]) + "\"";
  }
  j += "]";
  return j;
}

unsigned long customRhythmTotalMs(const String& data) {
  unsigned long total = 0;
  int start = 0;
  while (start < data.length()) {
    int semi = data.indexOf(';', start);
    if (semi < 0) semi = data.length();
    String pair = data.substring(start, semi);
    int comma = pair.indexOf(',');
    if (comma > 0) {
      int pulse = clampInt(pair.substring(0, comma).toInt(), 40, 2500);
      int gap = clampInt(pair.substring(comma + 1).toInt(), 0, 5000);
      total += pulse + gap;
    }
    start = semi + 1;
  }
  return total;
}

bool customRhythmPulseNow(int slot, unsigned long now) {
  if (!customRhythmHasData(slot)) return false;
  String data = customRhythmData[slot];
  unsigned long total = customRhythmTotalMs(data);
  if (total < 40) return false;
  unsigned long elapsed = now - customLoopStartMs;
  unsigned long scaled = (elapsed * (unsigned long)rhythmSpeedPct) / 100UL;
  unsigned long pos = scaled % total;

  int start = 0;
  while (start < data.length()) {
    int semi = data.indexOf(';', start);
    if (semi < 0) semi = data.length();
    String pair = data.substring(start, semi);
    int comma = pair.indexOf(',');
    if (comma > 0) {
      int pulse = clampInt(pair.substring(0, comma).toInt(), 40, 2500);
      int gap = clampInt(pair.substring(comma + 1).toInt(), 0, 5000);
      if (pos < (unsigned long)pulse) return true;
      pos -= (unsigned long)pulse;
      if (pos < (unsigned long)gap) return false;
      pos -= (unsigned long)gap;
    }
    start = semi + 1;
  }
  return false;
}

void setRhythmMode(int mode) {
  if (mode <= 0) {
    rhythmMode = 0;
    rhythmStep = -1;
    lastRhythmMs = 0;
    solPulseOn = false;
    applyHardwareOutputs();
    rhythmValueDirty = true;
    logSdEvent("rhythm off");
    if (currentScreen == SCREEN_RHYTHM) uiNeedsFullRedraw = true;
    return;
  }

  if (!running || activeMode != MODE_PROGRAM) startProgramMode();
  rhythmMode = clampInt(mode, 1, RHYTHM_MODE_COUNT - 1);
  if (isCustomRhythmMode(rhythmMode) && !customRhythmHasData(customSlotFromMode(rhythmMode))) {
    logSystemProblem("custom rhythm empty");
  }
  rhythmStep = -1;
  lastRhythmMs = 0;
  customLoopStartMs = millis();
  solPulseOn = false;
  rhythmValueDirty = true;
  String e = "rhythm "; e += rhythmName(rhythmMode);
  logSdEvent(e.c_str());
  if (currentScreen == SCREEN_RHYTHM) uiNeedsFullRedraw = true;
}

const char* rhythmName(int mode) {
  if (isCustomRhythmMode(mode)) {
    int slot = customSlotFromMode(mode);
    return customRhythmName[slot].c_str();
  }
  switch (mode) {
    case 1:  return "Steady";
    case 2:  return "Slow Pulse";
    case 3:  return "Heartbeat";
    case 4:  return "Double Tap";
    case 5:  return "Triple Tap";
    case 6:  return "Wave";
    case 7:  return "Rolling";
    case 8:  return "Breathing";
    case 9:  return "Massage Soft";
    case 10: return "Massage Deep";
    case 11: return "Stagger";
    case 12: return "Alt Pairs";
    case 13: return "SOS";
    case 14: return "Mary";
    case 15: return "Twinkle";
    case 16: return "Charge";
    case 17: return "Shave";
    case 18: return "Slow Release";
    case 19: return "Quick Flutter";
    case 20: return "Deep Wave";
    default: return "Off";
  }
}

const char* rhythmDots(int mode) {
  if (isCustomRhythmMode(mode)) {
    int slot = customSlotFromMode(mode);
    if (customRhythmData[slot].length() == 0) return "-- empty --";
    return customRhythmData[slot].c_str();
  }
  // Dot = short CH2 pulse, dash = longer CH2 pulse.
  // Patterns are CH2-only; they never touch CH1 VAC or CH3 MOTOR.
  switch (mode) {
    case 1:  return ". . . . . . . .";
    case 2:  return "- - - -";
    case 3:  return ". . - . . -";
    case 4:  return ". .   . .";
    case 5:  return ". . .   . . .";
    case 6:  return ". - . - . -";
    case 7:  return ". . - - . . - -";
    case 8:  return "- . . -";
    case 9:  return ". .   -";
    case 10: return "- - . .";
    case 11: return ". - . . - .";
    case 12: return ". . - -";
    case 13: return ". . . - - - . . .";
    case 14: return ". - . . - . .";
    case 15: return ". . - . . - -";
    case 16: return ". . . -";
    case 17: return ". . - . -";
    case 18: return "- .   - .";
    case 19: return ". . . . -";
    case 20: return "- . - . -";
    default: return "--";
  }
}

int rhythmPatternTicks(const char* pattern) {
  int total = 0;
  for (int i = 0; pattern[i] != '\0'; i++) {
    char c = pattern[i];
    if (c == '.') total += 2;       // 1 on, 1 off
    else if (c == '-') total += 4;  // 3 on, 1 off
    else if (c == ' ') total += 1;  // visual/extra rest separator
  }
  return total;
}

bool rhythmPulseFromDots(const char* pattern, int step) {
  int total = rhythmPatternTicks(pattern);
  if (total <= 0) return false;
  int t = step % total;

  for (int i = 0; pattern[i] != '\0'; i++) {
    char c = pattern[i];
    if (c == '.') {
      if (t == 0) return true;
      t -= 2;
    } else if (c == '-') {
      if (t >= 0 && t < 3) return true;
      t -= 4;
    } else if (c == ' ') {
      if (t == 0) return false;
      t -= 1;
    }
    if (t < 0) return false;
  }
  return false;
}

void applyRhythmOutputs() {
  if (rhythmMode == 0) return;

  // Rhythm/song programs are pulse-only. Never touch CH1 vacuum or CH3 motor.
  if (isCustomRhythmMode(rhythmMode)) {
    solPulseOn = customRhythmPulseNow(customSlotFromMode(rhythmMode), millis());
  } else {
    solPulseOn = rhythmPulseFromDots(rhythmDots(rhythmMode), rhythmStep);
  }
  applyHardwareOutputs();
}

void updateRhythmDemo() {
  if (!running || activeMode != MODE_PROGRAM || rhythmMode == 0) return;
  unsigned long now = millis();
  if (isCustomRhythmMode(rhythmMode)) {
    if (lastRhythmMs == 0 || now - lastRhythmMs >= 35) {
      lastRhythmMs = now;
      applyRhythmOutputs();
      rhythmValueDirty = true;
    }
    return;
  }
  unsigned long tick = (RHYTHM_BASE_TICK_MS * 100UL) / (unsigned long)rhythmSpeedPct;
  if (tick < 35) tick = 35;
  if (lastRhythmMs == 0 || now - lastRhythmMs >= tick) {
    lastRhythmMs = now;
    rhythmStep++;
    applyRhythmOutputs();
    rhythmValueDirty = true;
  }
}

// ------------------ TOUCH HANDLING ------------------
void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    int x = t.x;
    int y = t.y;
    logButtonPress("screen touch");

    if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_ACTIVE) {
      if (hit(BTN_TOP_STOP, x, y)) {
        setScreen(SCREEN_ESTOP_CONFIRM);
        return;
      }
      if (currentScreen != SCREEN_HOME && hit(BTN_TOP_HOME, x, y)) {
        setScreen(SCREEN_HOME);
        return;
      }
    }

    if (currentScreen == SCREEN_HOME) {
      if (hit(BTN_HOME_VAC, x, y)) { setScreen(SCREEN_VAC); return; }
      if (hit(BTN_HOME_PPM, x, y)) { setScreen(SCREEN_PPM); return; }
      if (hit(BTN_HOME_MOTOR, x, y)) { setScreen(SCREEN_MOTOR); return; }
      if (hit(BTN_HOME_RHYTHM, x, y)) { setScreen(SCREEN_RHYTHM); return; }
      if (hit(BTN_HOME_WIFI, x, y)) { setScreen(SCREEN_WIFI); return; }
      if (hit(BTN_HOME_RUN, x, y)) {
        if (running) stopProgramMode("program stop");
        else startProgramMode();
        uiNeedsFullRedraw = true;
        return;
      }
    }

    if (currentScreen == SCREEN_VAC) {
      if (hit(BTN_BIG_MINUS, x, y)) beginHold(1);
      else if (hit(BTN_BIG_PLUS, x, y)) beginHold(2);
      return;
    }

    if (currentScreen == SCREEN_PPM) {
      if (hit(BTN_BIG_MINUS, x, y)) beginHold(3);
      else if (hit(BTN_BIG_PLUS, x, y)) beginHold(4);
      return;
    }

    if (currentScreen == SCREEN_MOTOR) {
      if (hit(BTN_BIG_MINUS, x, y)) beginHold(7);
      else if (hit(BTN_BIG_PLUS, x, y)) beginHold(8);
      return;
    }

    if (currentScreen == SCREEN_RHYTHM) {
      if (hit(BTN_RHY_PREV, x, y)) {
        int next = rhythmMode <= 1 ? RHYTHM_MODE_COUNT - 1 : rhythmMode - 1;
        setRhythmMode(next);
        return;
      }
      if (hit(BTN_RHY_TOGGLE, x, y)) {
        if (rhythmMode > 0) setRhythmMode(0);
        else setRhythmMode(favoriteRhythm1 > 0 ? favoriteRhythm1 : 3);
        return;
      }
      if (hit(BTN_RHY_NEXT, x, y)) {
        int next = rhythmMode + 1;
        if (next <= 0 || next >= RHYTHM_MODE_COUNT) next = 1;
        setRhythmMode(next);
        return;
      }
      if (hit(BTN_RHY_SLOW, x, y)) { beginHold(9); return; }
      if (hit(BTN_RHY_FAST, x, y)) { beginHold(10); return; }
      return;
    }

    if (currentScreen == SCREEN_SETTINGS) {
      if (insideRect(x, y, 182, 39, 124, 40)) {
        darkTheme = !darkTheme;
        initPalette();
        uiNeedsFullRedraw = true;
        markSettingsDirty();
        return;
      }
      if (insideRect(x, y, 20, 146, 120, 50)) beginHold(5);
      else if (insideRect(x, y, 180, 146, 120, 50)) beginHold(6);
      return;
    }

    if (currentScreen == SCREEN_LIVE) {
      if (insideRect(x, y, 220, 205, 88, 31)) {
        if (running) stopProgramMode("program stop"); else startProgramMode();
        uiNeedsFullRedraw = true;
        return;
      }
      return;
    }

    if (currentScreen == SCREEN_WIFI) {
      if (hit(BTN_WIFI_WEB, x, y)) {
        if (webServerRunning()) {
          webControlEnabled = false;
          wifiLastMessage = "Webserver turned off";
          markSettingsDirty();
          saveSettingsNow();
          stopWebServerNow();
        } else {
          webControlEnabled = true;
          markSettingsDirty();
          saveSettingsNow();
          if (!setupApActive && WiFi.status() != WL_CONNECTED) startSetupAp();
          startWebServer();
        }
        uiNeedsFullRedraw = true;
        return;
      }
      if (hit(BTN_WIFI_SETUP, x, y)) {
        startSetupAp();
        if (webControlEnabled) startWebServer();
        uiNeedsFullRedraw = true;
        return;
      }
      return;
    }

    if (currentScreen == SCREEN_ESTOP_CONFIRM) {
      if (insideRect(x, y, 0, 216, 320, 24)) { setScreen(previousScreen); return; }
      if (insideRect(x, y, 0, 0, 320, 216)) { performEmergencyRelease(); return; }
    }

    if (currentScreen == SCREEN_ESTOP_ACTIVE) {
      if (insideRect(x, y, 0, 216, 320, 24)) {
        estopLatched = false;
        setScreen(SCREEN_HOME);
        return;
      }
    }
  }

  processHold(t.isPressed());

  if (t.wasReleased()) {
    endHold();
  }
}

void updateActiveScreenRegions() {
  unsigned long now = millis();
  if (currentScreen == SCREEN_HOME && now - lastLiveMs >= 180) {
    lastLiveMs = now;
    drawHomeVisuals();
  }
  if (currentScreen == SCREEN_VAC) {
    if (vacValueDirty) refreshVacValue();
    if (now - lastGraphMs >= 90) {
      lastGraphMs = now;
      updateVacGraph();
    }
  }
  if (currentScreen == SCREEN_PPM) {
    if (ppmValueDirty) refreshPpmValue();
    if (now - lastGraphMs >= 80) {
      lastGraphMs = now;
      updatePpmGraph();
    }
  }
  if (currentScreen == SCREEN_MOTOR) {
    if (motorValueDirty) refreshMotorValue();
    if (now - lastGraphMs >= 180) {
      lastGraphMs = now;
      updateMotorVisual();
    }
  }
  if (currentScreen == SCREEN_RHYTHM && now - lastGraphMs >= 120) {
    lastGraphMs = now;
    updateRhythmVisual();
  }
  if (now - lastLiveSampleMs >= 250) {
    lastLiveSampleMs = now;
    addLivePoint();
  }
  if (currentScreen == SCREEN_LIVE && running && now - lastLiveMs >= 250) {
    lastLiveMs = now;
    updateLiveGraphs();
  }
  if (currentScreen == SCREEN_SETTINGS && settingsValueDirty) {
    uiNeedsFullRedraw = true;
    settingsValueDirty = false;
  }
}

// ------------------ WEB PAGE BUILDING ------------------
String checked(bool b) { return b ? "checked" : ""; }

String htmlPage() {
  String h;
  h.reserve(22000);
  h += R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"><title>Pluto 9000</title>
<style>
:root{--bg:#0b1018;--panel:#141d2a;--panel2:#1b2938;--text:#f2f7ff;--muted:#a9b6c7;--line:#304158;--blue:#43aaff;--green:#78e6be;--red:#e84650;--yellow:#ffbc4b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font-family:Arial,Helvetica,sans-serif;-webkit-text-size-adjust:100%}header{position:sticky;top:0;z-index:10;background:#0d1420;border-bottom:1px solid var(--line);padding:12px 14px}.top{display:flex;justify-content:space-between;gap:10px;align-items:center}.brand{font-size:25px;font-weight:900}.small{color:var(--muted);font-size:13px;line-height:1.35}.wrap{max-width:1050px;margin:auto;padding:12px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(285px,1fr));gap:12px}.card{background:linear-gradient(180deg,var(--panel),#101722);border:1px solid #26364c;border-radius:18px;padding:14px;box-shadow:0 10px 22px #0005}h2{margin:0 0 8px;font-size:22px}.value{font-size:42px;font-weight:900;line-height:1}.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.row>*{flex:1 1 120px}button{border:0;border-radius:13px;min-height:52px;padding:12px;font-size:17px;font-weight:900;background:var(--green);color:#071019;touch-action:manipulation}.ghost{background:#233247;color:var(--text);border:1px solid #3b506a}.danger{background:var(--red);color:#fff}.warn{background:var(--yellow);color:#160f00}.blue{background:var(--blue);color:#06111b}input[type=range]{width:100%;height:32px;accent-color:var(--green)}select,input[type=text],input[type=password],input[type=number]{width:100%;background:#0b111b;border:1px solid var(--line);border-radius:12px;color:var(--text);padding:11px;font-size:16px}.pill{display:inline-block;border:1px solid var(--line);background:#101823;color:var(--muted);border-radius:999px;padding:7px 10px;margin:3px}.meter{height:17px;background:#263348;border-radius:999px;overflow:hidden;margin:8px 0}.fill{height:100%;background:linear-gradient(90deg,var(--blue),var(--green));width:0%}.nav{display:flex;gap:8px;overflow:auto;padding-top:8px}.nav a{white-space:nowrap;text-decoration:none;color:var(--text);background:#1a2637;border:1px solid #2b3d55;border-radius:999px;padding:9px 12px;font-weight:800}.statusLine{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}.statusLine div{background:#0b111b;border:1px solid var(--line);border-radius:12px;padding:8px;min-height:50px}.mini{font-size:13px;color:var(--muted);line-height:1.4}.share{background:#08111d;border:1px dashed #385476;border-radius:12px;padding:10px;margin-top:8px;word-break:break-all}.on{background:var(--green)!important;color:#05100b!important}canvas{width:100%;height:105px;background:#0b111b;border:1px solid var(--line);border-radius:12px;margin-top:8px}.net{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center;padding:8px;border:1px solid var(--line);border-radius:12px;margin:7px 0;background:#101823}@media(max-width:520px){header{padding:10px}.brand{font-size:22px}.wrap{padding:8px}.card{padding:12px;border-radius:16px}button{min-height:54px;font-size:16px;padding:10px}.value{font-size:38px}.statusLine{grid-template-columns:1fr}.nav a{font-size:14px;padding:8px 10px}}@media(min-width:900px){.wide{grid-column:span 2}}
</style></head><body><header><div class="top"><div><div class="brand">Pluto 9000 Control</div><div class="small">3-channel prototype: CH1 Vacuum · CH2 PPM/Rhythm · CH3 Motor</div></div><div><span class="pill" id="modePill">--</span><span class="pill" id="runPill">--</span></div></div><div class="nav"><a href="#status">Status</a><a href="#program">Run</a><a href="#rhythm">Rhythms</a><a href="#wifi">WiFi</a><a href="#system">System</a></div></header><main class="wrap">
<section id="status" class="card wide"><h2>Status</h2><div class="statusLine"><div><span class="mini">SSID</span><br><b id="ssid">--</b></div><div><span class="mini">IP</span><br><b id="ip">--</b></div><div><span class="mini">Outputs CH1/CH2/CH3</span><br><b id="outs">---</b></div></div><div class="mini" id="wifiMsg"></div><div class="share" id="shareLink">No IP yet</div></section>
<section id="program" class="grid"><div class="card"><h2>Program Control</h2><div class="row"><button id="runBtn" onclick="toggleRun()">START</button><button class="danger" onclick="api('/api/estop')">E-STOP</button></div><p class="mini">Screen changes do not stop the system. VAC, PPM, and MOTOR stay independent while running.</p></div>
<div class="card"><h2>Vacuum</h2><div class="value"><span id="vacNow">0</span>%</div><div class="mini">Target <span id="vacTarget">0</span>% · response <span id="rampNow">5</span>/10</div><div class="meter"><div id="vacFill" class="fill"></div></div><input id="vac" type="range" min="0" max="100" value="0" oninput="previewVac(this.value)" onchange="setVal('vac',this.value)"><label class="mini">Response time</label><input id="ramp" type="range" min="1" max="10" value="5" oninput="previewRamp(this.value)" onchange="setVal('ramp',this.value)"></div>
<div class="card"><h2>PPM / Pulse</h2><div class="value"><span id="ppmNow">0</span> PPM</div><div class="mini">0 = off · active range 30–400 PPM</div><div class="meter"><div id="ppmFill" class="fill"></div></div><input id="ppm" type="range" min="0" max="400" value="0" oninput="previewPpm(this.value)" onchange="setVal('ppm',this.value)"><canvas id="pulseCanvas"></canvas></div>
<div class="card"><h2>Motor Speed</h2><div class="value"><span id="motorNow">0</span>%</div><div class="mini">CH3 / G7 motor command in 10% steps</div><div class="meter"><div id="motorFill" class="fill"></div></div><input id="motor" type="range" min="0" max="100" step="10" value="0" oninput="previewMotor(this.value)" onchange="setVal('motor',this.value)"><div class="row"><button class="ghost" onclick="setMotor(0)">OFF</button><button class="ghost" onclick="setMotor(50)">50%</button><button class="ghost" onclick="setMotor(100)">100%</button></div></div></section>
<section id="rhythm" class="grid"><div class="card wide"><h2>Rhythms / Songs</h2><p class="mini">Rhythms only drive CH2. Vacuum and motor remain adjustable while the pulse pattern runs.</p><select id="rhythmSelect" onchange="setRhythm(this.value)"></select><p class="mini">Dot/dash: <b id="rhythmDotsNow">--</b></p><label class="mini">Rhythm speed <span id="rhythmSpeedNow">100</span>%</label><input id="rhythmSpeed" type="range" min="25" max="300" step="25" value="100" oninput="document.getElementById('rhythmSpeedNow').innerText=this.value" onchange="setRhythmSpeed(this.value)"><canvas id="rhythmCanvas"></canvas><div class="row"><button class="ghost" onclick="setRhythm(0)">STOP RHYTHM</button><button class="blue" onclick="setRhythm(document.getElementById('rhythmSelect').value||3)">RUN SELECTED</button><button onclick="saveFav(1)">SAVE FAV 1</button><button onclick="runFav(1)">RUN FAV 1</button></div></div><div class="card wide"><h2>Custom Tap Rhythm</h2><p class="mini">Web-configurable custom rhythms save durably and play on CH2 only. Hold or tap the button below while recording.</p><div class="row"><select id="customSlot"></select><input id="customName" placeholder="Custom name" maxlength="18"><button onclick="customAction('rename')">SAVE NAME</button></div><div class="row"><button class="blue" onclick="customAction('start')">RECORD</button><button id="tapPulse" class="danger">TAP / HOLD PULSE</button><button class="ghost" onclick="customAction('stop')">STOP REC</button></div><div class="row"><button onclick="customAction('play')">PLAY LOOP</button><button onclick="customAction('save')">SAVE SLOT</button><button class="ghost" onclick="customAction('clear')">CLEAR</button></div><p class="mini">Slot data: <b id="customDataNow">empty</b></p><p class="mini" id="customMsg">Custom rhythm ready.</p></div></section>
<section id="wifi" class="grid"><div class="card"><h2>WiFi Setup AP</h2><p class="mini">Connect phone to <b>Pluto9000-Setup</b>, then scan/select your home WiFi here. The touchscreen stays usable while WiFi connects in the background.</p><div class="row"><button class="ghost" onclick="api('/api/wifireset')">START SETUP AP</button><button class="ghost" onclick="scanWifi()">SCAN WIFI</button></div><div id="networks"></div></div><div class="card"><h2>Join Home Network</h2><label class="mini">SSID</label><input id="joinSsid" type="text" placeholder="Home WiFi name"><label class="mini">Password</label><input id="joinPass" type="password" placeholder="WiFi password"><button onclick="joinWifi()">JOIN WIFI</button><p class="mini" id="joinMsg"></p></div></section>
<section id="system" class="grid"><div class="card"><h2>System</h2><p><span class="pill">Version )rawliteral";
  h += APP_VERSION;
  h += R"rawliteral(</span><span class="pill">SD <span id="sdWeb">--</span></span></p><div class="row"><button class="ghost" onclick="location.href='/stats.html'">OPEN STATS</button><button class="ghost" onclick="location.href='/current.log'">VIEW LOG</button><button class="ghost" onclick="api('/api/save')">SAVE SETTINGS</button></div></div></section></main>
<script>
const names=['Off','Steady','Slow Pulse','Heartbeat','Double Tap','Triple Tap','Wave','Rolling','Breathing','Massage Soft','Massage Deep','Stagger','Alt Pairs','SOS','Mary','Twinkle','Charge','Shave','Slow Release','Quick Flutter','Deep Wave'];const dots=['--','. . . . . . . .','- - - -','. . - . . -','. .   . .','. . .   . . .','. - . - . -','. . - - . . - -','- . . -','. .   -','- - . .','. - . . - .','. . - -','. . . - - - . . .','. - . . - . .','. . - . . - -','. . . -','. . - . -','- .   - .','. . . . -','- . - . -'];let customNames=['Custom 1','Custom 2','Custom 3','Custom 4','Custom 5'];let customData=['','','','',''];function el(i){return document.getElementById(i)}function clamp(v,a,b){v=parseInt(v||0);return Math.max(a,Math.min(b,v))}function api(u){return fetch(u,{cache:'no-store'}).then(r=>r.text()).then(t=>{poll();return t})}function setVal(k,v){return api('/api/set?'+k+'='+encodeURIComponent(v))}function toggleRun(){let run=el('runPill').innerText==='RUNNING';api('/api/run?state='+(run?0:1))}function setRhythm(m){api('/api/rhythm?mode='+m)}function setRhythmSpeed(s){api('/api/rhythm?speed='+s)}function saveFav(s){api('/api/favorite?slot='+s+'&save='+(el('rhythmSelect').value||0))}function runFav(s){api('/api/favorite?slot='+s)}function previewVac(v){v=clamp(v,0,100);el('vacTarget').innerText=v;el('vacFill').style.width=v+'%'}function previewRamp(v){el('rampNow').innerText=clamp(v,1,10)}function previewPpm(v){v=clamp(v,0,400);if(v>0&&v<30)v=30;el('ppmNow').innerText=v;el('ppmFill').style.width=(v/400*100)+'%';drawPulse('pulseCanvas',v)}function normalizeMotor(v){v=clamp(v,0,100);if(v<=0)return 0;return Math.max(10,Math.min(100,Math.round(v/10)*10))}function previewMotor(v){v=normalizeMotor(v);el('motorNow').innerText=v;el('motorFill').style.width=v+'%';el('motor').value=v}function setMotor(v){v=normalizeMotor(v);previewMotor(v);setVal('motor',v)}function fillSelect(){let rv=el('rhythmSelect').value,cv=el('customSlot')?el('customSlot').value:'1';let s=el('rhythmSelect');s.innerHTML='';names.forEach((n,i)=>{let o=document.createElement('option');o.value=i;o.textContent=i+' - '+n+'  '+(dots[i]||'');s.appendChild(o)});customNames.forEach((n,i)=>{let o=document.createElement('option');o.value=21+i;o.textContent=(21+i)+' - '+n+'  CUSTOM';s.appendChild(o)});s.value=rv||0;let cs=el('customSlot');if(cs){cs.innerHTML='';for(let i=0;i<5;i++){let o=document.createElement('option');o.value=i+1;o.textContent='Slot '+(i+1)+' - '+customNames[i];cs.appendChild(o)}cs.value=cv||1}}function drawPulse(id,ppm,on){let c=el(id);if(!c)return;let r=c.getBoundingClientRect();c.width=Math.max(1,r.width*devicePixelRatio);c.height=Math.max(1,r.height*devicePixelRatio);let x=c.getContext('2d');x.scale(devicePixelRatio,devicePixelRatio);x.clearRect(0,0,r.width,r.height);x.strokeStyle='#304158';for(let i=1;i<4;i++){let y=r.height*i/4;x.beginPath();x.moveTo(0,y);x.lineTo(r.width,y);x.stroke()}if(ppm<=0&&!on)return;let period=ppm>0?60000/ppm:500,win=3000;x.strokeStyle=on?'#78e6be':'#ffbc4b';x.lineWidth=3;x.beginPath();for(let px=0;px<r.width;px++){let t=px/r.width*win;let phase=(t%period)/period;let isOn=phase<.5;let y=isOn?18:r.height-18;if(px===0)x.moveTo(px,y);else x.lineTo(px,y)}x.stroke()}async function customAction(a){let slot=el('customSlot').value||1;let name=encodeURIComponent(el('customName').value||'');let t=await fetch('/api/custom?slot='+slot+'&action='+a+'&name='+name,{cache:'no-store'}).then(r=>r.text());el('customMsg').innerText=t;poll()}function customDown(e){e.preventDefault();let slot=el('customSlot').value||1;fetch('/api/custom?slot='+slot+'&action=down',{cache:'no-store'})}function customUp(e){e.preventDefault();let slot=el('customSlot').value||1;fetch('/api/custom?slot='+slot+'&action=up',{cache:'no-store'}).then(()=>poll())}function bindCustomTap(){let b=el('tapPulse');if(!b)return;b.addEventListener('pointerdown',customDown,{passive:false});b.addEventListener('pointerup',customUp,{passive:false});b.addEventListener('pointercancel',customUp,{passive:false});b.addEventListener('pointerleave',customUp,{passive:false});let cs=el('customSlot');if(cs)cs.onchange=()=>{let i=(parseInt(cs.value||1)-1);el('customName').value=customNames[i]||('Custom '+(i+1));el('customDataNow').innerText=customData[i]||'empty'}}async function scanWifi(){let box=el('networks');box.innerHTML='<p class="mini">Scanning...</p>';let j=await fetch('/api/wifiscan',{cache:'no-store'}).then(r=>r.json());box.innerHTML='';(j.networks||[]).forEach(n=>{let d=document.createElement('div');d.className='net';d.innerHTML='<div><b>'+n.ssid+'</b><div class="mini">RSSI '+n.rssi+'</div></div><button class="ghost">USE</button>';d.querySelector('button').onclick=()=>{el('joinSsid').value=n.ssid};box.appendChild(d)});if(!j.networks||!j.networks.length)box.innerHTML='<p class="mini">No networks found.</p>';poll()}async function joinWifi(){let s=encodeURIComponent(el('joinSsid').value),p=encodeURIComponent(el('joinPass').value);el('joinMsg').innerText='Connecting in background... unit remains local-ready.';let t=await fetch('/api/wifijoin?ssid='+s+'&pass='+p,{cache:'no-store'}).then(r=>r.text());el('joinMsg').innerText=t;poll()}async function poll(){try{let s=await fetch('/api/state',{cache:'no-store'}).then(r=>r.json());el('modePill').innerText=s.mode==1?'PROGRAM':'IDLE';el('runPill').innerText=s.running?'RUNNING':'STOPPED';el('runBtn').innerText=s.running?'STOP':'START';el('runBtn').classList.toggle('danger',s.running);el('ssid').innerText=s.ssid;el('ip').innerText=s.ip;el('shareLink').innerText=s.share;el('wifiMsg').innerText=s.wifiMode+' · '+s.wifiMsg+(s.wifiRssi?' · RSSI '+s.wifiRssi+' dBm':'');el('outs').innerText=(s.solVacuum?'1':'-')+(s.solPulse?'2':'-')+(s.solMotor?'3':'-');el('vacNow').innerText=s.vacActual;el('vacTarget').innerText=s.vac;el('vac').value=s.vac;el('vacFill').style.width=s.vacActual+'%';el('ppmNow').innerText=s.ppm;el('ppm').value=s.ppm;el('ppmFill').style.width=(s.ppm/400*100)+'%';el('rampNow').innerText=s.ramp;el('ramp').value=s.ramp;el('motorNow').innerText=s.motor;el('motor').value=s.motor;el('motorFill').style.width=s.motor+'%';el('rhythmSpeed').value=s.rhythmSpeed;el('rhythmSpeedNow').innerText=s.rhythmSpeed;el('rhythmSelect').value=s.rhythm;customNames=s.customNames||customNames;customData=s.customData||customData;fillSelect();el('rhythmSelect').value=s.rhythm;el('rhythmDotsNow').innerText=s.rhythmDots||dots[s.rhythm]||'--';let cs=el('customSlot');if(cs){let i=(parseInt(cs.value||1)-1);el('customName').value=customNames[i]||('Custom '+(i+1));el('customDataNow').innerText=customData[i]||'empty';el('customMsg').innerText=s.customRecording?'RECORDING slot '+(s.customRecordSlot+1):'Custom rhythm ready.'}el('sdWeb').innerText=s.sdReady?'READY':'NONE';drawPulse('pulseCanvas',s.ppm,s.solPulse);drawPulse('rhythmCanvas',s.rhythm?120:0,s.solPulse)}catch(e){}}fillSelect();bindCustomTap();poll();setInterval(poll,1000);
</script></body></html>)rawliteral";
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleState() {
  String j;
  j.reserve(1200);
  j += "{";
  j += "\"vac\":" + String(vacTarget) + ",";
  j += "\"vacActual\":" + String((int)vacActual) + ",";
  j += "\"ppm\":" + String(ppmTarget) + ",";
  j += "\"motor\":" + String(motorPowerTarget) + ",";
  j += "\"ramp\":" + String(vacRampSpeed) + ",";
  j += "\"running\":" + String(running ? "true" : "false") + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"uptime\":" + String((millis() - bootMs) / 1000) + ",";
  j += "\"saved\":" + String(settingsDirty ? "false" : "true") + ",";
  j += "\"webRefresh\":" + String(webRefreshSec) + ",";
  j += "\"mobile\":" + String(mobileMode ? "true" : "false") + ",";
  j += "\"sdReady\":" + String(sdReady ? "true" : "false") + ",";
  j += "\"sdDataReady\":" + String(sdDataReady ? "true" : "false") + ",";
  j += "\"sdEvents\":" + String(sdEventCount) + ",";
  j += "\"statsExists\":" + String((sdReady && SD.exists("/stats.html")) ? "true" : "false") + ",";
  j += "\"logExists\":" + String((sdLoggingReady() && SD.exists(sessionLogPath)) ? "true" : "false") + ",";
  j += "\"wifiActive\":" + String(webServerRunning() ? "true" : "false") + ",";
  j += "\"setupAp\":" + String(setupApActive ? "true" : "false") + ",";
  j += "\"ssid\":\"" + safeJsonString(wifiStatusLabel()) + "\",";
  j += "\"ip\":\"" + wifiIpLabel() + "\",";
  j += "\"share\":\"" + safeJsonString(shareLink()) + "\",";
  j += "\"wifiMode\":\"" + safeJsonString(wifiModeLabel()) + "\",";
  j += "\"wifiMsg\":\"" + safeJsonString(wifiLastMessage) + "\",";
  j += "\"wifiConnecting\":" + String(wifiStaConnecting ? "true" : "false") + ",";
  j += "\"wifiRssi\":" + String(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0) + ",";
  j += "\"wifiBars\":" + String(wifiSignalBars()) + ",";
  j += "\"webPort\":" + String(webPort) + ",";
  j += "\"mode\":" + String(activeMode) + ",";
  j += "\"solVacuum\":" + String(solVacOn ? "true" : "false") + ",";
  j += "\"solPulse\":" + String(solPulseOn ? "true" : "false") + ",";
  j += "\"solMotor\":" + String(solMotorOn ? "true" : "false") + ",";
  j += "\"rhythm\":" + String(rhythmMode) + ",";
  j += "\"rhythmName\":\"" + String(rhythmName(rhythmMode)) + "\",";
  j += "\"rhythmDots\":\"" + String(rhythmDots(rhythmMode)) + "\",";
  j += "\"rhythmSpeed\":" + String(rhythmSpeedPct) + ",";
  j += "\"fav1\":" + String(favoriteRhythm1) + ",";
  j += "\"fav2\":" + String(favoriteRhythm2) + ",";
  j += "\"fav3\":" + String(favoriteRhythm3) + ",";
  j += "\"customNames\":" + customNamesJson() + ",";
  j += "\"customData\":" + customDataJson() + ",";
  j += "\"customRecording\":" + String(customRecording ? "true" : "false") + ",";
  j += "\"customRecordSlot\":" + String(customRecordSlot) + ",";
  j += "\"currentLog\":\"" + sessionLogPath + "\"";
  j += "}";
  server.send(200, "application/json", j);
}
void handleSet() {
  bool paletteChanged = false;
  bool layoutChanged = false;

  if (server.hasArg("vac")) { vacTarget = clampInt(server.arg("vac").toInt(), 0, 100); vacValueDirty = true; liveValueDirty = true; }
  if (server.hasArg("ppm")) { ppmTarget = normalizePpmInput(server.arg("ppm").toInt()); ppmValueDirty = true; liveValueDirty = true; }
  if (server.hasArg("ramp")) { vacRampSpeed = clampInt(server.arg("ramp").toInt(), 1, 10); markSettingsDirty(); }
  if (server.hasArg("motor")) { motorPowerTarget = normalizeMotorPower(server.arg("motor").toInt()); motorValueDirty = true; liveValueDirty = true; markSettingsDirty(); }
  if (server.hasArg("graph")) graphSpeed = clampInt(server.arg("graph").toInt(), 1, 10);
  if (server.hasArg("release")) releaseMs = clampInt(server.arg("release").toInt(), 250, 5000);
  if (server.hasArg("bl")) { backlightStep = clampInt(server.arg("bl").toInt(), 1, 10); applyBacklight(); }
  if (server.hasArg("dark")) { darkTheme = server.arg("dark").toInt() == 1; paletteChanged = true; layoutChanged = true; }
  if (server.hasArg("large")) { largeButtons = server.arg("large").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("scale")) { screenScale = clampInt(server.arg("scale").toInt(), 1, 3); layoutChanged = true; }
  if (server.hasArg("showVac")) { showVac = server.arg("showVac").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showPpm")) { showPpm = server.arg("showPpm").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showLive")) { showLive = server.arg("showLive").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showSettings")) { showSettings = server.arg("showSettings").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showWifi")) { showWifi = server.arg("showWifi").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("mobile")) { mobileMode = server.arg("mobile").toInt() == 1; }
  if (server.hasArg("simple")) { simpleHome = server.arg("simple").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("advweb")) { showAdvancedWeb = server.arg("advweb").toInt() == 1; }
  if (server.hasArg("webref")) { webRefreshSec = clampInt(server.arg("webref").toInt(), 1, 5); }
  if (server.hasArg("webPort")) { webPort = clampInt(server.arg("webPort").toInt(), 80, 65535); wifiLastMessage = "Port saved. Reset webserver to apply."; }
  if (server.hasArg("port")) { webPort = clampInt(server.arg("port").toInt(), 80, 65535); }

  logButtonPress("web setting change");
  if (paletteChanged) initPalette();
  if (layoutChanged) uiNeedsFullRedraw = true;
  markSettingsDirty();
  server.send(200, "text/plain", "OK");
}

void handleRun() {
  logButtonPress("web run");
  if (server.hasArg("state")) {
    bool newState = server.arg("state").toInt() == 1;
    if (newState && estopLatched) { server.send(423, "text/plain", "ESTOP_LATCHED"); return; }
    if (newState) startProgramMode();
    else stopProgramMode("program stop");
  }
  uiNeedsFullRedraw = true;
  server.send(200, "text/plain", "OK");
}

void handleRhythmApi() {
  if (server.hasArg("speed")) {
    rhythmSpeedPct = clampInt(server.arg("speed").toInt(), 25, 300);
    rhythmValueDirty = true;
    markSettingsDirty();
  }
  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    setRhythmMode(m);
  }
  server.send(200, "text/plain", "OK");
}

void handleCustomRhythmApi() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 1;
  slot = clampInt(slot, 1, CUSTOM_RHYTHM_SLOTS) - 1;
  String action = server.hasArg("action") ? server.arg("action") : "status";
  unsigned long now = millis();

  if (server.hasArg("name")) {
    String nm = server.arg("name");
    nm.trim();
    if (nm.length() > 0) {
      if (nm.length() > 18) nm.remove(18);
      customRhythmName[slot] = nm;
      markSettingsDirty();
    }
  }

  if (action == "start") {
    customRecording = true;
    customRecordSlot = slot;
    customRecordData = "";
    customRecordDownMs = 0;
    customLastTapEndMs = now;
    logSdEvent("custom rhythm record start");
    server.send(200, "text/plain", "RECORDING");
    return;
  }

  if (action == "down") {
    if (customRecording && customRecordSlot == slot) customRecordDownMs = now;
    server.send(200, "text/plain", "DOWN");
    return;
  }

  if (action == "up" || action == "tap") {
    if (!customRecording || customRecordSlot != slot) { server.send(409, "text/plain", "NOT_RECORDING"); return; }
    unsigned long down = customRecordDownMs ? customRecordDownMs : now - 120;
    int pulse = clampInt((int)(now - down), 50, 1800);
    int gap = clampInt((int)(down - customLastTapEndMs), 0, 3000);
    String add = String(pulse) + "," + String(gap) + ";";
    if (customRecordData.length() + add.length() <= CUSTOM_RHYTHM_MAX_CHARS) {
      customRecordData += add;
      customLastTapEndMs = now;
      customRecordDownMs = 0;
      logSdEvent("custom rhythm tap");
      server.send(200, "text/plain", "TAP_SAVED");
    } else {
      server.send(507, "text/plain", "CUSTOM_PATTERN_FULL");
    }
    return;
  }

  if (action == "stop") {
    customRecording = false;
    customRecordDownMs = 0;
    logSdEvent("custom rhythm record stop");
    server.send(200, "text/plain", "STOPPED_RECORDING");
    return;
  }

  if (action == "save") {
    if (customRecordSlot == slot && customRecordData.length() > 0) customRhythmData[slot] = customRecordData;
    if (customRhythmData[slot].length() > CUSTOM_RHYTHM_MAX_CHARS) customRhythmData[slot].remove(CUSTOM_RHYTHM_MAX_CHARS);
    customRecording = false;
    saveSettingsNow();
    logSdEvent("custom rhythm saved");
    server.send(200, "text/plain", "SAVED");
    return;
  }

  if (action == "play") {
    customRecording = false;
    setRhythmMode(CUSTOM_RHYTHM_BASE + slot);
    logSdEvent("custom rhythm play");
    server.send(200, "text/plain", "PLAYING");
    return;
  }

  if (action == "clear") {
    if (rhythmMode == CUSTOM_RHYTHM_BASE + slot) setRhythmMode(0);
    customRhythmData[slot] = "";
    customRecordData = "";
    customRecording = false;
    saveSettingsNow();
    logSdEvent("custom rhythm cleared");
    server.send(200, "text/plain", "CLEARED");
    return;
  }

  if (action == "rename") {
    saveSettingsNow();
    logSdEvent("custom rhythm renamed");
    server.send(200, "text/plain", "RENAMED");
    return;
  }

  String j = "{\"slot\":" + String(slot + 1) + ",\"name\":\"" + safeJsonString(customRhythmName[slot]) + "\",\"data\":\"" + safeJsonString(customRhythmData[slot]) + "\",\"recording\":" + String(customRecording ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

void handleFavoriteApi() {
  int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 1;
  if (server.hasArg("save")) {
    int m = server.arg("save").toInt();
    if (m <= 0) m = rhythmMode;
    m = clampInt(m, 1, RHYTHM_MODE_COUNT - 1);
    if (slot == 1) favoriteRhythm1 = m;
    else if (slot == 2) favoriteRhythm2 = m;
    else favoriteRhythm3 = m;
    markSettingsDirty();
    logSdEvent("rhythm favorite saved");
    server.send(200, "text/plain", "SAVED");
    return;
  }
  int m = favoriteRhythm1;
  if (slot == 2) m = favoriteRhythm2;
  if (slot == 3) m = favoriteRhythm3;
  setRhythmMode(m);
  server.send(200, "text/plain", "OK");
}

void handleSave() {
  saveSettingsNow();
  server.send(200, "text/plain", "SAVED");
}

void handleEstop() {
  // No drawing here. Main loop performs screen change.
  setScreen(SCREEN_ESTOP_CONFIRM);
  server.send(200, "text/plain", "ESTOP_CONFIRM");
}

void handleRelease() {
  performEmergencyRelease();
  server.send(200, "text/plain", "RELEASED");
}


void handleStatsFile() {
  if (!sdReady || !SD.exists("/stats.html")) {
    String m;
    m.reserve(2200);
    m += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>Stats not ready</title><style>body{margin:0;background:#0d1320;color:#eef5ff;font-family:Arial,Helvetica,sans-serif}.wrap{max-width:720px;margin:auto;padding:18px}.card{background:#182235;border:1px solid #2a3a53;border-radius:22px;padding:22px;margin-top:18px}h1{font-size:34px;margin:0 0 12px}.note{color:#aebed3;font-size:18px;line-height:1.45}a,button{display:inline-block;margin-top:12px;border:0;border-radius:16px;padding:16px 18px;font-size:19px;font-weight:900;text-decoration:none;background:#78e6be;color:#081019}.ghost{background:#22314a;color:#eef5ff;border:1px solid #34465f;margin-left:8px}</style></head><body><div class='wrap'><div class='card'><h1>Stats not ready yet</h1><p class='note'>The Pluto 9000 can run normally without an SD card. SD logging is only for recording logs, saving session stats, and opening the <b>stats.html</b> report.</p><p class='note'>To create this report, go back to the dashboard and use <b>More - SD Card Logging</b> to set up SD logging.</p><a href='/'>Back to dashboard</a></div></div></body></html>");
    server.send(200, "text/html", m);
    return;
  }
  File f = SD.open("/stats.html", FILE_READ);
  if (!f) { server.send(500, "text/plain", "Could not open stats.html"); return; }
  server.streamFile(f, "text/html");
  f.close();
}

void handleLogFile() {
  String path = sessionLogPath.length() ? sessionLogPath : dailyLogPath();
  if (!sdReady || !SD.exists(path)) {
    server.send(404, "text/plain", "No daily log is set up yet. Logs are stored in /log/YYYY-MM-DD/YYYY-MM-DD.log after SD logging is set up.");
    return;
  }
  File f = SD.open(path, FILE_READ);
  if (!f) { server.send(500, "text/plain", "Could not open daily log"); return; }
  server.streamFile(f, "text/plain");
  f.close();
}

void handleLogList() {
  server.send(200, "application/json", logListJson());
}

void handleLogData() {
  if (!sdReady) { server.send(404, "text/plain", "NO_SD_CARD"); return; }
  String name = server.hasArg("file") ? server.arg("file") : sessionLogPath;
  name.replace("\\", "/");
  name.replace("/log/", "");
  if (!nameLooksLikeLog(name)) { server.send(400, "text/plain", "BAD_LOG_NAME"); return; }
  String path = "/log/" + name;
  if (!SD.exists(path)) { server.send(404, "text/plain", "LOG_NOT_FOUND"); return; }
  File f = SD.open(path, FILE_READ);
  if (!f) { server.send(500, "text/plain", "Could not open log"); return; }
  server.streamFile(f, "text/plain");
  f.close();
}


String safeJsonString(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  return s;
}

void handleWifiScan() {
  logButtonPress("web wifi scan");
  wifiLastMessage = "Scanning for networks";
  if (setupApActive) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_STA);

  WiFi.scanDelete();
  vTaskDelay(120 / portTICK_PERIOD_MS);
  int n = WiFi.scanNetworks(false, true, false, 350);

  String j = "{\"message\":\"";
  if (n <= 0) j += "No networks found";
  else j += "Scan complete";
  j += "\",\"networks\":[";
  if (n < 0) n = 0;
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + safeJsonString(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"enc\":" + String((int)WiFi.encryptionType(i)) + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  wifiLastMessage = (n > 0) ? "Scan complete" : "No networks found";
  refreshWifiScreenNow();
  server.send(200, "application/json", j);
}

void handleWifiJoin() {
  if (!server.hasArg("ssid")) { server.send(400, "text/plain", "SSID_REQUIRED"); return; }
  String trySsid = server.arg("ssid");
  String tryPass = server.hasArg("pass") ? server.arg("pass") : "";

  logButtonPress("web wifi join background");
  if (!setupApActive) startSetupAp();
  if (webControlEnabled && !webServerRunning()) startWebServer();
  startBackgroundStaConnect(trySsid, tryPass, true);
  server.send(200, "text/plain", "CONNECTING_IN_BACKGROUND - local touchscreen and setup AP remain usable");
  refreshWifiScreenNow();
}


void handleWifiOff() {
  webControlEnabled = false;
  wifiLastMessage = "Webserver turned off";
  markSettingsDirty();
  server.send(200, "text/plain", "WEB_OFF");
  wifiShutdownRequested = true;
  refreshWifiScreenNow();
}

void handleWifiReset() {
  webControlEnabled = true;
  if (server.hasArg("clear")) {
    staSsid = "";
    staPass = "";
    wifiStaConnecting = false;
    pendingWifiSave = false;
    pendingWifiSsid = "";
    pendingWifiPass = "";
    WiFi.disconnect(true, true);
    markSettingsDirty();
    saveSettingsNow();
  }
  startSetupAp();
  if (!webServerRunning()) startWebServer();
  wifiLastMessage = "Setup AP active. Join Pluto9000-Setup";
  server.send(200, "text/plain", "SETUP_AP_ON");
  refreshWifiScreenNow();
}


void handleLogSnapshot() {
  if (!sdReady) { server.send(200, "text/plain", "NO_SD_CARD"); return; }
  if (!sdDataReady) { server.send(200, "text/plain", "SD_LOGGING_NOT_SETUP"); return; }
  logSdEvent("manual snapshot");
  server.send(200, "text/plain", "SNAPSHOT_LOGGED");
}

void handleSdInfo() {
  String j;
  j.reserve(220);
  j += "{";
  j += "\"sdReady\":" + String(sdReady ? "true" : "false") + ",";
  j += "\"sdDataReady\":" + String(sdDataReady ? "true" : "false") + ",";
  j += "\"statsExists\":" + String((sdReady && SD.exists("/stats.html")) ? "true" : "false") + ",";
  j += "\"logExists\":" + String((sdLoggingReady() && SD.exists(sessionLogPath)) ? "true" : "false") + ",";
  j += "\"events\":" + String(sdEventCount) + ",";
  j += "\"lastLogMs\":" + String(lastSdLogMs);
  j += "}";
  server.send(200, "application/json", j);
}


void handleSdFormat() {
  if (!sdReady) {
    server.send(200, "text/plain", "NO_SD_CARD");
    return;
  }
  if (!server.hasArg("confirm") || (server.arg("confirm") != "YES" && server.arg("confirm") != "FORMAT")) {
    server.send(200, "text/plain", "CONFIRM_REQUIRED");
    return;
  }

  // Arduino SD.h does not provide a safe low-level FAT formatter here.
  // This clears/recreates Pluto-generated files only.
  if (!SD.exists("/log")) SD.mkdir("/log");
  // Keep SD format safe: create a fresh log folder structure for Pluto-generated logs.
  // Recursive delete is intentionally avoided on embedded SD.h. Existing dated folders may remain.
  File root = SD.open("/log");
  root.close();
  if (SD.exists("/stats.html")) SD.remove("/stats.html");
  if (SD.exists("/pluto9000.cfg")) SD.remove("/pluto9000.cfg");

  sdEventCount = 0;
  lastSdLogMs = 0;
  beginSessionLog(true);
  logSdEvent("sd logging setup");
  saveSettingsNow();
  server.send(200, "text/plain", "SD_LOGGING_READY");
}


void startBackgroundStaConnect(String ssid, String pass, bool saveOnSuccess) {
  ssid.trim();
  if (ssid.length() == 0) {
    wifiLastMessage = "Local ready. No saved WiFi.";
    wifiStaConnecting = false;
    pendingWifiSave = false;
    refreshWifiScreenNow();
    return;
  }

  pendingWifiSsid = ssid;
  pendingWifiPass = pass;
  pendingWifiSave = saveOnSuccess;
  wifiStaConnecting = true;
  wifiStaConnectStartMs = millis();
  wifiLastMessage = "Trying " + ssid + " in background";

  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (setupApActive) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.begin(ssid.c_str(), pass.c_str());
  refreshWifiScreenNow();
}

void beginLocalFirstNetworking() {
  WiFi.persistent(false);
  WiFi.setSleep(false);
  // Always give the operator immediate local control and a phone-access setup portal.
  startSetupAp();
  if (webControlEnabled) startWebServer();
  if (staSsid.length()) {
    startBackgroundStaConnect(staSsid, staPass, false);
  } else {
    wifiLastMessage = "Local control ready. Use Pluto9000-Setup for WiFi.";
  }
  uiNeedsFullRedraw = true;
}

void updateWifiBackground() {
  if (!wifiStaConnecting) return;

  if (WiFi.status() == WL_CONNECTED) {
    wifiStaConnecting = false;
    if (pendingWifiSave) {
      staSsid = pendingWifiSsid;
      staPass = pendingWifiPass;
      pendingWifiSave = false;
      markSettingsDirty();
      saveSettingsNow();
    }
    wifiLastMessage = "LAN connected: " + WiFi.localIP().toString();
    configTime(-5 * 3600, 3600, "pool.ntp.org", "time.nist.gov");
    if (webControlEnabled && !webServerRunning()) startWebServer();
    logSdEvent("wifi background connected");
    uiNeedsFullRedraw = true;
    refreshWifiScreenNow();
    return;
  }

  if (millis() - wifiStaConnectStartMs > WIFI_BACKGROUND_TIMEOUT_MS) {
    wifiStaConnecting = false;
    wifiLastMessage = "Home WiFi not found. Local/AP still ready.";
    logSystemProblem("wifi background timeout");
    uiNeedsFullRedraw = true;
    refreshWifiScreenNow();
  }
}

void startSetupAp() {
  if (setupApActive) {
    wifiLastMessage = "Setup AP already active at " + setupApIp.toString();
    return;
  }
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(setupApIp, setupApIp, IPAddress(255, 255, 255, 0));
  bool ok = WiFi.softAP(SETUP_AP_SSID);
  setupApActive = ok;
  if (ok) {
    dnsServer.start(DNS_PORT, "*", setupApIp);
    wifiLastMessage = "Local ready. Setup AP on: Pluto9000-Setup";
    logSdEvent("setup ap started");
  } else {
    wifiLastMessage = "Setup AP failed to start";
    logSystemProblem("setup ap failed");
  }
}

void stopSetupAp() {
  if (!setupApActive) return;
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  setupApActive = false;
  wifiLastMessage = "Setup AP stopped";
}

void refreshWifiScreenNow() {
  if (currentScreen == SCREEN_WIFI) uiNeedsFullRedraw = true;
}

void startWebServer() {
  if (webServerRunning()) { refreshWifiScreenNow(); return; }
  if (WiFi.status() != WL_CONNECTED && !setupApActive) {
    startSetupAp();
  }
  if (serverPtr != nullptr) { delete serverPtr; serverPtr = nullptr; }
  serverPtr = new WebServer(webPort);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (!setupApActive && WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);

  server.on("/", handleRoot);
  server.on("/api/state", handleState);
  server.on("/api/set", handleSet);
  server.on("/api/run", handleRun);
  server.on("/api/rhythm", handleRhythmApi);
  server.on("/api/custom", handleCustomRhythmApi);
  server.on("/api/favorite", handleFavoriteApi);
  server.on("/api/save", handleSave);
  server.on("/api/estop", handleEstop);
  server.on("/api/release", handleRelease);
  server.on("/stats.html", handleStatsFile);
  server.on("/current.log", handleLogFile);
  server.on("/api/logsnap", handleLogSnapshot);
  server.on("/api/sdinfo", handleSdInfo);
  server.on("/api/sdformat", handleSdFormat);
  server.on("/api/wifioff", handleWifiOff);
  server.on("/api/wifireset", handleWifiReset);
  server.on("/api/wifiscan", handleWifiScan);
  server.on("/api/wifijoin", handleWifiJoin);
  server.on("/api/loglist", handleLogList);
  server.on("/api/logdata", handleLogData);
  server.onNotFound(handleRoot);
  server.begin();
  wifiActive = true;
  wifiShutdownRequested = false;
  wifiResetRequested = false;
  if (!wifiStaConnecting) wifiLastMessage = "Web portal ready at " + shareLink();
  refreshWifiScreenNow();
}

void stopWebServerNow() {
  if (serverPtr != nullptr) {
    server.stop();
    delete serverPtr;
    serverPtr = nullptr;
  }
  wifiActive = false;
  wifiShutdownRequested = false;
  refreshWifiScreenNow();
}

bool connectSavedWifi(uint32_t timeoutMs) {
  // Deprecated for v40: kept only to avoid old call-site breakage.
  // Do not block local control while WiFi is attempted.
  (void)timeoutMs;
  if (staSsid.length() == 0) return false;
  startBackgroundStaConnect(staSsid, staPass, false);
  return false;
}


void onWifiEvent(arduino_event_t *event) {
  if (event == nullptr) return;
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiStaConnecting = false;
      if (pendingWifiSave) {
        staSsid = pendingWifiSsid;
        staPass = pendingWifiPass;
        pendingWifiSave = false;
        markSettingsDirty();
        saveSettingsNow();
      }
      wifiLastMessage = "LAN connected: " + WiFi.localIP().toString();
      if (webControlEnabled && serverPtr == nullptr) startWebServer();
      uiNeedsFullRedraw = true;
      refreshWifiScreenNow();
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      if (wifiStaConnecting) wifiLastMessage = "Still trying home WiFi. Local control ready.";
      else if (setupApActive) wifiLastMessage = "LAN disconnected. Setup AP still available.";
      else wifiLastMessage = "WiFi disconnected. Local control ready.";
      uiNeedsFullRedraw = true;
      refreshWifiScreenNow();
      break;
    default:
      break;
  }
}

// ------------------ SETUP / LOOP ------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  bootMs = millis();

  configureOutputPins();
  loadSettings();
  WiFi.onEvent(onWifiEvent);
  initPalette();
  applyBacklight();

  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, SPI, 25000000);
  if (sdReady) {
    // v42: create logging automatically when an SD card is present.
    // Logs live under /log/YYYY-MM-DD/YYYY-MM-DD.log.
    beginSessionLog(false);
    logSdEvent("boot");
  } else {
    Serial.println("SD init failed");
  }
  for (int i = 0; i < LIVE_POINTS; i++) {
    liveVacHist[i] = (int)vacActual;
    livePpmHist[i] = ppmTarget;
    liveMotorHist[i] = motorPowerTarget;
  }

  bootAnimation();

  // Local-first boot: do not make the operator wait for WiFi.
  // The unit is usable immediately; saved LAN WiFi is attempted in the background.
  beginLocalFirstNetworking();

  uiNeedsFullRedraw = true;
}

void loop() {
  M5.update();
  if (webServerRunning()) {
    server.handleClient();
  }
  if (setupApActive) {
    dnsServer.processNextRequest();
  }
  if (wifiShutdownRequested) stopWebServerNow();
  if (wifiResetRequested) { wifiResetRequested = false; startSetupAp(); if (webControlEnabled) startWebServer(); uiNeedsFullRedraw = true; }
  updateWifiBackground();

  updateVacPhysics();
  updateProgramControl();
  updateRhythmDemo();
  writePeriodicSdSample();
  handleTouch();

  if (uiNeedsFullRedraw) drawCurrentScreen();
  updateActiveScreenRegions();
  flushSettingsIfIdle();

  // Tiny RTOS yield. No Arduino delay() here.
  vTaskDelay(1);
}
