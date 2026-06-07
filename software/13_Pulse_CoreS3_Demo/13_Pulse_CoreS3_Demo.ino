#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <math.h>

// =====================================================
//  PulseCore / Pluto 9000 - CoreS3/SE Demo Iteration 13
//  Screen + web UI mockup only. No hardware outputs fire.
// =====================================================

// ------------------ GLOBAL APP STATE ------------------
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
M5Canvas bootSprite(&M5.Display);

const char* AP_NAME = "Pluto9000";
const char* AP_PASS = "pluto9000";   // WPA2 password for bench demo
const byte DNS_PORT = 53;

const int SCREEN_HOME = 0;
const int SCREEN_VAC = 1;
const int SCREEN_PPM = 2;
const int SCREEN_LIVE = 3;
const int SCREEN_MORE = 4;
const int SCREEN_SETTINGS = 5;
const int SCREEN_WIFI = 6;
const int SCREEN_DIAG = 7;
const int SCREEN_ESTOP_CONFIRM = 8;
const int SCREEN_ESTOP_ACTIVE = 9;

int currentScreen = SCREEN_HOME;
int previousScreen = SCREEN_HOME;

bool darkTheme = true;
bool largeButtons = true;
bool running = false;
bool estopLatched = false;
bool webControlEnabled = true;

bool showVac = true;
bool showPpm = true;
bool showLive = true;
bool showDiag = true;
bool showSettings = true;
bool showWifi = true;

int vacTarget = 45;           // 0-100%
float vacActual = 0.0f;       // simulated slow vacuum response
int ppmTarget = 24;           // 0-60 PPM; 0 = off
int backlightStep = 7;        // 1-10
int vacRampSpeed = 5;         // 1-10; lower = slower reaction
int screenScale = 2;          // 1-3; demo setting for future layout tuning
int graphSpeed = 5;           // 1-10; web-visible demo setting
int releaseMs = 1200;         // future emergency release timing setting

bool sdReady = false;
bool settingsDirty = false;
unsigned long lastSettingsChangeMs = 0;
const unsigned long SAVE_IDLE_MS = 900;

bool uiNeedsFullRedraw = true;
bool vacValueDirty = true;
bool ppmValueDirty = true;
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
const unsigned long FAST_AFTER_MS = 3000;

// Colors assigned after M5.begin() and after settings load.
uint16_t C_BG, C_PANEL, C_PANEL2, C_TEXT, C_MUTED, C_ACCENT, C_ACCENT2;
uint16_t C_WARN, C_DANGER, C_GOOD, C_LINE, C_GRID, C_BUTTON, C_BUTTON2;

// ------------------ BASIC HELPERS ------------------
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool insideRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

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

void drawHeader(const char* title) {
  M5.Display.fillRect(0, 0, 320, 34, C_PANEL);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  M5.Display.setCursor(9, 9);
  M5.Display.print(title);
  if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_ACTIVE) {
    drawButton(246, 3, 70, 28, "STOP", C_DANGER, C_DANGER, TFT_WHITE, 2);
  }
}

void drawBackButton() {
  drawButton(4, 207, 72, 29, "BACK", C_PANEL2, C_GRID, C_TEXT, 2);
}

// ------------------ SETTINGS SAVE / LOAD ------------------
void saveSettingsNow() {
  prefs.begin("pluto9000", false);
  prefs.putInt("vac", vacTarget);
  prefs.putInt("ppm", ppmTarget);
  prefs.putBool("dark", darkTheme);
  prefs.putInt("bl", backlightStep);
  prefs.putInt("ramp", vacRampSpeed);
  prefs.putInt("gspd", graphSpeed);
  prefs.putInt("rel", releaseMs);
  prefs.putBool("large", largeButtons);
  prefs.putBool("web", webControlEnabled);
  prefs.putBool("shVac", showVac);
  prefs.putBool("shPpm", showPpm);
  prefs.putBool("shLive", showLive);
  prefs.putBool("shDiag", showDiag);
  prefs.putBool("shSet", showSettings);
  prefs.putBool("shWifi", showWifi);
  prefs.putInt("scale", screenScale);
  prefs.end();

  if (sdReady) {
    SD.remove("/pluto9000.cfg");
    File f = SD.open("/pluto9000.cfg", FILE_WRITE);
    if (f) {
      f.println("Pluto9000 configuration backup");
      f.print("vac="); f.println(vacTarget);
      f.print("ppm="); f.println(ppmTarget);
      f.print("dark="); f.println(darkTheme ? 1 : 0);
      f.print("backlightStep="); f.println(backlightStep);
      f.print("vacRampSpeed="); f.println(vacRampSpeed);
      f.print("graphSpeed="); f.println(graphSpeed);
      f.print("releaseMs="); f.println(releaseMs);
      f.print("largeButtons="); f.println(largeButtons ? 1 : 0);
      f.print("webControlEnabled="); f.println(webControlEnabled ? 1 : 0);
      f.print("showVac="); f.println(showVac ? 1 : 0);
      f.print("showPpm="); f.println(showPpm ? 1 : 0);
      f.print("showLive="); f.println(showLive ? 1 : 0);
      f.print("showDiag="); f.println(showDiag ? 1 : 0);
      f.print("showSettings="); f.println(showSettings ? 1 : 0);
      f.print("showWifi="); f.println(showWifi ? 1 : 0);
      f.print("screenScale="); f.println(screenScale);
      f.close();
    }
  }

  settingsDirty = false;
}

