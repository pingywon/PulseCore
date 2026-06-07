#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>
#include <math.h>

// =============================================================
// Pluto 9000 / PulseCore - Screen + Web Demo
// Version: 12_Pulse_CoreS3_Demo
// Simulation only. No real outputs are fired in this sketch.
// =============================================================

// ---------- App state declared before all functions ----------
const int SCREEN_HOME = 0;
const int SCREEN_VAC = 1;
const int SCREEN_PPM = 2;
const int SCREEN_LIVE = 3;
const int SCREEN_SETTINGS = 4;
const int SCREEN_WEB = 5;
const int SCREEN_MENU = 6;
const int SCREEN_ESTOP_CONFIRM = 7;
const int SCREEN_ESTOP_ACTIVE = 8;

int currentScreen = SCREEN_HOME;
int previousScreen = SCREEN_HOME;

int vacTarget = 42;                 // 0-100%
float vacActual = 0.0;              // simulated real vacuum response
int ppmTarget = 18;                 // 0-60; 0 = off
bool simRunning = false;
bool darkTheme = true;
int backlightStep = 8;              // 1-10
bool largeButtons = true;
bool showVacScreen = true;
bool showPpmScreen = true;
bool showLiveScreen = true;
bool showSettingsScreen = true;
bool showManualScreen = true;
bool showWebScreen = true;
int vacRampSpeed = 12;              // % per second, simulated response lag
int graphSpeed = 2;                 // 1-5 visual scroll speed
int screenScale = 2;                // reserved layout scale
int eStopReleaseMs = 1200;
String unitName = "Pluto 9000";

bool sdReady = false;

unsigned long lastGraphMs = 0;
unsigned long lastVacStepMs = 0;
unsigned long lastLiveMs = 0;
unsigned long lastTouchMs = 0;

int activeAdjust = 0;               // 0 none, 1 VAC-, 2 VAC+, 3 PPM-, 4 PPM+, 5 BR-, 6 BR+
unsigned long adjustStartMs = 0;
unsigned long lastAdjustMs = 0;

const int HIST_N = 190;
float vacHistory[HIST_N];
float ppmHistory[HIST_N];
int histPos = 0;

// ---------- Network / storage ----------
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);
IPAddress netMsk(255, 255, 255, 0);

// ---------- Colors ----------
uint16_t COL_BG;
uint16_t COL_PANEL;
uint16_t COL_PANEL2;
uint16_t COL_TEXT;
uint16_t COL_MUTED;
uint16_t COL_ACCENT;
uint16_t COL_ACCENT2;
uint16_t COL_WARN;
uint16_t COL_GOOD;
uint16_t COL_LINE;
uint16_t COL_GRID;
uint16_t COL_WHITE;
uint16_t COL_BLACK;
uint16_t COL_RED;
uint16_t COL_GREEN;
uint16_t COL_YELLOW;
uint16_t COL_BLUE;
uint16_t COL_ORANGE;

M5Canvas bootCanvas(&M5.Display);
M5Canvas graphCanvas(&M5.Display);

// ---------- Layout constants ----------
const int W = 320;
const int H = 240;
const int ESTOP_X = 248;
const int ESTOP_Y = 6;
const int ESTOP_W = 66;
const int ESTOP_H = 34;

// ---------- Helpers ----------
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void setText(int size, uint16_t fg, uint16_t bg) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(fg, bg);
}

void drawCenteredText(const String &s, int cx, int y, int size, uint16_t fg, uint16_t bg) {
  int approxW = s.length() * 6 * size;
  int x = cx - approxW / 2;
  if (x < 0) x = 0;
  setText(size, fg, bg);
  M5.Display.setCursor(x, y);
  M5.Display.print(s);
}

void fillCleanRect(int x, int y, int w, int h, uint16_t color) {
  M5.Display.fillRect(x, y, w, h, color);
}

void initPalette() {
  COL_WHITE = M5.Display.color565(245, 248, 255);
  COL_BLACK = M5.Display.color565(3, 8, 14);
  COL_RED = M5.Display.color565(230, 25, 42);
  COL_GREEN = M5.Display.color565(46, 210, 119);
  COL_YELLOW = M5.Display.color565(255, 214, 82);
  COL_BLUE = M5.Display.color565(67, 161, 255);
  COL_ORANGE = M5.Display.color565(255, 145, 52);

  if (darkTheme) {
    COL_BG = M5.Display.color565(8, 16, 26);
    COL_PANEL = M5.Display.color565(18, 31, 45);
    COL_PANEL2 = M5.Display.color565(28, 45, 64);
    COL_TEXT = M5.Display.color565(238, 246, 255);
    COL_MUTED = M5.Display.color565(145, 164, 181);
    COL_ACCENT = M5.Display.color565(70, 185, 255);
    COL_ACCENT2 = M5.Display.color565(119, 238, 184);
    COL_WARN = M5.Display.color565(255, 92, 105);
    COL_GOOD = M5.Display.color565(62, 220, 130);
    COL_LINE = M5.Display.color565(255, 220, 90);
    COL_GRID = M5.Display.color565(54, 74, 94);
  } else {
    COL_BG = M5.Display.color565(224, 234, 242);
    COL_PANEL = M5.Display.color565(246, 250, 255);
    COL_PANEL2 = M5.Display.color565(214, 228, 240);
    COL_TEXT = M5.Display.color565(14, 28, 42);
    COL_MUTED = M5.Display.color565(77, 96, 112);
    COL_ACCENT = M5.Display.color565(0, 117, 190);
    COL_ACCENT2 = M5.Display.color565(0, 150, 105);
    COL_WARN = M5.Display.color565(205, 22, 52);
    COL_GOOD = M5.Display.color565(0, 145, 80);
    COL_LINE = M5.Display.color565(210, 145, 0);
    COL_GRID = M5.Display.color565(179, 196, 210);
  }
}

int brightnessToHardware() {
  int step = clampInt(backlightStep, 1, 10);
  return map(step, 1, 10, 35, 255);
}

void applyBacklight() {
  M5.Display.setBrightness(brightnessToHardware());
}

void resetHistories() {
  for (int i = 0; i < HIST_N; i++) {
    vacHistory[i] = vacActual;
    ppmHistory[i] = 0;
  }
  histPos = 0;
}

