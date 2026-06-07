#include <M5Unified.h>

// ============================================================
// 10_Pulse_CoreS3_Demo
// Pulse controller touchscreen concept for M5Stack CoreS3/SE
// Simulation only: this sketch does NOT fire GPIO outputs.
// ============================================================

// ---------- Global app state declared before ALL functions ----------
enum ScreenId {
  SCREEN_HOME,
  SCREEN_VAC,
  SCREEN_PPM,
  SCREEN_LIVE,
  SCREEN_DIAG,
  SCREEN_SETTINGS,
  SCREEN_ESTOP_CONFIRM,
  SCREEN_ESTOP_LATCHED
};

ScreenId currentScreen = SCREEN_HOME;
ScreenId lastScreenBeforeEStop = SCREEN_HOME;

bool darkTheme = true;
bool running = false;
bool priming = false;
bool diagPump = false;
bool diagPulse = false;
bool diagVacValve = false;
bool diagRelease = false;
bool emergencyLatched = false;

int vacTarget = 35;       // 0-100%
int vacActual = 0;        // simulated ramp toward target
int ppmTarget = 42;       // 0 = off
int backlightStep = 7;    // 1-10

uint16_t C_BG, C_CARD, C_CARD2, C_TEXT, C_MUTED, C_ACCENT, C_ACCENT2, C_WARN, C_DANGER, C_OK, C_LINE, C_GRAPH;

int sw = 320;
int sh = 240;

unsigned long lastGraphTick = 0;
unsigned long lastVacRampTick = 0;
unsigned long lastClockTick = 0;
unsigned long lastTouchRepeat = 0;
unsigned long touchStartMs = 0;
bool touchHeld = false;
int heldAction = 0;

// action IDs for hold repeat
const int ACT_NONE = 0;
const int ACT_VAC_MINUS = 1;
const int ACT_VAC_PLUS = 2;
const int ACT_PPM_MINUS = 3;
const int ACT_PPM_PLUS = 4;
const int ACT_BACKLIGHT_MINUS = 5;
const int ACT_BACKLIGHT_PLUS = 6;

// Graph buffers
const int GRAPH_W = 176;
int vacGraph[GRAPH_W];
int ppmGraph[GRAPH_W];
int liveVacBar = 0;
int livePpmBar = 0;
int phase = 0;

// ---------- Utility ----------
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void initPalette() {
  if (darkTheme) {
    C_BG      = M5.Display.color565(10, 14, 22);
    C_CARD    = M5.Display.color565(22, 30, 44);
    C_CARD2   = M5.Display.color565(30, 42, 58);
    C_TEXT    = M5.Display.color565(235, 242, 248);
    C_MUTED   = M5.Display.color565(140, 154, 170);
    C_ACCENT  = M5.Display.color565(45, 210, 180);
    C_ACCENT2 = M5.Display.color565(120, 160, 255);
    C_WARN    = M5.Display.color565(255, 190, 65);
    C_DANGER  = M5.Display.color565(230, 32, 52);
    C_OK      = M5.Display.color565(52, 210, 105);
    C_LINE    = M5.Display.color565(60, 75, 95);
    C_GRAPH   = M5.Display.color565(255, 210, 68);
  } else {
    C_BG      = M5.Display.color565(226, 236, 244);
    C_CARD    = M5.Display.color565(248, 250, 252);
    C_CARD2   = M5.Display.color565(220, 230, 240);
    C_TEXT    = M5.Display.color565(18, 28, 40);
    C_MUTED   = M5.Display.color565(90, 105, 120);
    C_ACCENT  = M5.Display.color565(0, 150, 135);
    C_ACCENT2 = M5.Display.color565(55, 95, 210);
    C_WARN    = M5.Display.color565(210, 130, 20);
    C_DANGER  = M5.Display.color565(210, 20, 42);
    C_OK      = M5.Display.color565(20, 155, 70);
    C_LINE    = M5.Display.color565(170, 188, 205);
    C_GRAPH   = M5.Display.color565(230, 145, 20);
  }
}

int brightnessToHardware() {
  return map(backlightStep, 1, 10, 35, 255);
}

void applyBacklight() {
  M5.Display.setBrightness(brightnessToHardware());
}

void fillAppBg() {
  M5.Display.fillScreen(C_BG);
}

void textAt(int x, int y, const char* txt, uint16_t color, int size) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, C_BG);
  M5.Display.setCursor(x, y);
  M5.Display.print(txt);
}