void flushSettingsIfIdle() {
  if (settingsDirty && millis() - lastSettingsChangeMs >= SAVE_IDLE_MS) {
    saveSettingsNow();
  }
}

void loadSettings() {
  prefs.begin("pluto9000", true);
  vacTarget = prefs.getInt("vac", 45);
  ppmTarget = prefs.getInt("ppm", 24);
  darkTheme = prefs.getBool("dark", true);
  backlightStep = prefs.getInt("bl", 7);
  vacRampSpeed = prefs.getInt("ramp", 5);
  graphSpeed = prefs.getInt("gspd", 5);
  releaseMs = prefs.getInt("rel", 1200);
  largeButtons = prefs.getBool("large", true);
  webControlEnabled = prefs.getBool("web", true);
  showVac = prefs.getBool("shVac", true);
  showPpm = prefs.getBool("shPpm", true);
  showLive = prefs.getBool("shLive", true);
  showDiag = prefs.getBool("shDiag", true);
  showSettings = prefs.getBool("shSet", true);
  showWifi = prefs.getBool("shWifi", true);
  screenScale = prefs.getInt("scale", 2);
  prefs.end();

  vacTarget = clampInt(vacTarget, 0, 100);
  ppmTarget = clampInt(ppmTarget, 0, 60);
  backlightStep = clampInt(backlightStep, 1, 10);
  vacRampSpeed = clampInt(vacRampSpeed, 1, 10);
  graphSpeed = clampInt(graphSpeed, 1, 10);
  screenScale = clampInt(screenScale, 1, 3);
  releaseMs = clampInt(releaseMs, 250, 5000);
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

  // Label
  s.fillRoundRect(cx - 24, cy + 3, 48, 23, 7, M5.Display.color565(45, 140, 210));
  s.setTextSize(2);
  s.setTextColor(TFT_WHITE, M5.Display.color565(45, 140, 210));
  int labelW = s.textWidth("milky");
  s.setCursor(cx - labelW / 2, cy + 7);
  s.print("milky");

  // Eyes
  s.fillCircle(cx - 12, cy - 22, 8, TFT_BLACK);
  s.fillCircle(cx + 12, cy - 22, 8, TFT_BLACK);
  s.fillCircle(cx - 12 + eyeDir, cy - 22, 3, TFT_WHITE);
  s.fillCircle(cx + 12 + eyeDir, cy - 22, 3, TFT_WHITE);
  s.drawArc(cx, cy - 8, 12, 9, 20, 160, TFT_BLACK);
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

  // Milky flies in. Boot animation intentionally owns the screen before Wi-Fi starts.
  for (int x = -55; x <= 160; x += 7) {
    bootSprite.fillScreen(C_BG);
    drawMilkyToSprite(bootSprite, x, 132, 0);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(18 / portTICK_PERIOD_MS);
  }

  // Milky looks around while Pluto drops in and fits the screen.
  for (int i = 0; i < 90; i++) {
    int plutoY = map(i, 0, 89, -80, 14);
    int eyeDir = 0;
    if (i > 16 && i < 34) eyeDir = 4;
    if (i >= 34 && i < 55) eyeDir = -4;
    if (i >= 55) eyeDir = 4;
    drawPlutoDropFrame(plutoY, eyeDir);
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }

  for (int x = 160; x < 380; x += 8) {
    bootSprite.fillScreen(C_BG);
    bootSprite.setTextSize(4);
    bootSprite.setTextColor(C_ACCENT2, C_BG);
    int tw = bootSprite.textWidth("PLUTO");
    bootSprite.setCursor((320 - tw) / 2, 14);
    bootSprite.print("PLUTO");
    bootSprite.setTextSize(3);
    bootSprite.setTextColor(C_TEXT, C_BG);
    tw = bootSprite.textWidth("9000");
    bootSprite.setCursor((320 - tw) / 2, 50);
    bootSprite.print("9000");
    drawMilkyToSprite(bootSprite, x, 132, 0);
    bootSprite.pushSprite(0, 0);
    vTaskDelay(16 / portTICK_PERIOD_MS);
  }

  bootSprite.deleteSprite();
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
  vacActual += ((float)vacTarget - vacActual) * response;
  if (fabsf(vacActual - (float)vacTarget) < 0.12f) vacActual = (float)vacTarget;
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
  int x = 12, y = 72, w = 296, h = 98;
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
  int x = 12, y = 72, w = 296, h = 98;
  int gx = x + 10, gy = y + 20, gw = w - 20, gh = h - 32;
  M5.Display.fillRect(gx, gy, gw, gh, C_PANEL);
  for (int i = 0; i <= 3; i++) {
    int yy = gy + (gh * i) / 3;
    M5.Display.drawLine(gx, yy, gx + gw, yy, C_GRID);
  }

  int lastX = gx;
  int lastY = gy + gh;
  unsigned long now = millis();
  // Draw historical waveform from left to right. Higher PPM means more humps fit across the same width.
  for (int px = 0; px < gw; px++) {
    unsigned long sampleT = now - (unsigned long)((gw - px) * (35 - graphSpeed * 2));
    float v = ppmWaveValue(sampleT);
    int yy = gy + gh - (int)(v * (gh - 7));
    if (ppmTarget <= 0) yy = gy + gh;
    if (px > 0) M5.Display.drawLine(lastX, lastY, gx + px, yy, C_ACCENT);
    lastX = gx + px;
    lastY = yy;
  }
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

// ------------------ SCREEN DRAWING ------------------
void refreshVacValue() {
  M5.Display.fillRoundRect(17, 39, 128, 29, 8, C_PANEL2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(27, 46);
  M5.Display.print("VAC ");
  M5.Display.print(vacTarget);
  M5.Display.print("%");
  vacValueDirty = false;
}

void refreshPpmValue() {
  M5.Display.fillRoundRect(17, 39, 150, 29, 8, C_PANEL2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(27, 46);
  M5.Display.print("PPM ");
  M5.Display.print(ppmTarget);
  ppmValueDirty = false;
}

void drawHomeScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Pluto 9000");

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 42);
  M5.Display.print("VAC "); M5.Display.print(vacTarget); M5.Display.print("%   ");
  M5.Display.print("PPM "); M5.Display.print(ppmTarget);

  drawButton(10, 70, 145, 68, running ? "STOP" : "START", running ? C_WARN : C_GOOD, running ? C_WARN : C_GOOD, TFT_BLACK, 3);

  if (showVac) drawButton(165, 70, 145, 68, "VAC", C_BUTTON2, C_ACCENT2, C_TEXT, 3);
  if (showPpm) drawButton(10, 148, 145, 68, "PPM", C_BUTTON, C_ACCENT, C_TEXT, 3);
  drawButton(165, 148, 145, 68, "MORE", C_PANEL2, C_GRID, C_TEXT, 3);
}

void drawVacScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("VAC Control");
  refreshVacValue();
  drawGraphFrame(12, 72, 296, 98, "0-100% ramp to set point", true);
  updateVacGraph();
  drawButton(19, 178, 132, 53, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButton(169, 178, 132, 53, "+", C_BUTTON2, C_ACCENT2, C_TEXT, 5);
}

void drawPpmScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("PPM Control");
  refreshPpmValue();
  drawGraphFrame(12, 72, 296, 98, "pulse frequency per minute", false);
  updatePpmGraph();
  drawButton(19, 178, 132, 53, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButton(169, 178, 132, 53, "+", C_BUTTON2, C_ACCENT, C_TEXT, 5);
}

void drawLiveScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Live View");
  drawHorizontalBar(14, 50, 292, 75, ppmTarget, 60, "PPM", C_ACCENT);
  drawHorizontalBar(14, 138, 292, 75, (int)vacActual, 100, "VAC", C_ACCENT2);
  drawBackButton();
}

void drawMoreScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("More");
  int y1 = 50;
  if (showLive) drawButton(10, y1, 145, 54, "LIVE", C_BUTTON, C_GRID, C_TEXT, 3);
  if (showDiag) drawButton(165, y1, 145, 54, "TEST", C_BUTTON, C_GRID, C_TEXT, 3);
  if (showSettings) drawButton(10, 114, 145, 54, "SET", C_PANEL2, C_GRID, C_TEXT, 3);
  if (showWifi) drawButton(165, 114, 145, 54, "WIFI", C_PANEL2, C_GRID, C_TEXT, 3);
  drawBackButton();
}

void drawSettingsScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Settings");

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(14, 46);
  M5.Display.print("Theme: "); M5.Display.print(darkTheme ? "Dark" : "Light");
  drawButton(205, 39, 100, 38, "SWAP", C_BUTTON, C_GRID, C_TEXT, 2);

  M5.Display.setCursor(14, 91);
  M5.Display.print("Backlight");
  int sx = 14, sy = 119, sw = 292, sh = 24;
  M5.Display.fillRoundRect(sx, sy, sw, sh, 6, C_PANEL2);
  for (int i = 1; i <= 10; i++) {
    int tickX = sx + map(i, 1, 10, 8, sw - 8);
    M5.Display.drawLine(tickX, sy + 3, tickX, sy + sh - 3, C_GRID);
  }
  int knobX = sx + map(backlightStep, 1, 10, 8, sw - 8);
  M5.Display.fillCircle(knobX, sy + sh / 2, 11, C_ACCENT2);

  drawButton(18, 157, 130, 44, "BL -", C_BUTTON, C_GRID, C_TEXT, 2);
  drawButton(172, 157, 130, 44, "BL +", C_BUTTON2, C_ACCENT2, C_TEXT, 2);
  drawBackButton();
}

void drawWifiScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Web Control");
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(12, 48);
  M5.Display.print("WiFi: "); M5.Display.print(AP_NAME);
  M5.Display.setCursor(12, 78);
  M5.Display.print("Pass: "); M5.Display.print(AP_PASS);
  M5.Display.setCursor(12, 108);
  M5.Display.print("Open:");
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_ACCENT2, C_BG);
  M5.Display.setCursor(30, 138);
  M5.Display.print("192.168.4.1");
  drawBackButton();
}

void drawDiagScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Manual Test");
  drawButton(12, 48, 140, 52, "PUMP", C_BUTTON, C_GRID, C_TEXT, 2);
  drawButton(168, 48, 140, 52, "PULSE", C_BUTTON, C_GRID, C_TEXT, 2);
  drawButton(12, 112, 140, 52, "VAC", C_BUTTON2, C_GRID, C_TEXT, 2);
  drawButton(168, 112, 140, 52, "RELEASE", C_WARN, C_WARN, TFT_BLACK, 2);
  drawBackButton();
}

void drawEstopConfirmScreen() {
  M5.Display.fillScreen(C_DANGER);
  M5.Display.setTextSize(4);
  M5.Display.setTextColor(TFT_WHITE, C_DANGER);
  int tw = M5.Display.textWidth("E-STOP");
  M5.Display.setCursor((320 - tw) / 2, 52);
  M5.Display.print("E-STOP");
  M5.Display.setTextSize(2);
  M5.Display.setCursor(31, 113);
  M5.Display.print("PRESS RED SCREEN TO RELEASE");
  M5.Display.setCursor(76, 139);
  M5.Display.print("ALL PRESSURE");
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
  M5.Display.setTextSize(2);
  M5.Display.setCursor(40, 125);
  M5.Display.print("POSITIVE + NEGATIVE OFF");
  M5.Display.fillRect(0, 216, 320, 24, C_GOOD);
  drawCenteredText("RETURN", 0, 216, 320, 24, 2, TFT_BLACK, C_GOOD);
}

void drawCurrentScreen() {
  initPalette();
  if (currentScreen == SCREEN_HOME) drawHomeScreen();
  else if (currentScreen == SCREEN_VAC) drawVacScreen();
  else if (currentScreen == SCREEN_PPM) drawPpmScreen();
  else if (currentScreen == SCREEN_LIVE) drawLiveScreen();
  else if (currentScreen == SCREEN_MORE) drawMoreScreen();
  else if (currentScreen == SCREEN_SETTINGS) drawSettingsScreen();
  else if (currentScreen == SCREEN_WIFI) drawWifiScreen();
  else if (currentScreen == SCREEN_DIAG) drawDiagScreen();
  else if (currentScreen == SCREEN_ESTOP_CONFIRM) drawEstopConfirmScreen();
  else if (currentScreen == SCREEN_ESTOP_ACTIVE) drawEstopActiveScreen();
  uiNeedsFullRedraw = false;
}