void saveSettingsToSD() {
  if (!sdReady) return;
  File f = SD.open("/pluto9000.cfg", FILE_WRITE);
  if (!f) return;
  f.println("unit=" + unitName);
  f.println("vac=" + String(vacTarget));
  f.println("ppm=" + String(ppmTarget));
  f.println("dark=" + String(darkTheme ? 1 : 0));
  f.println("backlight=" + String(backlightStep));
  f.println("largeButtons=" + String(largeButtons ? 1 : 0));
  f.println("showVac=" + String(showVacScreen ? 1 : 0));
  f.println("showPpm=" + String(showPpmScreen ? 1 : 0));
  f.println("showLive=" + String(showLiveScreen ? 1 : 0));
  f.println("showSettings=" + String(showSettingsScreen ? 1 : 0));
  f.println("showManual=" + String(showManualScreen ? 1 : 0));
  f.println("showWeb=" + String(showWebScreen ? 1 : 0));
  f.println("vacRamp=" + String(vacRampSpeed));
  f.println("graphSpeed=" + String(graphSpeed));
  f.println("screenScale=" + String(screenScale));
  f.println("estopReleaseMs=" + String(eStopReleaseMs));
  f.close();
}

void saveSettings() {
  prefs.putInt("vac", vacTarget);
  prefs.putInt("ppm", ppmTarget);
  prefs.putBool("dark", darkTheme);
  prefs.putInt("back", backlightStep);
  prefs.putBool("large", largeButtons);
  prefs.putBool("sVac", showVacScreen);
  prefs.putBool("sPpm", showPpmScreen);
  prefs.putBool("sLive", showLiveScreen);
  prefs.putBool("sSet", showSettingsScreen);
  prefs.putBool("sMan", showManualScreen);
  prefs.putBool("sWeb", showWebScreen);
  prefs.putInt("ramp", vacRampSpeed);
  prefs.putInt("gspd", graphSpeed);
  prefs.putInt("scale", screenScale);
  prefs.putInt("relms", eStopReleaseMs);
  prefs.putString("unit", unitName);
  saveSettingsToSD();
}

void loadSettings() {
  vacTarget = prefs.getInt("vac", 42);
  ppmTarget = prefs.getInt("ppm", 18);
  darkTheme = prefs.getBool("dark", true);
  backlightStep = prefs.getInt("back", 8);
  largeButtons = prefs.getBool("large", true);
  showVacScreen = prefs.getBool("sVac", true);
  showPpmScreen = prefs.getBool("sPpm", true);
  showLiveScreen = prefs.getBool("sLive", true);
  showSettingsScreen = prefs.getBool("sSet", true);
  showManualScreen = prefs.getBool("sMan", true);
  showWebScreen = prefs.getBool("sWeb", true);
  vacRampSpeed = prefs.getInt("ramp", 12);
  graphSpeed = prefs.getInt("gspd", 2);
  screenScale = prefs.getInt("scale", 2);
  eStopReleaseMs = prefs.getInt("relms", 1200);
  unitName = prefs.getString("unit", "Pluto 9000");

  vacTarget = clampInt(vacTarget, 0, 100);
  ppmTarget = clampInt(ppmTarget, 0, 60);
  backlightStep = clampInt(backlightStep, 1, 10);
  vacRampSpeed = clampInt(vacRampSpeed, 2, 40);
  graphSpeed = clampInt(graphSpeed, 1, 5);
  screenScale = clampInt(screenScale, 1, 3);
  eStopReleaseMs = clampInt(eStopReleaseMs, 300, 5000);
}

// ---------- Drawing primitives ----------
void drawBigButton(int x, int y, int w, int h, const String &label, uint16_t fill, uint16_t fg, int size) {
  M5.Display.fillRoundRect(x, y, w, h, 12, fill);
  M5.Display.drawRoundRect(x, y, w, h, 12, COL_GRID);
  int approxW = label.length() * 6 * size;
  int tx = x + (w - approxW) / 2;
  int ty = y + (h - 8 * size) / 2;
  if (tx < x + 4) tx = x + 4;
  setText(size, fg, fill);
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label);
}

void drawSmallEStop() {
  drawBigButton(ESTOP_X, ESTOP_Y, ESTOP_W, ESTOP_H, "STOP", COL_WARN, COL_WHITE, 2);
}