void drawHeader(const char* title, bool showBack, bool showEStop) {
  M5.Display.fillRoundRect(4, 4, 312, 32, 8, C_CARD);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(showBack ? 48 : 14, 12);
  M5.Display.print(title);

  if (showBack) {
    M5.Display.fillRoundRect(8, 8, 34, 24, 6, C_CARD2);
    M5.Display.setTextColor(C_TEXT, C_CARD2);
    M5.Display.setCursor(17, 13);
    M5.Display.print("<");
  }
  if (showEStop) {
    M5.Display.fillRoundRect(254, 8, 58, 24, 6, C_DANGER);
    M5.Display.setTextColor(TFT_WHITE, C_DANGER);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(265, 17);
    M5.Display.print("E-STOP");
  }
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg, int textSize) {
  M5.Display.fillRoundRect(x, y, w, h, 10, bg);
  M5.Display.drawRoundRect(x, y, w, h, 10, C_LINE);
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(fg, bg);
  int16_t tw = M5.Display.textWidth(label);
  int tx = x + (w - tw) / 2;
  int ty = y + (h - (8 * textSize)) / 2;
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label);
}

void drawValueBox(int x, int y, int w, int h, const char* label, int value, const char* suffix, uint16_t accent) {
  M5.Display.fillRoundRect(x, y, w, h, 10, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 10, accent);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 10, y + 8);
  M5.Display.print(label);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(x + 10, y + 24);
  M5.Display.print(value);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + w - 38, y + 42);
  M5.Display.print(suffix);
}

void drawMiniEStop() {
  drawButton(254, 8, 58, 24, "E-STOP", C_DANGER, TFT_WHITE, 1);
}

void goScreen(ScreenId s) {
  currentScreen = s;
  touchHeld = false;
  heldAction = ACT_NONE;
  fillAppBg();
  if (s == SCREEN_HOME) drawHeader("Pulse Control", false, true);
  if (s == SCREEN_VAC) drawHeader("VAC Control", true, true);
  if (s == SCREEN_PPM) drawHeader("PPM Control", true, true);
  if (s == SCREEN_LIVE) drawHeader("Live Monitor", true, true);
  if (s == SCREEN_DIAG) drawHeader("Manual Test", true, true);
  if (s == SCREEN_SETTINGS) drawHeader("Settings", true, true);
}

void refreshCurrentScreenFull();