void performEmergencyRelease() {
  running = false;
  estopLatched = true;
  vacTarget = 0;
  ppmTarget = 0;
  vacActual = 0;
  markSettingsDirty();
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
  ppmTarget = clampInt(ppmTarget + delta, 0, 60);
  if (old != ppmTarget) {
    ppmValueDirty = true;
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
  if (holdControl == 1) changeVac(held >= FAST_AFTER_MS ? -10 : -2);
  if (holdControl == 2) changeVac(held >= FAST_AFTER_MS ? 10 : 2);
  if (holdControl == 3) changePpm(held >= FAST_AFTER_MS ? -10 : -1);
  if (holdControl == 4) changePpm(held >= FAST_AFTER_MS ? 10 : 1);
  if (holdControl == 5) changeBacklight(-1);
  if (holdControl == 6) changeBacklight(1);
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

// ------------------ TOUCH HANDLING ------------------
void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    int x = t.x;
    int y = t.y;

    if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_ACTIVE && insideRect(x, y, 246, 3, 70, 28)) {
      setScreen(SCREEN_ESTOP_CONFIRM);
      return;
    }

    if (currentScreen == SCREEN_HOME) {
      if (insideRect(x, y, 10, 70, 145, 68)) { running = !running; uiNeedsFullRedraw = true; return; }
      if (showVac && insideRect(x, y, 165, 70, 145, 68)) { setScreen(SCREEN_VAC); return; }
      if (showPpm && insideRect(x, y, 10, 148, 145, 68)) { setScreen(SCREEN_PPM); return; }
      if (insideRect(x, y, 165, 148, 145, 68)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_VAC) {
      if (insideRect(x, y, 19, 178, 132, 53)) beginHold(1);
      else if (insideRect(x, y, 169, 178, 132, 53)) beginHold(2);
    }

    if (currentScreen == SCREEN_PPM) {
      if (insideRect(x, y, 19, 178, 132, 53)) beginHold(3);
      else if (insideRect(x, y, 169, 178, 132, 53)) beginHold(4);
    }

    if (currentScreen == SCREEN_MORE) {
      if (showLive && insideRect(x, y, 10, 50, 145, 54)) { setScreen(SCREEN_LIVE); return; }
      if (showDiag && insideRect(x, y, 165, 50, 145, 54)) { setScreen(SCREEN_DIAG); return; }
      if (showSettings && insideRect(x, y, 10, 114, 145, 54)) { setScreen(SCREEN_SETTINGS); return; }
      if (showWifi && insideRect(x, y, 165, 114, 145, 54)) { setScreen(SCREEN_WIFI); return; }
      if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_HOME); return; }
    }

    if (currentScreen == SCREEN_SETTINGS) {
      if (insideRect(x, y, 205, 39, 100, 38)) {
        darkTheme = !darkTheme;
        initPalette();
        uiNeedsFullRedraw = true;
        markSettingsDirty();
        return;
      }
      if (insideRect(x, y, 18, 157, 130, 44)) beginHold(5);
      else if (insideRect(x, y, 172, 157, 130, 44)) beginHold(6);
      else if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_LIVE || currentScreen == SCREEN_WIFI || currentScreen == SCREEN_DIAG) {
      if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_ESTOP_CONFIRM) {
      if (insideRect(x, y, 0, 216, 320, 24)) { setScreen(previousScreen); return; }
      performEmergencyRelease();
      return;
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
  if (currentScreen == SCREEN_LIVE && now - lastLiveMs >= 220) {
    lastLiveMs = now;
    drawHorizontalBar(14, 50, 292, 75, ppmTarget, 60, "PPM", C_ACCENT);
    drawHorizontalBar(14, 138, 292, 75, (int)vacActual, 100, "VAC", C_ACCENT2);
    drawBackButton();
  }
  if (currentScreen == SCREEN_SETTINGS && settingsValueDirty) {
    uiNeedsFullRedraw = true;  // backlight slider is static enough; redraw only after a step
    settingsValueDirty = false;
  }
}

// ------------------ WEB PAGE BUILDING ------------------
String checked(bool b) { return b ? "checked" : ""; }