void drawTopBar(const String &title) {
  M5.Display.fillRect(0, 0, W, 44, COL_PANEL);
  setText(3, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(8, 10);
  M5.Display.print(title);
  drawSmallEStop();
}

void drawBackButton() {
  drawBigButton(8, 198, 86, 34, "BACK", COL_PANEL2, COL_TEXT, 2);
}

void drawValueBox(int x, int y, int w, int h, const String &big, const String &small, uint16_t edge) {
  M5.Display.fillRoundRect(x, y, w, h, 12, COL_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 12, edge);
  setText(4, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(x + 12, y + 10);
  M5.Display.print(big);
  setText(2, COL_MUTED, COL_PANEL);
  M5.Display.setCursor(x + 14, y + h - 22);
  M5.Display.print(small);
}

void updateVacValueText() {
  M5.Display.fillRoundRect(20, 46, 280, 34, 8, COL_PANEL);
  setText(3, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(26, 53);
  M5.Display.print("VAC ");
  M5.Display.print(vacTarget);
  M5.Display.print("%    ");
}

void updatePpmValueText() {
  M5.Display.fillRoundRect(20, 46, 280, 34, 8, COL_PANEL);
  setText(3, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(26, 53);
  M5.Display.print("PPM ");
  M5.Display.print(ppmTarget);
  if (ppmTarget == 0) M5.Display.print(" OFF");
  M5.Display.print("    ");
}

// ---------- Boot art ----------
void drawMilky(M5Canvas &c, int x, int y, int eyeDir) {
  uint16_t milkWhite = c.color565(248, 252, 255);
  uint16_t milkBlue = c.color565(168, 218, 255);
  uint16_t glass = c.color565(205, 234, 255);
  uint16_t cap = c.color565(78, 160, 230);
  uint16_t ink = c.color565(15, 38, 58);
  uint16_t label = c.color565(235, 245, 255);
  uint16_t labelEdge = c.color565(70, 185, 255);

  // Bottle shadow
  c.fillRoundRect(x + 4, y + 118, 96, 18, 9, c.color565(6, 12, 20));

  // Bottle body
  c.fillRoundRect(x + 12, y + 38, 86, 92, 18, milkWhite);
  c.drawRoundRect(x + 12, y + 38, 86, 92, 18, glass);
  c.fillRoundRect(x + 29, y + 16, 52, 34, 12, milkWhite);
  c.drawRoundRect(x + 29, y + 16, 52, 34, 12, glass);
  c.fillRoundRect(x + 32, y + 8, 46, 16, 6, cap);

  // shine
  c.fillRoundRect(x + 25, y + 48, 12, 56, 6, c.color565(255, 255, 255));
  c.drawLine(x + 78, y + 52, x + 87, y + 96, milkBlue);

  // face
  c.fillCircle(x + 45, y + 61, 10, COL_WHITE);
  c.fillCircle(x + 70, y + 61, 10, COL_WHITE);
  c.drawCircle(x + 45, y + 61, 10, ink);
  c.drawCircle(x + 70, y + 61, 10, ink);
  int px = eyeDir * 3;
  c.fillCircle(x + 45 + px, y + 62, 4, ink);
  c.fillCircle(x + 70 + px, y + 62, 4, ink);
  c.drawLine(x + 49, y + 77, x + 55, y + 82, ink);
  c.drawLine(x + 55, y + 82, x + 64, y + 82, ink);
  c.drawLine(x + 64, y + 82, x + 70, y + 77, ink);

  // chest label
  c.fillRoundRect(x + 22, y + 88, 66, 28, 8, label);
  c.drawRoundRect(x + 22, y + 88, 66, 28, 8, labelEdge);
  c.setTextSize(2);
  c.setTextColor(ink, label);
  c.setCursor(x + 28, y + 95);
  c.print("milky");
}

void drawPulseCoreLogoCanvas(M5Canvas &c, int cx, int cy, int size) {
  uint16_t pcBlue = c.color565(76, 190, 255);
  uint16_t pcMint = c.color565(112, 240, 186);
  uint16_t pcText = c.color565(235, 248, 255);
  uint16_t pcDim = c.color565(48, 78, 104);

  c.drawCircle(cx - 74, cy + 2, 22, pcBlue);
  c.drawCircle(cx - 74, cy + 2, 12, pcMint);
  c.fillCircle(cx - 74, cy + 2, 5, pcText);
  c.drawLine(cx - 110, cy + 2, cx - 97, cy + 2, pcDim);
  c.drawLine(cx - 52, cy + 2, cx - 32, cy + 2, pcDim);
  c.drawLine(cx - 97, cy + 2, cx - 91, cy - 12, pcMint);
  c.drawLine(cx - 91, cy - 12, cx - 82, cy + 16, pcMint);
  c.drawLine(cx - 82, cy + 16, cx - 75, cy - 8, pcMint);
  c.drawLine(cx - 75, cy - 8, cx - 66, cy + 2, pcMint);
  c.drawLine(cx - 66, cy + 2, cx - 52, cy + 2, pcMint);

  c.setTextSize(size);
  c.setTextColor(pcText, c.color565(8, 16, 26));
  c.setCursor(cx - 20, cy - 13);
  c.print("PulseCore");
}

void bootAnimation() {
  bootCanvas.createSprite(W, H);
  bootCanvas.setTextWrap(false);

  // Milky flies in and stops.
  for (int x = -120; x <= 105; x += 9) {
    bootCanvas.fillScreen(bootCanvas.color565(8, 16, 26));
    drawMilky(bootCanvas, x, 56, 0);
    bootCanvas.pushSprite(0, 0);
    delay(24);
  }

  // Pluto descends while Milky looks around.
  for (int i = 0; i <= 70; i += 4) {
    int eye = 0;
    if (i > 14 && i <= 30) eye = 1;
    if (i > 30 && i <= 48) eye = -1;
    if (i > 48) eye = 1;

    bootCanvas.fillScreen(bootCanvas.color565(8, 16, 26));
    drawMilky(bootCanvas, 105, 56, eye);

    int py = -54 + i;
    bootCanvas.setTextSize(4);
    bootCanvas.setTextColor(bootCanvas.color565(240, 248, 255), bootCanvas.color565(8, 16, 26));
    bootCanvas.setCursor(52, py);
    bootCanvas.print("PLUTO");
    bootCanvas.setCursor(75, py + 34);
    bootCanvas.print("9000");

    bootCanvas.pushSprite(0, 0);
    delay(34);
  }

  delay(450);

  // Milky exits.
  for (int x = 105; x <= 340; x += 11) {
    bootCanvas.fillScreen(bootCanvas.color565(8, 16, 26));
    drawMilky(bootCanvas, x, 56, 1);
    bootCanvas.setTextSize(4);
    bootCanvas.setTextColor(bootCanvas.color565(240, 248, 255), bootCanvas.color565(8, 16, 26));
    bootCanvas.setCursor(52, 16);
    bootCanvas.print("PLUTO");
    bootCanvas.setCursor(75, 50);
    bootCanvas.print("9000");
    bootCanvas.pushSprite(0, 0);
    delay(22);
  }

  // PulseCore logo hold screen.
  bootCanvas.fillScreen(bootCanvas.color565(8, 16, 26));
  drawPulseCoreLogoCanvas(bootCanvas, 158, 118, 3);
  bootCanvas.pushSprite(0, 0);
  delay(5000);

  bootCanvas.deleteSprite();
}

// ---------- Graph rendering ----------
void drawGraphFrameToCanvas(int gw, int gh, const String &leftTop, const String &leftBottom) {
  graphCanvas.fillScreen(COL_PANEL);
  graphCanvas.drawRect(0, 0, gw, gh, COL_GRID);
  for (int i = 1; i < 4; i++) {
    int yy = i * gh / 4;
    graphCanvas.drawLine(28, yy, gw - 2, yy, COL_GRID);
  }
  graphCanvas.drawLine(28, 4, 28, gh - 8, COL_MUTED);
  graphCanvas.setTextSize(1);
  graphCanvas.setTextColor(COL_MUTED, COL_PANEL);
  graphCanvas.setCursor(3, 4);
  graphCanvas.print(leftTop);
  graphCanvas.setCursor(5, gh - 14);
  graphCanvas.print(leftBottom);
}

void pushVacGraph(int x, int y, int gw, int gh) {
  drawGraphFrameToCanvas(gw, gh, "100", "0");
  int baseX = 30;
  int baseY = gh - 10;
  int topY = 8;
  int usableH = baseY - topY;

  int targetY = baseY - map(vacTarget, 0, 100, 0, usableH);
  graphCanvas.drawLine(baseX, targetY, gw - 4, targetY, COL_LINE);

  int lastX = baseX;
  int lastY = baseY - map((int)vacHistory[(histPos + 1) % HIST_N], 0, 100, 0, usableH);
  for (int i = 1; i < gw - baseX - 5; i++) {
    int idx = (histPos + i) % HIST_N;
    int yy = baseY - map((int)vacHistory[idx], 0, 100, 0, usableH);
    int xx = baseX + i;
    graphCanvas.drawLine(lastX, lastY, xx, yy, COL_ACCENT2);
    lastX = xx;
    lastY = yy;
  }

  graphCanvas.pushSprite(x, y);
}

float ppmWaveAtSeconds(float t) {
  if (ppmTarget <= 0) return 0.0;
  float cycleSec = 60.0 / (float)ppmTarget;
  float phase = fmod(t, cycleSec) / cycleSec;
  if (phase < 0.5) return phase * 2.0;
  return (1.0 - phase) * 2.0;
}

void pushPpmGraph(int x, int y, int gw, int gh) {
  drawGraphFrameToCanvas(gw, gh, "UP", "DN");
  int baseX = 30;
  int midY = gh / 2;
  int amp = 26;
  float nowSec = millis() / 1000.0;
  float pxSeconds = 0.14 / (float)graphSpeed;

  int lastX = baseX;
  int lastY = midY;
  for (int i = 0; i < gw - baseX - 5; i++) {
    float age = (gw - baseX - 5 - i) * pxSeconds;
    float v = ppmWaveAtSeconds(nowSec - age);
    int yy = midY - (int)((v - 0.5) * amp * 2.0);
    if (ppmTarget <= 0) yy = midY;
    int xx = baseX + i;
    if (i > 0) graphCanvas.drawLine(lastX, lastY, xx, yy, COL_ACCENT);
    lastX = xx;
    lastY = yy;
  }

  graphCanvas.pushSprite(x, y);
}

void pushLiveBars() {
  int x = 24;
  int y = 70;
  int bw = 272;
  int bh = 44;

  M5.Display.fillRoundRect(x, y, bw, bh, 8, COL_PANEL);
  setText(2, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(x + 12, y + 13);
  M5.Display.print("VAC");
  int vacBar = map((int)vacActual, 0, 100, 0, 168);
  M5.Display.drawRect(x + 82, y + 11, 174, 20, COL_GRID);
  M5.Display.fillRect(x + 84, y + 13, vacBar, 16, COL_ACCENT2);
  M5.Display.setCursor(x + 260, y + 13);
  M5.Display.print((int)vacActual);

  y = 128;
  M5.Display.fillRoundRect(x, y, bw, bh, 8, COL_PANEL);
  setText(2, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(x + 12, y + 13);
  M5.Display.print("PPM");
  int ppmBar = map(ppmTarget, 0, 60, 0, 168);
  M5.Display.drawRect(x + 82, y + 11, 174, 20, COL_GRID);
  M5.Display.fillRect(x + 84, y + 13, ppmBar, 16, COL_ACCENT);
  M5.Display.setCursor(x + 260, y + 13);
  M5.Display.print(ppmTarget);
}

// ---------- Screens ----------
void drawHomeScreen() {
  currentScreen = SCREEN_HOME;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("Pluto");

  drawValueBox(12, 50, 142, 54, "VAC " + String(vacTarget), "target %", COL_ACCENT2);
  drawValueBox(166, 50, 142, 54, "PPM " + String(ppmTarget), "pumps/min", COL_ACCENT);

  drawBigButton(12, 114, 142, 52, showVacScreen ? "VAC" : "VAC", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(166, 114, 142, 52, showPpmScreen ? "PPM" : "PPM", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(12, 176, 142, 52, simRunning ? "STOP" : "START", simRunning ? COL_WARN : COL_GOOD, COL_WHITE, 3);
  drawBigButton(166, 176, 142, 52, "MORE", COL_ACCENT, COL_WHITE, 3);
}

void drawVacScreen() {
  currentScreen = SCREEN_VAC;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("VAC");
  updateVacValueText();
  graphCanvas.createSprite(250, 82);
  pushVacGraph(35, 86, 250, 82);
  graphCanvas.deleteSprite();
  drawBigButton(18, 177, 132, 56, "-", COL_PANEL2, COL_TEXT, 5);
  drawBigButton(170, 177, 132, 56, "+", COL_ACCENT2, COL_BLACK, 5);
  drawBackButton();
}

void drawPpmScreen() {
  currentScreen = SCREEN_PPM;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("PPM");
  updatePpmValueText();
  graphCanvas.createSprite(250, 82);
  pushPpmGraph(35, 86, 250, 82);
  graphCanvas.deleteSprite();
  drawBigButton(18, 177, 132, 56, "-", COL_PANEL2, COL_TEXT, 5);
  drawBigButton(170, 177, 132, 56, "+", COL_ACCENT, COL_WHITE, 5);
  drawBackButton();
}

void drawLiveScreen() {
  currentScreen = SCREEN_LIVE;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("Live");
  pushLiveBars();
  drawBigButton(22, 188, 132, 42, simRunning ? "STOP" : "START", simRunning ? COL_WARN : COL_GOOD, COL_WHITE, 2);
  drawBigButton(166, 188, 132, 42, "HOME", COL_PANEL2, COL_TEXT, 2);
}

void drawSettingsScreen() {
  currentScreen = SCREEN_SETTINGS;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("Settings");

  drawBigButton(16, 54, 136, 48, darkTheme ? "DARK" : "LIGHT", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(168, 54, 136, 48, "WEB", COL_ACCENT, COL_WHITE, 3);

  M5.Display.fillRoundRect(16, 112, 288, 42, 10, COL_PANEL);
  setText(2, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(24, 125);
  M5.Display.print("BACKLIGHT");
  int sliderX = 150;
  int sliderY = 128;
  M5.Display.drawRect(sliderX, sliderY, 118, 12, COL_GRID);
  int fillW = map(backlightStep, 1, 10, 8, 116);
  M5.Display.fillRect(sliderX + 1, sliderY + 1, fillW, 10, COL_ACCENT2);
  setText(2, COL_TEXT, COL_PANEL);
  M5.Display.setCursor(275, 125);
  M5.Display.print(backlightStep);

  drawBigButton(16, 166, 136, 48, "DIM", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(168, 166, 136, 48, "BRIGHT", COL_ACCENT2, COL_BLACK, 3);
  drawBackButton();
}

void drawWebScreen() {
  currentScreen = SCREEN_WEB;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("Web");
  setText(3, COL_TEXT, COL_BG);
  M5.Display.setCursor(18, 62);
  M5.Display.print("WiFi");
  setText(3, COL_ACCENT2, COL_BG);
  M5.Display.setCursor(18, 100);
  M5.Display.print("Pluto9000");
  setText(3, COL_TEXT, COL_BG);
  M5.Display.setCursor(18, 146);
  M5.Display.print("192.168.4.1");
  drawBigButton(18, 194, 132, 38, "HOME", COL_PANEL2, COL_TEXT, 2);
  drawBigButton(170, 194, 132, 38, "SET", COL_ACCENT, COL_WHITE, 2);
}

void drawMenuScreen() {
  currentScreen = SCREEN_MENU;
  M5.Display.fillScreen(COL_BG);
  drawTopBar("More");
  drawBigButton(16, 54, 136, 52, "LIVE", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(168, 54, 136, 52, "WEB", COL_ACCENT, COL_WHITE, 3);
  drawBigButton(16, 118, 136, 52, "SET", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(168, 118, 136, 52, "TEST", COL_PANEL2, COL_TEXT, 3);
  drawBigButton(16, 182, 288, 48, "HOME", COL_PANEL2, COL_TEXT, 3);
}

void drawEStopConfirmScreen() {
  previousScreen = currentScreen;
  currentScreen = SCREEN_ESTOP_CONFIRM;
  M5.Display.fillScreen(COL_RED);
  setText(5, COL_WHITE, COL_RED);
  M5.Display.setCursor(32, 48);
  M5.Display.print("E-STOP");
  setText(3, COL_WHITE, COL_RED);
  M5.Display.setCursor(28, 118);
  M5.Display.print("PRESS RED");
  M5.Display.fillRect(0, 216, W, 24, COL_GREEN);
  setText(2, COL_BLACK, COL_GREEN);
  M5.Display.setCursor(112, 221);
  M5.Display.print("CANCEL");
}

void drawEStopActiveScreen() {
  currentScreen = SCREEN_ESTOP_ACTIVE;
  simRunning = false;
  activeAdjust = 0;
  M5.Display.fillScreen(COL_RED);
  setText(5, COL_WHITE, COL_RED);
  M5.Display.setCursor(32, 42);
  M5.Display.print("STOPPED");
  setText(3, COL_WHITE, COL_RED);
  M5.Display.setCursor(26, 112);
  M5.Display.print("RELEASED");
  M5.Display.fillRect(0, 216, W, 24, COL_GREEN);
  setText(2, COL_BLACK, COL_GREEN);
  M5.Display.setCursor(118, 221);
  M5.Display.print("RETURN");
}

void drawScreenById(int screenId) {
  if (screenId == SCREEN_HOME) drawHomeScreen();
  else if (screenId == SCREEN_VAC) drawVacScreen();
  else if (screenId == SCREEN_PPM) drawPpmScreen();
  else if (screenId == SCREEN_LIVE) drawLiveScreen();
  else if (screenId == SCREEN_SETTINGS) drawSettingsScreen();
  else if (screenId == SCREEN_WEB) drawWebScreen();
  else if (screenId == SCREEN_MENU) drawMenuScreen();
  else drawHomeScreen();
}

// ---------- Simulation ----------
void updateVacSimulation() {
  unsigned long now = millis();
  if (lastVacStepMs == 0) lastVacStepMs = now;
  float dt = (now - lastVacStepMs) / 1000.0;
  if (dt <= 0.0) return;
  lastVacStepMs = now;

  float target = (float)vacTarget;
  if (!simRunning) target = 0.0;
  float maxStep = (float)vacRampSpeed * dt;
  if (vacActual < target) {
    vacActual += maxStep;
    if (vacActual > target) vacActual = target;
  } else if (vacActual > target) {
    vacActual -= maxStep;
    if (vacActual < target) vacActual = target;
  }
}

void updateHistories() {
  unsigned long now = millis();
  int interval = 120 / graphSpeed;
  if (interval < 30) interval = 30;
  if (now - lastGraphMs < (unsigned long)interval) return;
  lastGraphMs = now;

  histPos++;
  if (histPos >= HIST_N) histPos = 0;
  vacHistory[histPos] = vacActual;
  ppmHistory[histPos] = ppmWaveAtSeconds(now / 1000.0) * 100.0;
}

void refreshActiveGraphOnly() {
  if (currentScreen == SCREEN_VAC) {
    graphCanvas.createSprite(250, 82);
    pushVacGraph(35, 86, 250, 82);
    graphCanvas.deleteSprite();
  } else if (currentScreen == SCREEN_PPM) {
    graphCanvas.createSprite(250, 82);
    pushPpmGraph(35, 86, 250, 82);
    graphCanvas.deleteSprite();
  } else if (currentScreen == SCREEN_LIVE) {
    if (millis() - lastLiveMs > 250) {
      lastLiveMs = millis();
      pushLiveBars();
    }
  }
}

void changeVac(int delta) {
  int old = vacTarget;
  vacTarget = clampInt(vacTarget + delta, 0, 100);
  if (vacTarget != old) {
    updateVacValueText();
    saveSettings();
  }
}

void changePpm(int delta) {
  int old = ppmTarget;
  ppmTarget = clampInt(ppmTarget + delta, 0, 60);
  if (ppmTarget != old) {
    updatePpmValueText();
    saveSettings();
  }
}

void changeBacklight(int delta) {
  int old = backlightStep;
  backlightStep = clampInt(backlightStep + delta, 1, 10);
  if (backlightStep != old) {
    applyBacklight();
    saveSettings();
    drawSettingsScreen();
  }
}

void performAdjustStep() {
  if (activeAdjust == 0) return;
  unsigned long held = millis() - adjustStartMs;
  int repeatMs = held > 3000 ? 120 : 260;
  if (millis() - lastAdjustMs < (unsigned long)repeatMs) return;
  lastAdjustMs = millis();

  if (activeAdjust == 1) changeVac(held > 3000 ? -10 : -2);
  if (activeAdjust == 2) changeVac(held > 3000 ? 10 : 2);
  if (activeAdjust == 3) changePpm(held > 3000 ? -10 : -1);
  if (activeAdjust == 4) changePpm(held > 3000 ? 10 : 1);
  if (activeAdjust == 5) changeBacklight(-1);
  if (activeAdjust == 6) changeBacklight(1);
}

void startAdjust(int adjustId) {
  activeAdjust = adjustId;
  adjustStartMs = millis();
  lastAdjustMs = 0;
  performAdjustStep();
}

// ---------- Web UI ----------
String checked(bool v) {
  return v ? "checked" : "";
}

String selectedIf(int a, int b) {
  return a == b ? "selected" : "";
}

String htmlPage() {
  String html = R"HTML(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1">
<title>Pluto 9000</title>
<style>
:root{--bg:#08101a;--card:#111f2e;--card2:#1d3043;--text:#eef6ff;--muted:#91a4b5;--blue:#46b9ff;--mint:#77eeb8;--red:#ff5366;--yellow:#ffd65a;}
body.light{--bg:#e6eef6;--card:#ffffff;--card2:#dceaf5;--text:#102133;--muted:#587086;--blue:#0075be;--mint:#00996b;--red:#ce203b;--yellow:#c98d00;}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#19354b,var(--bg));color:var(--text);font-family:Arial,Helvetica,sans-serif}.wrap{max-width:960px;margin:0 auto;padding:18px}.hero{display:flex;gap:14px;align-items:center;justify-content:space-between;margin-bottom:14px}.brand{font-size:34px;font-weight:900;letter-spacing:-1px}.sub{color:var(--muted);font-size:15px}.pill{background:var(--card);border:1px solid #31506b;border-radius:999px;padding:10px 14px;color:var(--mint);font-weight:800}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px}.card{background:linear-gradient(180deg,var(--card),var(--card2));border:1px solid #31506b;border-radius:22px;padding:16px;box-shadow:0 16px 40px #0008}.card h2{margin:0 0 12px;font-size:24px}.row{display:flex;align-items:center;justify-content:space-between;gap:12px;margin:12px 0}.big{font-size:44px;font-weight:900}.btns{display:grid;grid-template-columns:1fr 1fr;gap:10px}button,.button{border:0;border-radius:18px;padding:18px;font-size:20px;font-weight:900;background:var(--blue);color:#fff;box-shadow:0 8px 0 #0004;text-decoration:none;text-align:center}button.alt{background:var(--card2);color:var(--text);border:1px solid #44647e}button.good{background:var(--mint);color:#07150e}button.bad{background:var(--red)}input[type=range]{width:100%;accent-color:var(--mint)}input[type=number],input[type=text],select{width:100%;border-radius:14px;border:1px solid #40627d;background:#0003;color:var(--text);padding:14px;font-size:20px}label{font-size:17px;color:var(--muted);font-weight:700}.toggle{display:flex;gap:10px;align-items:center}input[type=checkbox]{width:26px;height:26px}.tiny{font-size:13px;color:var(--muted)}.share{font-size:22px;background:#0003;border-radius:14px;padding:12px;word-break:break-all}.nav{display:flex;gap:10px;flex-wrap:wrap;margin:16px 0}.nav a{color:var(--text);background:var(--card);border:1px solid #31506b;padding:12px 14px;border-radius:999px;text-decoration:none;font-weight:800}.savebar{position:sticky;bottom:0;background:linear-gradient(0deg,var(--bg),#0000);padding:16px 0;margin-top:16px}.meter{height:18px;background:#0005;border-radius:99px;overflow:hidden}.meter span{display:block;height:100%;background:linear-gradient(90deg,var(--blue),var(--mint));width:20%}.warn{color:var(--yellow);font-weight:800}
</style>
</head>
<body class="{{THEME}}">
<div class="wrap">
  <div class="hero"><div><div class="brand">PulseCore</div><div class="sub">Pluto 9000</div></div><div class="pill">WiFi: Pluto9000</div></div>
  <div class="nav"><a href="#run">Run</a><a href="#tune">Tune</a><a href="#screen">Screen</a><a href="#menus">Menus</a><a href="#safety">Safety</a><a href="#share">Share</a></div>
<form method="POST" action="/save">
  <div class="grid" id="run">
    <div class="card"><h2>VAC</h2><div class="row"><div class="big"><span id="vacVal">{{VAC}}</span>%</div></div><input name="vac" id="vac" type="range" min="0" max="100" step="1" value="{{VAC}}" oninput="vacVal.textContent=this.value"></div>
    <div class="card"><h2>PPM</h2><div class="row"><div class="big"><span id="ppmVal">{{PPM}}</span></div></div><input name="ppm" id="ppm" type="range" min="0" max="60" step="1" value="{{PPM}}" oninput="ppmVal.textContent=this.value"><div class="tiny">0 is off. 1 PPM = 30 sec up / 30 sec down. 60 PPM = 0.5 sec up / 0.5 sec down.</div></div>
  </div>
  <div class="grid" id="tune">
    <div class="card"><h2>Response</h2><label>VAC ramp speed</label><input name="ramp" type="range" min="2" max="40" step="1" value="{{RAMP}}"><label>Graph speed</label><input name="graph" type="range" min="1" max="5" step="1" value="{{GRAPH}}"><label>Emergency release ms</label><input name="relms" type="number" min="300" max="5000" value="{{RELMS}}"></div>
    <div class="card"><h2>Unit</h2><label>Name</label><input name="unit" type="text" maxlength="24" value="{{UNIT}}"><div class="row"><label class="toggle"><input name="large" type="checkbox" {{LARGE}}> Large on-device buttons</label></div><label>Screen scale</label><select name="scale"><option value="1" {{S1}}>Compact</option><option value="2" {{S2}}>Large</option><option value="3" {{S3}}>Huge</option></select></div>
  </div>
  <div class="grid" id="screen">
    <div class="card"><h2>Display</h2><div class="row"><label class="toggle"><input name="dark" type="checkbox" {{DARK}}> Dark mode</label></div><label>Backlight, 10 steps</label><input name="back" type="range" min="1" max="10" step="1" value="{{BACK}}"></div>
    <div class="card"><h2>Home Layout</h2><div class="row"><label class="toggle"><input name="showVac" type="checkbox" {{SHOWVAC}}> Show VAC</label></div><div class="row"><label class="toggle"><input name="showPpm" type="checkbox" {{SHOWPPM}}> Show PPM</label></div><div class="row"><label class="toggle"><input name="showLive" type="checkbox" {{SHOWLIVE}}> Show Live</label></div><div class="row"><label class="toggle"><input name="showSet" type="checkbox" {{SHOWSET}}> Show Settings</label></div><div class="row"><label class="toggle"><input name="showMan" type="checkbox" {{SHOWMAN}}> Show Test</label></div><div class="row"><label class="toggle"><input name="showWeb" type="checkbox" {{SHOWWEB}}> Show Web</label></div></div>
  </div>
  <div class="grid" id="menus">
    <div class="card"><h2>Screen Options</h2><button class="alt" formaction="/set?screen=home">Show Home</button><br><br><button class="alt" formaction="/set?screen=vac">Show VAC</button><br><br><button class="alt" formaction="/set?screen=ppm">Show PPM</button></div>
    <div class="card"><h2>Presets</h2><div class="btns"><button name="preset" value="gentle">Gentle</button><button name="preset" value="normal">Normal</button><button name="preset" value="strong">Strong</button><button class="alt" name="preset" value="off">Off</button></div></div>
  </div>
  <div class="grid" id="safety">
    <div class="card"><h2>Safety</h2><p class="warn">Emergency stop forces the demo into release state.</p><div class="btns"><button class="bad" formaction="/estop">E-STOP</button><button class="good" formaction="/release">Release</button></div></div>
    <div class="card"><h2>Status</h2><div class="row"><label>VAC actual</label><b>{{VACTUAL}}%</b></div><div class="meter"><span style="width:{{VACTUAL}}%"></span></div><div class="row"><label>SD backup</label><b>{{SD}}</b></div></div>
  </div>
  <div class="card" id="share"><h2>Share Browser Control</h2><div class="share">http://192.168.4.1/</div><p class="tiny">Connect to WiFi network Pluto9000, then open this address.</p><button type="button" onclick="navigator.clipboard&&navigator.clipboard.writeText('http://192.168.4.1/')">Copy Link</button></div>
  <div class="savebar"><button class="good" type="submit">SAVE TO DEVICE</button></div>
</form>
</div>
<script>
setInterval(()=>fetch('/api').then(r=>r.json()).then(j=>{document.getElementById('vacVal').textContent=j.vac;document.getElementById('ppmVal').textContent=j.ppm}).catch(()=>{}),2000);
</script>
</body>
</html>
)HTML";

  html.replace("{{THEME}}", darkTheme ? "" : "light");
  html.replace("{{VAC}}", String(vacTarget));
  html.replace("{{PPM}}", String(ppmTarget));
  html.replace("{{RAMP}}", String(vacRampSpeed));
  html.replace("{{GRAPH}}", String(graphSpeed));
  html.replace("{{RELMS}}", String(eStopReleaseMs));
  html.replace("{{UNIT}}", unitName);
  html.replace("{{LARGE}}", checked(largeButtons));
  html.replace("{{DARK}}", checked(darkTheme));
  html.replace("{{BACK}}", String(backlightStep));
  html.replace("{{SHOWVAC}}", checked(showVacScreen));
  html.replace("{{SHOWPPM}}", checked(showPpmScreen));
  html.replace("{{SHOWLIVE}}", checked(showLiveScreen));
  html.replace("{{SHOWSET}}", checked(showSettingsScreen));
  html.replace("{{SHOWMAN}}", checked(showManualScreen));
  html.replace("{{SHOWWEB}}", checked(showWebScreen));
  html.replace("{{S1}}", selectedIf(screenScale, 1));
  html.replace("{{S2}}", selectedIf(screenScale, 2));
  html.replace("{{S3}}", selectedIf(screenScale, 3));
  html.replace("{{VACTUAL}}", String((int)vacActual));
  html.replace("{{SD}}", sdReady ? "ready" : "not detected");
  return html;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void readWebForm() {
  if (server.hasArg("vac")) vacTarget = clampInt(server.arg("vac").toInt(), 0, 100);
  if (server.hasArg("ppm")) ppmTarget = clampInt(server.arg("ppm").toInt(), 0, 60);
  if (server.hasArg("ramp")) vacRampSpeed = clampInt(server.arg("ramp").toInt(), 2, 40);
  if (server.hasArg("graph")) graphSpeed = clampInt(server.arg("graph").toInt(), 1, 5);
  if (server.hasArg("relms")) eStopReleaseMs = clampInt(server.arg("relms").toInt(), 300, 5000);
  if (server.hasArg("unit")) unitName = server.arg("unit");
  if (server.hasArg("back")) backlightStep = clampInt(server.arg("back").toInt(), 1, 10);
  if (server.hasArg("scale")) screenScale = clampInt(server.arg("scale").toInt(), 1, 3);

  largeButtons = server.hasArg("large");
  darkTheme = server.hasArg("dark");
  showVacScreen = server.hasArg("showVac");
  showPpmScreen = server.hasArg("showPpm");
  showLiveScreen = server.hasArg("showLive");
  showSettingsScreen = server.hasArg("showSet");
  showManualScreen = server.hasArg("showMan");
  showWebScreen = server.hasArg("showWeb");

  if (server.hasArg("preset")) {
    String p = server.arg("preset");
    if (p == "gentle") { vacTarget = 28; ppmTarget = 12; vacRampSpeed = 8; }
    if (p == "normal") { vacTarget = 42; ppmTarget = 18; vacRampSpeed = 12; }
    if (p == "strong") { vacTarget = 62; ppmTarget = 28; vacRampSpeed = 16; }
    if (p == "off") { vacTarget = 0; ppmTarget = 0; }
  }

  initPalette();
  applyBacklight();
  saveSettings();
}

void handleSave() {
  readWebForm();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSet() {
  if (server.hasArg("screen")) {
    String s = server.arg("screen");
    if (s == "home") drawHomeScreen();
    if (s == "vac") drawVacScreen();
    if (s == "ppm") drawPpmScreen();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleEStopWeb() {
  drawEStopActiveScreen();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleReleaseWeb() {
  simRunning = false;
  vacTarget = 0;
  ppmTarget = 0;
  saveSettings();
  drawHomeScreen();
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleApi() {
  String json = "{";
  json += "\"vac\":" + String(vacTarget) + ",";
  json += "\"vacActual\":" + String((int)vacActual) + ",";
  json += "\"ppm\":" + String(ppmTarget) + ",";
  json += "\"running\":" + String(simRunning ? 1 : 0) + ",";
  json += "\"ip\":\"192.168.4.1\"";
  json += "}";
  server.send(200, "application/json", json);
}

void startWifi() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP("Pluto9000");
  dnsServer.start(53, "*", apIP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/set", HTTP_POST, handleSet);
  server.on("/set", HTTP_GET, handleSet);
  server.on("/estop", HTTP_POST, handleEStopWeb);
  server.on("/release", HTTP_POST, handleReleaseWeb);
  server.on("/api", HTTP_GET, handleApi);
  server.onNotFound(handleRoot);
  server.begin();
}

// ---------- Touch handling ----------
void touchHome(int x, int y) {
  if (inRect(x, y, 12, 114, 142, 52)) drawVacScreen();
  else if (inRect(x, y, 166, 114, 142, 52)) drawPpmScreen();
  else if (inRect(x, y, 12, 176, 142, 52)) {
    simRunning = !simRunning;
    drawHomeScreen();
  } else if (inRect(x, y, 166, 176, 142, 52)) drawMenuScreen();
}

void touchVac(int x, int y, bool pressed) {
  if (pressed) {
    if (inRect(x, y, 18, 177, 132, 56)) startAdjust(1);
    else if (inRect(x, y, 170, 177, 132, 56)) startAdjust(2);
    else if (inRect(x, y, 8, 198, 86, 34)) drawHomeScreen();
  }
}

void touchPpm(int x, int y, bool pressed) {
  if (pressed) {
    if (inRect(x, y, 18, 177, 132, 56)) startAdjust(3);
    else if (inRect(x, y, 170, 177, 132, 56)) startAdjust(4);
    else if (inRect(x, y, 8, 198, 86, 34)) drawHomeScreen();
  }
}

void touchLive(int x, int y) {
  if (inRect(x, y, 22, 188, 132, 42)) {
    simRunning = !simRunning;
    drawLiveScreen();
  } else if (inRect(x, y, 166, 188, 132, 42)) drawHomeScreen();
}

void touchSettings(int x, int y, bool pressed) {
  if (!pressed) return;
  if (inRect(x, y, 16, 54, 136, 48)) {
    darkTheme = !darkTheme;
    initPalette();
    saveSettings();
    drawSettingsScreen();
  } else if (inRect(x, y, 168, 54, 136, 48)) drawWebScreen();
  else if (inRect(x, y, 16, 166, 136, 48)) startAdjust(5);
  else if (inRect(x, y, 168, 166, 136, 48)) startAdjust(6);
  else if (inRect(x, y, 8, 198, 86, 34)) drawHomeScreen();
}

void touchWeb(int x, int y) {
  if (inRect(x, y, 18, 194, 132, 38)) drawHomeScreen();
  else if (inRect(x, y, 170, 194, 132, 38)) drawSettingsScreen();
}

void touchMenu(int x, int y) {
  if (inRect(x, y, 16, 54, 136, 52)) drawLiveScreen();
  else if (inRect(x, y, 168, 54, 136, 52)) drawWebScreen();
  else if (inRect(x, y, 16, 118, 136, 52)) drawSettingsScreen();
  else if (inRect(x, y, 168, 118, 136, 52)) drawLiveScreen();
  else if (inRect(x, y, 16, 182, 288, 48)) drawHomeScreen();
}

void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasReleased()) {
    activeAdjust = 0;
  }

  if (!t.wasPressed()) return;
  int x = t.x;
  int y = t.y;

  if (currentScreen == SCREEN_ESTOP_CONFIRM) {
    if (y >= 216) drawScreenById(previousScreen);
    else drawEStopActiveScreen();
    return;
  }

  if (currentScreen == SCREEN_ESTOP_ACTIVE) {
    if (y >= 216) drawHomeScreen();
    return;
  }

  if (inRect(x, y, ESTOP_X, ESTOP_Y, ESTOP_W, ESTOP_H)) {
    drawEStopConfirmScreen();
    return;
  }

  if (currentScreen == SCREEN_HOME) touchHome(x, y);
  else if (currentScreen == SCREEN_VAC) touchVac(x, y, true);
  else if (currentScreen == SCREEN_PPM) touchPpm(x, y, true);
  else if (currentScreen == SCREEN_LIVE) touchLive(x, y);
  else if (currentScreen == SCREEN_SETTINGS) touchSettings(x, y, true);
  else if (currentScreen == SCREEN_WEB) touchWeb(x, y);
  else if (currentScreen == SCREEN_MENU) touchMenu(x, y);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  prefs.begin("pluto", false);
  loadSettings();
  initPalette();
  applyBacklight();

  sdReady = SD.begin();
  startWifi();

  vacActual = 0;
  resetHistories();

  bootAnimation();
  drawHomeScreen();
}

void loop() {
  M5.update();
  dnsServer.processNextRequest();
  server.handleClient();

  handleTouch();
  performAdjustStep();

  updateVacSimulation();
  updateHistories();
  refreshActiveGraphOnly();

  delay(8);
}