void requestEStopConfirm() {
  lastScreenBeforeEStop = currentScreen;
  currentScreen = SCREEN_ESTOP_CONFIRM;
  fillAppBg();
  M5.Display.fillRect(0, 0, 320, 216, C_DANGER);
  M5.Display.setTextColor(TFT_WHITE, C_DANGER);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(42, 52);
  M5.Display.print("E-STOP?");
  M5.Display.setTextSize(2);
  M5.Display.setCursor(24, 98);
  M5.Display.print("PRESS RED SCREEN");
  M5.Display.setCursor(42, 125);
  M5.Display.print("TO RELEASE ALL");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(34, 165);
  M5.Display.print("Positive and negative pressure will release");
  M5.Display.fillRect(0, 216, 320, 24, C_OK);
  M5.Display.setTextColor(TFT_BLACK, C_OK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(75, 221);
  M5.Display.print("CANCEL / RETURN");
}

void triggerEmergencyStop() {
  running = false;
  priming = false;
  diagPump = false;
  diagPulse = false;
  diagVacValve = false;
  diagRelease = false;
  emergencyLatched = true;
  currentScreen = SCREEN_ESTOP_LATCHED;

  // Future real-output safety location:
  // digitalWrite(PUMP_PIN, LOW);
  // digitalWrite(PULSE_VALVE_PIN, SAFE_RELEASE_STATE);
  // digitalWrite(VAC_VALVE_PIN, SAFE_RELEASE_STATE);
  // digitalWrite(RELEASE_VALVE_PIN, SAFE_RELEASE_STATE);
  // Force all MOSFET/relay outputs safe here.

  M5.Display.fillScreen(C_DANGER);
  M5.Display.setTextColor(TFT_WHITE, C_DANGER);
  M5.Display.setTextSize(3);
  M5.Display.setCursor(34, 40);
  M5.Display.print("STOPPED");
  M5.Display.setTextSize(2);
  M5.Display.setCursor(32, 86);
  M5.Display.print("PRESSURE RELEASED");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(33, 130);
  M5.Display.print("Pump OFF  |  VAC vented  |  Pulse open");
  M5.Display.setCursor(47, 150);
  M5.Display.print("All simulated outputs forced safe");
  M5.Display.fillRect(0, 216, 320, 24, C_OK);
  M5.Display.setTextColor(TFT_BLACK, C_OK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(79, 221);
  M5.Display.print("RETURN HOME");
}

// ---------- Boot Animation ----------
void drawFlame(int x, int y, int flicker) {
  uint16_t red = M5.Display.color565(240, 40, 25);
  uint16_t orange = M5.Display.color565(255, 116, 20);
  uint16_t yellow = M5.Display.color565(255, 220, 55);
  M5.Display.fillTriangle(x, y + 22, x - 48, y + 2 + flicker, x - 18, y + 45, red);
  M5.Display.fillTriangle(x + 4, y + 18, x - 38, y + 8 - flicker, x - 10, y + 38, orange);
  M5.Display.fillTriangle(x + 8, y + 18, x - 24, y + 12 + flicker, x, y + 34, yellow);
  M5.Display.fillCircle(x - 32, y + 26, 12, orange);
  M5.Display.fillCircle(x - 16, y + 25, 8, yellow);
}

void drawMilkBottle(int x, int y, int eyeOffset) {
  uint16_t glass = M5.Display.color565(235, 245, 255);
  uint16_t milk = M5.Display.color565(255, 255, 250);
  uint16_t outline = M5.Display.color565(20, 35, 48);
  uint16_t cap = M5.Display.color565(70, 180, 255);
  uint16_t label = M5.Display.color565(50, 210, 180);

  drawFlame(x - 4, y + 17, (millis() / 90) % 8);
  M5.Display.fillRoundRect(x, y + 14, 72, 54, 13, glass);
  M5.Display.drawRoundRect(x, y + 14, 72, 54, 13, outline);
  M5.Display.fillRoundRect(x + 12, y, 48, 22, 8, glass);
  M5.Display.drawRoundRect(x + 12, y, 48, 22, 8, outline);
  M5.Display.fillRoundRect(x + 18, y - 8, 36, 12, 5, cap);
  M5.Display.drawRoundRect(x + 18, y - 8, 36, 12, 5, outline);

  M5.Display.fillRoundRect(x + 10, y + 42, 52, 20, 6, label);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK, label);
  M5.Display.setCursor(x + 22, y + 49);
  M5.Display.print("PULSE");

  M5.Display.fillCircle(x + 25, y + 28, 9, TFT_WHITE);
  M5.Display.fillCircle(x + 49, y + 28, 9, TFT_WHITE);
  M5.Display.fillCircle(x + 25 + eyeOffset, y + 28, 4, TFT_BLACK);
  M5.Display.fillCircle(x + 49 + eyeOffset, y + 28, 4, TFT_BLACK);
  M5.Display.drawLine(x + 30, y + 38, x + 43, y + 38, outline);
}

void bootAnimation() {
  M5.Display.fillScreen(M5.Display.color565(7, 10, 18));
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(M5.Display.color565(45, 210, 180), M5.Display.color565(7, 10, 18));
  M5.Display.setCursor(68, 18);
  M5.Display.print("PULSE SYSTEM");

  for (int x = -90; x <= 120; x += 10) {
    M5.Display.fillRect(0, 54, 320, 124, M5.Display.color565(7, 10, 18));
    drawMilkBottle(x, 88, 2);
    delay(65);
  }
  for (int i = 0; i < 5; i++) {
    int eyes = (i == 0) ? 5 : (i == 1) ? -5 : (i == 2) ? 5 : 0;
    M5.Display.fillRect(0, 54, 320, 124, M5.Display.color565(7, 10, 18));
    drawMilkBottle(120, 88, eyes);
    delay(340);
  }
  for (int x = 120; x <= 340; x += 12) {
    M5.Display.fillRect(0, 54, 320, 124, M5.Display.color565(7, 10, 18));
    drawMilkBottle(x, 88, 3);
    delay(42);
  }
}

// ---------- Screens ----------
void drawHome() {
  goScreen(SCREEN_HOME);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 44);
  M5.Display.print("Primary controls - simulation only");

  drawValueBox(12, 62, 142, 66, "VAC", vacTarget, "%", C_ACCENT);
  drawValueBox(166, 62, 142, 66, "PPM", ppmTarget, "", C_ACCENT2);

  drawButton(12, 140, 94, 42, running ? "STOP" : "START", running ? C_DANGER : C_OK, TFT_WHITE, 2);
  drawButton(113, 140, 94, 42, "VAC", C_CARD2, C_TEXT, 2);
  drawButton(214, 140, 94, 42, "PPM", C_CARD2, C_TEXT, 2);
  drawButton(12, 190, 94, 38, "LIVE", C_CARD2, C_TEXT, 2);
  drawButton(113, 190, 94, 38, "TEST", C_CARD2, C_TEXT, 2);
  drawButton(214, 190, 94, 38, "SET", C_CARD2, C_TEXT, 2);
}