String htmlPage() {
  String h;
  h.reserve(19000);
  h += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>");
  h += F("<title>Pluto 9000</title><style>");
  h += F(":root{--bg:#090d14;--card:#141c29;--card2:#1d2b3a;--text:#f2f7ff;--muted:#9daabd;--blue:#40aaff;--green:#78e6be;--red:#e62630;--yellow:#ffbc4b}");
  h += F("*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#17253a,#090d14 55%);color:var(--text);font-family:Arial,Helvetica,sans-serif}header{padding:22px 18px;background:#0d1420;border-bottom:1px solid #263245;position:sticky;top:0;z-index:5}h1{margin:0;font-size:32px;letter-spacing:.5px}.sub{color:var(--muted);font-size:15px;margin-top:6px}.wrap{padding:14px;max-width:980px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px}.card{background:linear-gradient(180deg,var(--card),#101723);border:1px solid #263245;border-radius:18px;padding:16px;box-shadow:0 12px 28px rgba(0,0,0,.25)}h2{margin:0 0 12px;font-size:22px}.big{font-size:44px;font-weight:800;line-height:1}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.label{color:var(--muted);font-size:14px;margin:10px 0 6px}input[type=range]{width:100%;accent-color:var(--green)}input[type=number],select{width:100%;background:#0b111b;border:1px solid #304158;color:var(--text);border-radius:12px;padding:12px;font-size:18px}button{border:0;border-radius:14px;padding:14px 16px;font-size:18px;font-weight:800;color:#081019;background:var(--green);min-height:52px}.ghost{background:#223247;color:var(--text);border:1px solid #34465f}.danger{background:var(--red);color:white}.warn{background:var(--yellow);color:#140c00}.pill{display:inline-block;border-radius:999px;background:#223247;color:var(--muted);padding:8px 12px;margin:4px 4px 0 0}.toggle{display:flex;align-items:center;justify-content:space-between;gap:12px;border:1px solid #2e3d52;border-radius:14px;padding:12px;margin:8px 0;background:#101723}.toggle input{width:24px;height:24px}.share{font-size:20px;background:#0b111b;border:1px solid #304158;padding:13px;border-radius:14px;word-break:break-all}.meter{height:22px;border-radius:999px;background:#243145;overflow:hidden;margin-top:8px}.fill{height:100%;background:linear-gradient(90deg,var(--blue),var(--green));width:0%}.mini{font-size:13px;color:var(--muted)}nav{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}nav a{color:var(--text);text-decoration:none;background:#1a2637;padding:10px 13px;border-radius:999px;border:1px solid #2b3d55}</style></head><body>");
  h += F("<header><h1>Pluto 9000</h1><div class='sub'>PulseCore Web Control · AP: Pluto9000 · IP: 192.168.4.1</div><nav><a href='#dash'>Dashboard</a><a href='#control'>Controls</a><a href='#layout'>Layout</a><a href='#safety'>Safety</a><a href='#system'>System</a></nav></header><div class='wrap'>");

  h += F("<section id='dash' class='grid'><div class='card'><h2>Live VAC</h2><div class='big'><span id='vacNow'>"); h += String((int)vacActual); h += F("</span>%</div><div class='label'>Target: <span id='vacTarget'>"); h += String(vacTarget); h += F("</span>%</div><div class='meter'><div id='vacFill' class='fill'></div></div></div>");
  h += F("<div class='card'><h2>Live PPM</h2><div class='big'><span id='ppmNow'>"); h += String(ppmTarget); h += F("</span></div><div class='label'>Pulses per minute</div><div class='meter'><div id='ppmFill' class='fill'></div></div></div>");
  h += F("<div class='card'><h2>Run State</h2><p><span class='pill' id='runPill'>"); h += running ? "RUNNING" : "STOPPED"; h += F("</span><span class='pill' id='sdPill'>SD: "); h += sdReady ? "READY" : "NOT FOUND"; h += F("</span></p><div class='row'><button onclick='api(\"/api/run?state=1\")'>START</button><button class='ghost' onclick='api(\"/api/run?state=0\")'>STOP</button><button class='danger' onclick='api(\"/api/estop\")'>E-STOP</button></div></div></section>");

  h += F("<section id='control' class='grid'><div class='card'><h2>VAC Control</h2><div class='label'>Set point 0-100%</div><input id='vac' type='range' min='0' max='100' value='"); h += String(vacTarget); h += F("' oninput='liveNum(\"vacNum\",this.value)' onchange='setVal(\"vac\",this.value)'><div class='big'><span id='vacNum'>"); h += String(vacTarget); h += F("</span>%</div><div class='label'>VAC ramp speed</div><input id='ramp' type='range' min='1' max='10' value='"); h += String(vacRampSpeed); h += F("' onchange='setVal(\"ramp\",this.value)'></div>");
  h += F("<div class='card'><h2>PPM Control</h2><div class='label'>0 = off · 1 PPM = 30 sec up / 30 sec down · 60 PPM = .5 sec up / .5 sec down</div><input id='ppm' type='range' min='0' max='60' value='"); h += String(ppmTarget); h += F("' oninput='liveNum(\"ppmNum\",this.value)' onchange='setVal(\"ppm\",this.value)'><div class='big'><span id='ppmNum'>"); h += String(ppmTarget); h += F("</span></div><div class='label'>Graph speed</div><input type='range' min='1' max='10' value='"); h += String(graphSpeed); h += F("' onchange='setVal(\"graph\",this.value)'></div>");
  h += F("<div class='card'><h2>Presets</h2><div class='row'><button onclick='preset(15,25)'>Soft</button><button onclick='preset(24,45)'>Balanced</button><button onclick='preset(36,65)'>Strong</button><button class='ghost' onclick='preset(0,0)'>Off</button></div><p class='mini'>Preset buttons update the device and save after you press Save.</p></div></section>");

  h += F("<section id='layout' class='grid'><div class='card'><h2>Display</h2>");
  h += F("<div class='toggle'><span>Dark mode</span><input type='checkbox' "); h += checked(darkTheme); h += F(" onchange='setVal(\"dark\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>Large buttons</span><input type='checkbox' "); h += checked(largeButtons); h += F(" onchange='setVal(\"large\",this.checked?1:0)'></div>");
  h += F("<div class='label'>Backlight step 1-10</div><input type='range' min='1' max='10' value='"); h += String(backlightStep); h += F("' onchange='setVal(\"bl\",this.value)'><div class='label'>Screen scale</div><select onchange='setVal(\"scale\",this.value)'><option value='1'>Compact</option><option value='2' selected>Normal</option><option value='3'>Huge</option></select></div>");

  h += F("<div class='card'><h2>Visible Screens</h2>");
  h += F("<div class='toggle'><span>VAC screen</span><input type='checkbox' "); h += checked(showVac); h += F(" onchange='setVal(\"showVac\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>PPM screen</span><input type='checkbox' "); h += checked(showPpm); h += F(" onchange='setVal(\"showPpm\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>Live screen</span><input type='checkbox' "); h += checked(showLive); h += F(" onchange='setVal(\"showLive\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>Manual Test screen</span><input type='checkbox' "); h += checked(showDiag); h += F(" onchange='setVal(\"showDiag\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>Settings screen</span><input type='checkbox' "); h += checked(showSettings); h += F(" onchange='setVal(\"showSettings\",this.checked?1:0)'></div>");
  h += F("<div class='toggle'><span>WiFi screen</span><input type='checkbox' "); h += checked(showWifi); h += F(" onchange='setVal(\"showWifi\",this.checked?1:0)'></div></div></section>");

  h += F("<section id='safety' class='grid'><div class='card'><h2>Safety</h2><div class='label'>Emergency release timing ms</div><input type='number' min='250' max='5000' value='"); h += String(releaseMs); h += F("' onchange='setVal(\"release\",this.value)'><p class='mini'>Demo only. Final hardware version will force pump off and open/close the correct valves to release positive and negative pressure immediately.</p><button class='danger' onclick='api(\"/api/estop\")'>TRIGGER E-STOP SCREEN</button><button class='warn' onclick='api(\"/api/release\")'>SIM RELEASE</button></div>");
  h += F("<div class='card'><h2>Share Control</h2><div class='share'>http://192.168.4.1</div><p class='mini'>Connect to WiFi network Pluto9000, then open this IP from any browser on the same device.</p><button onclick='copyLink()'>COPY LINK</button></div></section>");

  h += F("<section id='system' class='grid'><div class='card'><h2>System</h2><p><span class='pill'>Heap: <span id='heap'>"); h += String(ESP.getFreeHeap()); h += F("</span></span><span class='pill'>Uptime: <span id='uptime'>0</span>s</span><span class='pill'>Saved: <span id='saved'>"); h += settingsDirty ? "NO" : "YES"; h += F("</span></span></p><div class='row'><button onclick='api(\"/api/save\")'>SAVE TO DEVICE + SD</button><button class='ghost' onclick='api(\"/api/reload\")'>RELOAD UI</button></div></div></section>");

  h += F("</div><script>");
  h += F("function liveNum(id,v){document.getElementById(id).innerText=v}function api(u){return fetch(u).then(r=>r.text()).then(()=>poll())}function setVal(k,v){return api('/api/set?'+k+'='+encodeURIComponent(v))}function preset(ppm,vac){document.getElementById('ppm').value=ppm;document.getElementById('vac').value=vac;liveNum('ppmNum',ppm);liveNum('vacNum',vac);api('/api/set?ppm='+ppm+'&vac='+vac)}function copyLink(){navigator.clipboard&&navigator.clipboard.writeText('http://192.168.4.1')}function poll(){fetch('/api/state').then(r=>r.json()).then(s=>{vacNow.innerText=s.vacActual;vacTarget.innerText=s.vac;ppmNow.innerText=s.ppm;vacFill.style.width=s.vacActual+'%';ppmFill.style.width=(s.ppm/60*100)+'%';runPill.innerText=s.running?'RUNNING':'STOPPED';heap.innerText=s.heap;uptime.innerText=s.uptime;saved.innerText=s.saved?'YES':'NO';})}setInterval(poll,1000);poll();</script></body></html>");
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleState() {
  String j;
  j.reserve(280);
  j += "{";
  j += "\"vac\":" + String(vacTarget) + ",";
  j += "\"vacActual\":" + String((int)vacActual) + ",";
  j += "\"ppm\":" + String(ppmTarget) + ",";
  j += "\"running\":" + String(running ? "true" : "false") + ",";
  j += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  j += "\"uptime\":" + String((millis() - bootMs) / 1000) + ",";
  j += "\"saved\":" + String(settingsDirty ? "false" : "true");
  j += "}";
  server.send(200, "application/json", j);
}

void handleSet() {
  bool paletteChanged = false;
  bool layoutChanged = false;

  if (server.hasArg("vac")) { vacTarget = clampInt(server.arg("vac").toInt(), 0, 100); vacValueDirty = true; liveValueDirty = true; }
  if (server.hasArg("ppm")) { ppmTarget = clampInt(server.arg("ppm").toInt(), 0, 60); ppmValueDirty = true; liveValueDirty = true; }
  if (server.hasArg("ramp")) vacRampSpeed = clampInt(server.arg("ramp").toInt(), 1, 10);
  if (server.hasArg("graph")) graphSpeed = clampInt(server.arg("graph").toInt(), 1, 10);
  if (server.hasArg("release")) releaseMs = clampInt(server.arg("release").toInt(), 250, 5000);
  if (server.hasArg("bl")) { backlightStep = clampInt(server.arg("bl").toInt(), 1, 10); applyBacklight(); }
  if (server.hasArg("dark")) { darkTheme = server.arg("dark").toInt() == 1; paletteChanged = true; layoutChanged = true; }
  if (server.hasArg("large")) { largeButtons = server.arg("large").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("scale")) { screenScale = clampInt(server.arg("scale").toInt(), 1, 3); layoutChanged = true; }
  if (server.hasArg("showVac")) { showVac = server.arg("showVac").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showPpm")) { showPpm = server.arg("showPpm").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showLive")) { showLive = server.arg("showLive").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showDiag")) { showDiag = server.arg("showDiag").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showSettings")) { showSettings = server.arg("showSettings").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("showWifi")) { showWifi = server.arg("showWifi").toInt() == 1; layoutChanged = true; }

  if (paletteChanged) initPalette();
  if (layoutChanged) uiNeedsFullRedraw = true;
  markSettingsDirty();
  server.send(200, "text/plain", "OK");
}