void shiftGraph(int* graph, int w, int val) {
  for (int i = 0; i < w - 1; i++) graph[i] = graph[i + 1];
  graph[w - 1] = val;
}

void drawGraphFrame(int x, int y, int w, int h, const char* leftLabel, const char* title) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 8, y + 6);
  M5.Display.print(title);
  M5.Display.setCursor(x + 6, y + 22);
  M5.Display.print(leftLabel);
  M5.Display.setCursor(x + 6, y + h - 14);
  M5.Display.print("0");
  M5.Display.drawLine(x + 28, y + 22, x + 28, y + h - 18, C_LINE);
  M5.Display.drawLine(x + 28, y + h - 18, x + w - 8, y + h - 18, C_LINE);
}

void renderVacGraph() {
  int x = 12, y = 56, w = 196, h = 104;
  M5.Display.fillRect(x + 29, y + 19, w - 40, h - 39, C_CARD);
  int top = y + 22;
  int bottom = y + h - 18;
  int gx = x + 30;
  int gw = w - 42;
  int targetY = map(vacTarget, 0, 100, bottom, top);
  M5.Display.drawFastHLine(gx, targetY, gw, C_WARN);
  for (int i = 1; i < gw && i < GRAPH_W; i++) {
    int y1 = map(vacGraph[GRAPH_W - gw + i - 1], 0, 100, bottom, top);
    int y2 = map(vacGraph[GRAPH_W - gw + i], 0, 100, bottom, top);
    M5.Display.drawLine(gx + i - 1, y1, gx + i, y2, C_ACCENT);
  }
}

void drawVacScreen() {
  goScreen(SCREEN_VAC);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 42);
  M5.Display.print("Target vacuum control, 0% is OFF");
  drawGraphFrame(12, 56, 196, 104, "100%", "Actual ramp to target");
  renderVacGraph();
  drawValueBox(218, 56, 90, 54, "VAC", vacTarget, "%", C_ACCENT);
  drawButton(218, 119, 90, 48, "+", C_ACCENT, TFT_BLACK, 4);
  drawButton(218, 178, 90, 48, "-", C_CARD2, C_TEXT, 4);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 174);
  M5.Display.print("Line follows actual value");
  M5.Display.setCursor(16, 190);
  M5.Display.print("Yellow = user setting");
}

void renderPpmGraph() {
  int x = 12, y = 56, w = 196, h = 104;
  M5.Display.fillRect(x + 10, y + 19, w - 20, h - 38, C_CARD);
  int top = y + 28;
  int mid = y + 62;
  int bottom = y + h - 20;
  M5.Display.drawFastHLine(x + 10, mid, w - 20, C_LINE);
  for (int i = 1; i < w - 22 && i < GRAPH_W; i++) {
    int v1 = ppmGraph[GRAPH_W - (w - 22) + i - 1];
    int v2 = ppmGraph[GRAPH_W - (w - 22) + i];
    int y1 = mid - v1;
    int y2 = mid - v2;
    M5.Display.drawLine(x + 10 + i - 1, y1, x + 10 + i, y2, C_ACCENT2);
  }
}

void drawPpmScreen() {
  goScreen(SCREEN_PPM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 42);
  M5.Display.print("Set squeezes / pumps per minute");
  drawGraphFrame(12, 56, 196, 104, "", "Frequency follows PPM");
  renderPpmGraph();
  drawValueBox(218, 56, 90, 54, "PPM", ppmTarget, "", C_ACCENT2);
  drawButton(218, 119, 90, 48, "+", C_ACCENT2, TFT_WHITE, 4);
  drawButton(218, 178, 90, 48, "-", C_CARD2, C_TEXT, 4);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 174);
  M5.Display.print("0 PPM = OFF / flat line");
  M5.Display.setCursor(16, 190);
  M5.Display.print("Higher PPM = closer humps");
}

void drawBar(int x, int y, int w, int h, int value, int maxValue, uint16_t color, const char* label) {
  value = clampInt(value, 0, maxValue);
  M5.Display.fillRoundRect(x, y, w, h, 8, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 8, y + 6);
  M5.Display.print(label);
  int barH = h - 30;
  int barW = map(value, 0, maxValue, 0, w - 20);
  M5.Display.fillRoundRect(x + 10, y + 24, w - 20, barH, 5, C_CARD2);
  M5.Display.fillRoundRect(x + 10, y + 24, barW, barH, 5, color);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(x + 12, y + h - 22);
  M5.Display.print(value);
}

void drawLiveScreen() {
  goScreen(SCREEN_LIVE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 44);
  M5.Display.print(running ? "System running - simulated" : "System idle - simulated");
  drawBar(16, 64, 288, 62, ppmTarget, 120, C_ACCENT2, "PPM - Squeezes / Pumps per minute");
  drawBar(16, 140, 288, 62, vacActual, 100, C_ACCENT, "VAC - Actual simulated vacuum %");
  drawButton(16, 210, 88, 24, running ? "STOP" : "START", running ? C_DANGER : C_OK, TFT_WHITE, 1);
  drawButton(116, 210, 88, 24, "VAC", C_CARD2, C_TEXT, 1);
  drawButton(216, 210, 88, 24, "PPM", C_CARD2, C_TEXT, 1);
}

void drawDiagScreen() {
  goScreen(SCREEN_DIAG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 44);
  M5.Display.print("Manual output test UI - no real pins active");
  drawButton(14, 62, 140, 42, diagPump ? "PUMP ON" : "PUMP", diagPump ? C_OK : C_CARD2, diagPump ? TFT_BLACK : C_TEXT, 2);
  drawButton(166, 62, 140, 42, diagPulse ? "PULSE ON" : "PULSE", diagPulse ? C_OK : C_CARD2, diagPulse ? TFT_BLACK : C_TEXT, 2);
  drawButton(14, 116, 140, 42, diagVacValve ? "VAC ON" : "VAC VALVE", diagVacValve ? C_OK : C_CARD2, diagVacValve ? TFT_BLACK : C_TEXT, 2);
  drawButton(166, 116, 140, 42, diagRelease ? "REL ON" : "RELEASE", diagRelease ? C_OK : C_CARD2, diagRelease ? TFT_BLACK : C_TEXT, 2);
  drawButton(14, 178, 292, 44, "ALL OUTPUTS OFF", C_DANGER, TFT_WHITE, 2);
}

void drawSettingsScreen() {
  goScreen(SCREEN_SETTINGS);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 44);
  M5.Display.print("User settings mockup");
  drawButton(16, 62, 136, 46, darkTheme ? "DARK MODE" : "LIGHT MODE", C_CARD2, C_TEXT, 2);
  drawButton(168, 62, 136, 46, "TOGGLE", C_ACCENT, TFT_BLACK, 2);

  M5.Display.fillRoundRect(16, 126, 288, 74, 10, C_CARD);
  M5.Display.drawRoundRect(16, 126, 288, 74, 10, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(28, 136);
  M5.Display.print("Backlight brightness: ");
  M5.Display.print(backlightStep);
  M5.Display.print(" / 10");
  int sliderX = 28;
  int sliderY = 160;
  int sliderW = 210;
  M5.Display.fillRoundRect(sliderX, sliderY, sliderW, 14, 7, C_CARD2);
  int knobX = sliderX + map(backlightStep, 1, 10, 0, sliderW - 12);
  M5.Display.fillCircle(knobX + 6, sliderY + 7, 10, C_ACCENT);
  drawButton(244, 143, 26, 44, "-", C_CARD2, C_TEXT, 2);
  drawButton(276, 143, 26, 44, "+", C_ACCENT, TFT_BLACK, 2);
}

void refreshCurrentScreenFull() {
  if (currentScreen == SCREEN_HOME) drawHome();
  else if (currentScreen == SCREEN_VAC) drawVacScreen();
  else if (currentScreen == SCREEN_PPM) drawPpmScreen();
  else if (currentScreen == SCREEN_LIVE) drawLiveScreen();
  else if (currentScreen == SCREEN_DIAG) drawDiagScreen();
  else if (currentScreen == SCREEN_SETTINGS) drawSettingsScreen();
}