void handleRun() {
  if (server.hasArg("state")) running = server.arg("state").toInt() == 1;
  uiNeedsFullRedraw = true;
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

void startWebServer() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASS);
  vTaskDelay(150 / portTICK_PERIOD_MS);
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/api/state", handleState);
  server.on("/api/set", handleSet);
  server.on("/api/run", handleRun);
  server.on("/api/save", handleSave);
  server.on("/api/estop", handleEstop);
  server.on("/api/release", handleRelease);
  server.on("/api/reload", handleRoot);

  // Common captive portal detection URLs
  server.on("/generate_204", handleRoot);
  server.on("/gen_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/fwlink", handleRoot);
  server.onNotFound(handleRoot);
  server.begin();
}

// ------------------ SETUP / LOOP ------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  bootMs = millis();

  loadSettings();
  initPalette();
  applyBacklight();

  sdReady = SD.begin();

  bootAnimation();
  pulseCoreSplash();

  // Start Wi-Fi after boot animation so clients do not connect before loop services HTTP/DNS.
  startWebServer();

  uiNeedsFullRedraw = true;
}

void loop() {
  M5.update();
  dnsServer.processNextRequest();
  server.handleClient();

  updateVacPhysics();
  handleTouch();

  if (uiNeedsFullRedraw) drawCurrentScreen();
  updateActiveScreenRegions();
  flushSettingsIfIdle();

  // Tiny RTOS yield. No Arduino delay() here.
  vTaskDelay(1);
}