void updateValueBoxOnly() {
  if (currentScreen == SCREEN_VAC) {
    drawValueBox(218, 56, 90, 54, "VAC", vacTarget, "%", C_ACCENT);
    drawButton(218, 119, 90, 48, "+", C_ACCENT, TFT_BLACK, 4);
    drawButton(218, 178, 90, 48, "-", C_CARD2, C_TEXT, 4);
  } else if (currentScreen == SCREEN_PPM) {
    drawValueBox(218, 56, 90, 54, "PPM", ppmTarget, "", C_ACCENT2);
    drawButton(218, 119, 90, 48, "+", C_ACCENT2, TFT_WHITE, 4);
    drawButton(218, 178, 90, 48, "-", C_CARD2, C_TEXT, 4);
  } else if (currentScreen == SCREEN_SETTINGS) {
    drawSettingsScreen();
  }
}

// ---------- Value adjustment ----------
void changeVac(int dir, bool fast) {
  int step = fast ? 10 : 2;
  vacTarget = clampInt(vacTarget + dir * step, 0, 100);
  updateValueBoxOnly();
}

void changePpm(int dir, bool fast) {
  int step = fast ? 10 : 1;
  ppmTarget = clampInt(ppmTarget + dir * step, 0, 120);
  updateValueBoxOnly();
}

void changeBacklight(int dir, bool fast) {
  backlightStep = clampInt(backlightStep + dir, 1, 10);
  applyBacklight();
  updateValueBoxOnly();
}

void doHeldAction(bool fast) {
  if (heldAction == ACT_VAC_MINUS) changeVac(-1, fast);
  else if (heldAction == ACT_VAC_PLUS) changeVac(1, fast);
  else if (heldAction == ACT_PPM_MINUS) changePpm(-1, fast);
  else if (heldAction == ACT_PPM_PLUS) changePpm(1, fast);
  else if (heldAction == ACT_BACKLIGHT_MINUS) changeBacklight(-1, fast);
  else if (heldAction == ACT_BACKLIGHT_PLUS) changeBacklight(1, fast);
}

// ---------- Touch handling ----------
void handleHomeTouch(int x, int y) {
  if (inRect(x, y, 254, 8, 58, 24)) { requestEStopConfirm(); return; }
  if (inRect(x, y, 12, 140, 94, 42)) { running = !running; drawHome(); return; }
  if (inRect(x, y, 113, 140, 94, 42)) { drawVacScreen(); return; }
  if (inRect(x, y, 214, 140, 94, 42)) { drawPpmScreen(); return; }
  if (inRect(x, y, 12, 190, 94, 38)) { drawLiveScreen(); return; }
  if (inRect(x, y, 113, 190, 94, 38)) { drawDiagScreen(); return; }
  if (inRect(x, y, 214, 190, 94, 38)) { drawSettingsScreen(); return; }
}

void commonTopTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24)) { drawHome(); return; }
  if (inRect(x, y, 254, 8, 58, 24)) { requestEStopConfirm(); return; }
}

void handleVacTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24) || inRect(x, y, 254, 8, 58, 24)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 218, 119, 90, 48)) { heldAction = ACT_VAC_PLUS; changeVac(1, false); return; }
  if (inRect(x, y, 218, 178, 90, 48)) { heldAction = ACT_VAC_MINUS; changeVac(-1, false); return; }
}

void handlePpmTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24) || inRect(x, y, 254, 8, 58, 24)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 218, 119, 90, 48)) { heldAction = ACT_PPM_PLUS; changePpm(1, false); return; }
  if (inRect(x, y, 218, 178, 90, 48)) { heldAction = ACT_PPM_MINUS; changePpm(-1, false); return; }
}

void handleLiveTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24) || inRect(x, y, 254, 8, 58, 24)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 16, 210, 88, 24)) { running = !running; drawLiveScreen(); return; }
  if (inRect(x, y, 116, 210, 88, 24)) { drawVacScreen(); return; }
  if (inRect(x, y, 216, 210, 88, 24)) { drawPpmScreen(); return; }
}

void handleDiagTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24) || inRect(x, y, 254, 8, 58, 24)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 14, 62, 140, 42)) diagPump = !diagPump;
  else if (inRect(x, y, 166, 62, 140, 42)) diagPulse = !diagPulse;
  else if (inRect(x, y, 14, 116, 140, 42)) diagVacValve = !diagVacValve;
  else if (inRect(x, y, 166, 116, 140, 42)) diagRelease = !diagRelease;
  else if (inRect(x, y, 14, 178, 292, 44)) {
    diagPump = false; diagPulse = false; diagVacValve = false; diagRelease = false;
  }
  drawDiagScreen();
}

void handleSettingsTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 34, 24) || inRect(x, y, 254, 8, 58, 24)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 168, 62, 136, 46) || inRect(x, y, 16, 62, 136, 46)) {
    darkTheme = !darkTheme;
    initPalette();
    drawSettingsScreen();
    return;
  }
  if (inRect(x, y, 244, 143, 26, 44)) { heldAction = ACT_BACKLIGHT_MINUS; changeBacklight(-1, false); return; }
  if (inRect(x, y, 276, 143, 26, 44)) { heldAction = ACT_BACKLIGHT_PLUS; changeBacklight(1, false); return; }
}

void handleTouch() {
  M5.update();
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    touchStartMs = millis();
    lastTouchRepeat = millis();
    touchHeld = true;
    heldAction = ACT_NONE;
    int x = t.x;
    int y = t.y;

    if (currentScreen == SCREEN_ESTOP_CONFIRM) {
      if (y >= 216) { refreshCurrentScreenFull(); return; }
      triggerEmergencyStop(); return;
    }
    if (currentScreen == SCREEN_ESTOP_LATCHED) {
      if (y >= 216) { emergencyLatched = false; drawHome(); return; }
      return;
    }

    if (currentScreen == SCREEN_HOME) handleHomeTouch(x, y);
    else if (currentScreen == SCREEN_VAC) handleVacTouch(x, y);
    else if (currentScreen == SCREEN_PPM) handlePpmTouch(x, y);
    else if (currentScreen == SCREEN_LIVE) handleLiveTouch(x, y);
    else if (currentScreen == SCREEN_DIAG) handleDiagTouch(x, y);
    else if (currentScreen == SCREEN_SETTINGS) handleSettingsTouch(x, y);
  }

  if (t.wasReleased()) {
    touchHeld = false;
    heldAction = ACT_NONE;
  }

  if (touchHeld && heldAction != ACT_NONE && t.isPressed()) {
    unsigned long now = millis();
    bool fast = (now - touchStartMs) > 3000;
    unsigned long interval = fast ? 160 : 360;
    if (now - lastTouchRepeat >= interval) {
      lastTouchRepeat = now;
      doHeldAction(fast);
    }
  }
}

// ---------- Simulation updates ----------
void updateVacActual() {
  unsigned long now = millis();
  if (now - lastVacRampTick < 80) return;
  lastVacRampTick = now;
  if (vacActual < vacTarget) vacActual++;
  else if (vacActual > vacTarget) vacActual--;
}

void updateGraphs() {
  unsigned long now = millis();
  if (now - lastGraphTick < 70) return;
  lastGraphTick = now;
  updateVacActual();

  shiftGraph(vacGraph, GRAPH_W, vacActual);

  int ppmAmp = 28;
  int nextPpm = 0;
  if (ppmTarget > 0) {
    int speed = map(ppmTarget, 1, 120, 1, 10);
    phase = (phase + speed) % 64;
    // Hump shape: consistent height, frequency changes with phase speed.
    if (phase < 16) nextPpm = map(phase, 0, 15, 0, ppmAmp);
    else if (phase < 32) nextPpm = map(phase, 16, 31, ppmAmp, 0);
    else nextPpm = 0;
  } else {
    nextPpm = 0;
  }
  shiftGraph(ppmGraph, GRAPH_W, nextPpm);

  if (currentScreen == SCREEN_VAC) renderVacGraph();
  else if (currentScreen == SCREEN_PPM) renderPpmGraph();
  else if (currentScreen == SCREEN_LIVE && now - lastClockTick > 300) {
    lastClockTick = now;
    drawLiveScreen();
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  sw = M5.Display.width();
  sh = M5.Display.height();
  Serial.begin(115200);

  initPalette();
  applyBacklight();
  for (int i = 0; i < GRAPH_W; i++) { vacGraph[i] = 0; ppmGraph[i] = 0; }

  bootAnimation();
  drawHome();
  Serial.println("10_Pulse_CoreS3_Demo booted. Simulation only.");
}

void loop() {
  handleTouch();
  updateGraphs();
}
