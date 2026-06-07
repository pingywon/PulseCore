#include <M5Unified.h>
#include <math.h>

// =============================================================
// 8_Pulse_CoreS3_Demo
// M5Stack CoreS3/SE UI sizing demo only.
// Simulation only: no real hardware outputs are fired.
// =============================================================

// -------------------- Global App State --------------------
enum ScreenId {
  SCREEN_HOME,
  SCREEN_PPM,
  SCREEN_VAC,
  SCREEN_LIVE,
  SCREEN_MANUAL,
  SCREEN_SETTINGS,
  SCREEN_ESTOP_CONFIRM,
  SCREEN_ESTOP_LATCHED
};

ScreenId currentScreen = SCREEN_HOME;
ScreenId previousScreen = SCREEN_HOME;

bool appRunning = false;
bool darkTheme = true;
int backlightLevel = 8;      // 0-10 user scale
int ppmValue = 42;           // 0 = OFF
int vacValue = 55;           // 0 = OFF, 100 = MAX
float vacActual = 55.0;      // simulated live ramp toward vacValue

// Touch hold/repeat state
bool holdActive = false;
int holdAction = 0;
unsigned long holdStartedAt = 0;
unsigned long lastHoldRepeat = 0;
const unsigned long HOLD_DELAY_MS = 330;
const unsigned long HOLD_REPEAT_MS = 95;
const unsigned long HOLD_FAST_MS = 3000;

// Display dimensions
int W = 320;
int H = 240;

// Theme palette
uint16_t C_BG;
uint16_t C_PANEL;
uint16_t C_PANEL2;
uint16_t C_TEXT;
uint16_t C_MUTED;
uint16_t C_ACCENT;
uint16_t C_ACCENT2;
uint16_t C_WARN;
uint16_t C_DANGER;
uint16_t C_OK;
uint16_t C_LINE;
uint16_t C_GRAPH_BG;

// Button action IDs
const int ACT_NONE = 0;
const int ACT_HOME = 1;
const int ACT_PPM = 2;
const int ACT_VAC = 3;
const int ACT_LIVE = 4;
const int ACT_MANUAL = 5;
const int ACT_SETTINGS = 6;
const int ACT_RUN_TOGGLE = 7;
const int ACT_ESTOP_OPEN = 8;
const int ACT_ESTOP_FIRE = 9;
const int ACT_ESTOP_CANCEL = 10;
const int ACT_PPM_PLUS = 11;
const int ACT_PPM_MINUS = 12;
const int ACT_VAC_PLUS = 13;
const int ACT_VAC_MINUS = 14;
const int ACT_THEME_TOGGLE = 15;
const int ACT_BACKLIGHT_PLUS = 16;
const int ACT_BACKLIGHT_MINUS = 17;
const int ACT_TEST1 = 18;
const int ACT_TEST2 = 19;
const int ACT_TEST3 = 20;

// Fixed layout values for in-place redraws
const int VALUE_X = 16;
const int VALUE_Y = 44;
const int VALUE_W = 118;
const int VALUE_H = 58;
const int GRAPH_X = 42;
const int GRAPH_Y = 104;
const int GRAPH_W = 250;
const int GRAPH_H = 58;

// Graph histories
const int HIST_LEN = 120;
int vacHist[HIST_LEN];
int ppmHist[HIST_LEN];
int histIndex = 0;
unsigned long lastGraphTick = 0;
unsigned long pulsePhaseMs = 0;
unsigned long lastPulseTick = 0;

// -------------------- Small Utility Functions --------------------
int clampInt(int v, int low, int high) {
  if (v < low) return low;
  if (v > high) return high;
  return v;
}

bool insideRect(int tx, int ty, int x, int y, int w, int h) {
  return (tx >= x && tx <= x + w && ty >= y && ty <= y + h);
}

void initPalette() {
  if (darkTheme) {
    C_BG = M5.Display.color565(12, 15, 23);
    C_PANEL = M5.Display.color565(26, 31, 46);
    C_PANEL2 = M5.Display.color565(38, 46, 66);
    C_TEXT = M5.Display.color565(242, 246, 255);
    C_MUTED = M5.Display.color565(160, 172, 192);
    C_ACCENT = M5.Display.color565(61, 180, 255);      // PPM blue
    C_ACCENT2 = M5.Display.color565(186, 116, 255);    // VAC purple
    C_WARN = M5.Display.color565(255, 190, 65);
    C_DANGER = M5.Display.color565(225, 38, 50);
    C_OK = M5.Display.color565(70, 220, 135);
    C_LINE = M5.Display.color565(78, 94, 125);
    C_GRAPH_BG = M5.Display.color565(17, 22, 35);
  } else {
    C_BG = M5.Display.color565(230, 237, 246);
    C_PANEL = M5.Display.color565(255, 255, 255);
    C_PANEL2 = M5.Display.color565(210, 222, 238);
    C_TEXT = M5.Display.color565(22, 29, 40);
    C_MUTED = M5.Display.color565(80, 92, 110);
    C_ACCENT = M5.Display.color565(0, 104, 185);
    C_ACCENT2 = M5.Display.color565(112, 58, 185);
    C_WARN = M5.Display.color565(200, 125, 16);
    C_DANGER = M5.Display.color565(208, 33, 45);
    C_OK = M5.Display.color565(20, 155, 83);
    C_LINE = M5.Display.color565(145, 160, 180);
    C_GRAPH_BG = M5.Display.color565(242, 246, 252);
  }
}

int brightnessToHardware() {
  // 10 brightness stops; minimum kept visible for demos.
  return map(backlightLevel, 0, 10, 25, 255);
}

void applyBacklight() {
  M5.Display.setBrightness(brightnessToHardware());
}

void forceAllOutputsSafeSimulationOnly() {
  // FUTURE REAL HARDWARE SAFETY LOCATION:
  // - pump OFF
  // - disable drive outputs
  // - open/release positive pressure path
  // - open/release negative pressure / VAC path
  // - force MOSFET/relay/solenoid pins to safe state
  appRunning = false;
  ppmValue = ppmValue; // keep user setting visible; no hardware action in this demo
}

// -------------------- Drawing Helpers --------------------
void setText(int size, uint16_t color) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t textColor, int textSize) {
  M5.Display.fillRoundRect(x + 2, y + 3, w, h, 12, M5.Display.color565(0, 0, 0));
  M5.Display.fillRoundRect(x, y, w, h, 12, fill);
  M5.Display.drawRoundRect(x, y, w, h, 12, C_LINE);
  setText(textSize, textColor);
  int charW = 6 * textSize;
  int charH = 8 * textSize;
  int len = strlen(label);
  int tx = x + (w - len * charW) / 2;
  int ty = y + (h - charH) / 2;
  if (tx < x + 4) tx = x + 4;
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label);
}

void drawHeader(const char* title) {
  M5.Display.fillRect(0, 0, W, 34, C_PANEL2);
  setText(2, C_TEXT);
  M5.Display.setCursor(10, 8);
  M5.Display.print(title);
  drawButton(W - 66, 5, 60, 24, "E-STOP", C_DANGER, WHITE, 1);
}

void drawBackButton() {
  drawButton(8, 202, 76, 30, "HOME", C_PANEL2, C_TEXT, 1);
}

void drawTinyStatusBar() {
  M5.Display.fillRect(0, H - 12, W, 12, C_PANEL2);
  setText(1, C_MUTED);
  M5.Display.setCursor(8, H - 10);
  M5.Display.print("DEMO ONLY  |  NO OUTPUTS ACTIVE");
}

void drawValueBox(const char* label, int value, const char* suffix, uint16_t color) {
  M5.Display.fillRoundRect(VALUE_X, VALUE_Y, VALUE_W, VALUE_H, 12, C_PANEL);
  M5.Display.drawRoundRect(VALUE_X, VALUE_Y, VALUE_W, VALUE_H, 12, C_LINE);
  setText(1, C_MUTED);
  M5.Display.setCursor(VALUE_X + 10, VALUE_Y + 8);
  M5.Display.print(label);
  setText(3, color);
  M5.Display.setCursor(VALUE_X + 12, VALUE_Y + 27);
  M5.Display.print(value);
  if (suffix != NULL) M5.Display.print(suffix);
}

void drawAxisBox(int x, int y, int w, int h, const char* topLabel, const char* midLabel, const char* botLabel) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_GRAPH_BG);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);
  setText(1, C_MUTED);
  M5.Display.setCursor(x - 25, y + 2);
  M5.Display.print(topLabel);
  M5.Display.setCursor(x - 19, y + h / 2 - 4);
  M5.Display.print(midLabel);
  M5.Display.setCursor(x - 13, y + h - 10);
  M5.Display.print(botLabel);
  for (int i = 1; i < 4; i++) {
    int yy = y + (h * i) / 4;
    M5.Display.drawLine(x + 4, yy, x + w - 6, yy, C_PANEL2);
  }
}

void seedGraphs() {
  for (int i = 0; i < HIST_LEN; i++) {
    vacHist[i] = (int)vacActual;
    ppmHist[i] = 0;
  }
}

void drawVacGraph() {
  drawAxisBox(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, "100", "50", "0");

  // target line
  int targetY = GRAPH_Y + GRAPH_H - 5 - map(vacValue, 0, 100, 0, GRAPH_H - 12);
  M5.Display.drawLine(GRAPH_X + 5, targetY, GRAPH_X + GRAPH_W - 7, targetY, C_WARN);

  int lastX = GRAPH_X + 6;
  int lastY = GRAPH_Y + GRAPH_H - 5 - map(vacHist[(histIndex + 1) % HIST_LEN], 0, 100, 0, GRAPH_H - 12);
  for (int i = 1; i < GRAPH_W - 13; i++) {
    int idx = (histIndex + 1 + map(i, 0, GRAPH_W - 14, 0, HIST_LEN - 1)) % HIST_LEN;
    int yy = GRAPH_Y + GRAPH_H - 5 - map(vacHist[idx], 0, 100, 0, GRAPH_H - 12);
    int xx = GRAPH_X + 6 + i;
    M5.Display.drawLine(lastX, lastY, xx, yy, C_ACCENT2);
    lastX = xx;
    lastY = yy;
  }
}

void drawPpmGraph() {
  drawAxisBox(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, "", "", "");
  setText(1, C_MUTED);
  M5.Display.setCursor(GRAPH_X - 30, GRAPH_Y + 20);
  M5.Display.print("Pulse");

  if (ppmValue <= 0) {
    setText(2, C_MUTED);
    M5.Display.setCursor(GRAPH_X + GRAPH_W / 2 - 24, GRAPH_Y + GRAPH_H / 2 - 8);
    M5.Display.print("OFF");
    return;
  }

  int lastX = GRAPH_X + 6;
  int lastY = GRAPH_Y + GRAPH_H - 8 - ppmHist[(histIndex + 1) % HIST_LEN];
  for (int i = 1; i < GRAPH_W - 13; i++) {
    int idx = (histIndex + 1 + map(i, 0, GRAPH_W - 14, 0, HIST_LEN - 1)) % HIST_LEN;
    int yy = GRAPH_Y + GRAPH_H - 8 - ppmHist[idx];
    int xx = GRAPH_X + 6 + i;
    M5.Display.drawLine(lastX, lastY, xx, yy, C_ACCENT);
    lastX = xx;
    lastY = yy;
  }
}

void drawLiveBars() {
  // PPM bar
  M5.Display.fillRoundRect(18, 70, 284, 48, 10, C_PANEL);
  M5.Display.drawRoundRect(18, 70, 284, 48, 10, C_LINE);
  setText(1, C_MUTED);
  M5.Display.setCursor(28, 78);
  M5.Display.print("PPM");
  int ppmFill = map(ppmValue, 0, 100, 0, 200);
  M5.Display.fillRoundRect(78, 83, ppmFill, 17, 6, C_ACCENT);
  M5.Display.drawRoundRect(78, 83, 200, 17, 6, C_LINE);
  setText(2, C_TEXT);
  M5.Display.setCursor(284, 82);
  M5.Display.print(ppmValue);

  // VAC bar
  M5.Display.fillRoundRect(18, 130, 284, 48, 10, C_PANEL);
  M5.Display.drawRoundRect(18, 130, 284, 48, 10, C_LINE);
  setText(1, C_MUTED);
  M5.Display.setCursor(28, 138);
  M5.Display.print("VAC");
  int vacFill = map((int)vacActual, 0, 100, 0, 200);
  M5.Display.fillRoundRect(78, 143, vacFill, 17, 6, C_ACCENT2);
  M5.Display.drawRoundRect(78, 143, 200, 17, 6, C_LINE);
  setText(2, C_TEXT);
  M5.Display.setCursor(284, 142);
  M5.Display.print((int)vacActual);
}

void updateLiveBarsOnly() {
  if (currentScreen != SCREEN_LIVE) return;
  M5.Display.fillRect(16, 68, 288, 112, C_BG);
  drawLiveBars();
}

// -------------------- Boot Animation --------------------
void drawBootBottle(int x, int y) {
  // Old simpler boot style: larger moving flaming milk bottle.
  M5.Display.fillTriangle(x - 48, y + 30, x - 16, y + 8, x - 18, y + 48, M5.Display.color565(255, 82, 25));
  M5.Display.fillTriangle(x - 38, y + 30, x - 8, y + 16, x - 12, y + 42, M5.Display.color565(255, 190, 30));
  M5.Display.fillRoundRect(x, y, 52, 74, 12, M5.Display.color565(245, 249, 255));
  M5.Display.drawRoundRect(x, y, 52, 74, 12, M5.Display.color565(70, 90, 125));
  M5.Display.fillRoundRect(x + 13, y - 20, 26, 24, 6, M5.Display.color565(235, 242, 255));
  M5.Display.drawRoundRect(x + 13, y - 20, 26, 24, 6, M5.Display.color565(70, 90, 125));
  M5.Display.fillRect(x + 9, y + 48, 34, 14, M5.Display.color565(210, 232, 255));
}

void bootAnimation() {
  M5.Display.fillScreen(C_BG);
  setText(2, C_TEXT);
  M5.Display.setCursor(74, 22);
  M5.Display.print("PULSE DEMO");
  for (int x = -60; x <= 355; x += 10) {
    M5.Display.fillRect(0, 54, W, 140, C_BG);
    drawBootBottle(x, 102);
    delay(34);
  }
}

// -------------------- Screens --------------------
void drawHome() {
  currentScreen = SCREEN_HOME;
  M5.Display.fillScreen(C_BG);
  drawHeader("Pulse Control");
  drawButton(16, 46, 136, 62, appRunning ? "STOP" : "START", appRunning ? C_WARN : C_OK, BLACK, 2);
  drawButton(168, 46, 136, 62, "LIVE", C_ACCENT, WHITE, 2);
  drawButton(16, 122, 136, 62, "VAC", C_ACCENT2, WHITE, 2);
  drawButton(168, 122, 136, 62, "PPM", C_ACCENT, WHITE, 2);
  drawButton(16, 194, 90, 32, "TEST", C_PANEL2, C_TEXT, 1);
  drawButton(116, 194, 90, 32, "SET", C_PANEL2, C_TEXT, 1);
  drawButton(216, 194, 88, 32, "SAFE", C_DANGER, WHITE, 1);
  drawTinyStatusBar();
}

void drawControlFrame(bool ppmScreen) {
  if (ppmScreen) currentScreen = SCREEN_PPM;
  else currentScreen = SCREEN_VAC;

  M5.Display.fillScreen(C_BG);
  drawHeader(ppmScreen ? "PPM Control" : "VAC Control");

  if (ppmScreen) {
    drawValueBox("PPM", ppmValue, "", C_ACCENT);
    setText(1, C_MUTED);
    M5.Display.setCursor(148, 46);
    M5.Display.print("Adjust squeezes /");
    M5.Display.setCursor(148, 60);
    M5.Display.print("pumps per minute.");
    M5.Display.setCursor(148, 74);
    M5.Display.print("0 = OFF");
    drawPpmGraph();
  } else {
    drawValueBox("VAC", vacValue, "%", C_ACCENT2);
    setText(1, C_MUTED);
    M5.Display.setCursor(148, 46);
    M5.Display.print("Target vacuum pull.");
    M5.Display.setCursor(148, 60);
    M5.Display.print("Graph ramps to setpoint.");
    M5.Display.setCursor(148, 74);
    M5.Display.print("0% = OFF");
    drawVacGraph();
  }

  // Bigger buttons; graph is smaller and high enough to stay out of the way.
  drawButton(22, 168, 132, 56, "-", C_PANEL2, C_TEXT, 4);
  drawButton(166, 168, 132, 56, "+", ppmScreen ? C_ACCENT : C_ACCENT2, WHITE, 4);
  drawButton(8, 202, 70, 30, "HOME", C_PANEL2, C_TEXT, 1);
}

void drawLive() {
  currentScreen = SCREEN_LIVE;
  M5.Display.fillScreen(C_BG);
  drawHeader("Live Status");
  setText(1, C_MUTED);
  M5.Display.setCursor(18, 45);
  M5.Display.print("Live demo bars for operating values");
  drawLiveBars();
  drawBackButton();
  drawButton(202, 202, 100, 30, appRunning ? "STOP" : "START", appRunning ? C_WARN : C_OK, BLACK, 1);
}

void drawManual() {
  currentScreen = SCREEN_MANUAL;
  M5.Display.fillScreen(C_BG);
  drawHeader("Manual Test");
  setText(1, C_MUTED);
  M5.Display.setCursor(18, 46);
  M5.Display.print("Visual test buttons only. No outputs fire.");
  drawButton(18, 70, 132, 52, "PUMP", C_PANEL2, C_TEXT, 2);
  drawButton(170, 70, 132, 52, "VALVE", C_PANEL2, C_TEXT, 2);
  drawButton(18, 136, 132, 52, "RELEASE", C_PANEL2, C_TEXT, 2);
  drawButton(170, 136, 132, 52, "PULSE", C_PANEL2, C_TEXT, 2);
  drawBackButton();
}

void drawBacklightSlider() {
  M5.Display.fillRoundRect(104, 144, 112, 26, 8, C_PANEL);
  M5.Display.drawRoundRect(104, 144, 112, 26, 8, C_LINE);
  int fillW = map(backlightLevel, 0, 10, 0, 108);
  M5.Display.fillRoundRect(106, 146, fillW, 22, 7, C_OK);
  setText(1, C_TEXT);
  M5.Display.setCursor(130, 153);
  M5.Display.print(backlightLevel);
  M5.Display.print(" / 10");
}

void drawSettings() {
  currentScreen = SCREEN_SETTINGS;
  M5.Display.fillScreen(C_BG);
  drawHeader("Settings");
  setText(2, C_TEXT);
  M5.Display.setCursor(18, 52);
  M5.Display.print("Theme");
  drawButton(152, 42, 136, 42, darkTheme ? "DARK" : "LIGHT", C_ACCENT, WHITE, 2);
  setText(2, C_TEXT);
  M5.Display.setCursor(18, 104);
  M5.Display.print("Backlight");
  drawButton(18, 137, 74, 46, "-", C_PANEL2, C_TEXT, 3);
  drawBacklightSlider();
  drawButton(224, 137, 74, 46, "+", C_PANEL2, C_TEXT, 3);
  drawBackButton();
}

void drawEstopConfirm() {
  currentScreen = SCREEN_ESTOP_CONFIRM;
  M5.Display.fillScreen(C_DANGER);
  setText(3, WHITE);
  M5.Display.setCursor(28, 34);
  M5.Display.print("E-STOP?");
  setText(2, WHITE);
  M5.Display.setCursor(24, 84);
  M5.Display.print("Press red screen");
  M5.Display.setCursor(24, 112);
  M5.Display.print("to release all");
  M5.Display.setCursor(24, 140);
  M5.Display.print("pressure now.");
  M5.Display.fillRect(0, H - 24, W, 24, C_OK);
  setText(2, BLACK);
  M5.Display.setCursor(84, H - 20);
  M5.Display.print("CANCEL / BACK");
}

void drawEstopLatched() {
  currentScreen = SCREEN_ESTOP_LATCHED;
  M5.Display.fillScreen(C_DANGER);
  setText(3, WHITE);
  M5.Display.setCursor(18, 28);
  M5.Display.print("EMERGENCY");
  M5.Display.setCursor(18, 62);
  M5.Display.print("STOPPED");
  setText(2, WHITE);
  M5.Display.setCursor(20, 116);
  M5.Display.print("PRESSURE RELEASED");
  setText(1, WHITE);
  M5.Display.setCursor(20, 152);
  M5.Display.print("Positive + negative pressure safe.");
  M5.Display.setCursor(20, 168);
  M5.Display.print("Future outputs forced OFF/OPEN.");
  M5.Display.fillRect(0, H - 24, W, 24, C_OK);
  setText(2, BLACK);
  M5.Display.setCursor(70, H - 20);
  M5.Display.print("RETURN TO HOME");
}

void redrawCurrentScreen() {
  switch (currentScreen) {
    case SCREEN_HOME: drawHome(); break;
    case SCREEN_PPM: drawControlFrame(true); break;
    case SCREEN_VAC: drawControlFrame(false); break;
    case SCREEN_LIVE: drawLive(); break;
    case SCREEN_MANUAL: drawManual(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
    case SCREEN_ESTOP_CONFIRM: drawEstopConfirm(); break;
    case SCREEN_ESTOP_LATCHED: drawEstopLatched(); break;
  }
}

// -------------------- In-place Updates --------------------
void updatePpmValueOnly() {
  if (currentScreen == SCREEN_PPM) {
    drawValueBox("PPM", ppmValue, "", C_ACCENT);
  } else if (currentScreen == SCREEN_LIVE) {
    updateLiveBarsOnly();
  }
}

void updateVacValueOnly() {
  if (currentScreen == SCREEN_VAC) {
    drawValueBox("VAC", vacValue, "%", C_ACCENT2);
  } else if (currentScreen == SCREEN_LIVE) {
    updateLiveBarsOnly();
  }
}

void updateSettingsBacklightOnly() {
  if (currentScreen == SCREEN_SETTINGS) {
    drawBacklightSlider();
  }
}

// -------------------- Input Handling --------------------
void changePpm(int delta) {
  ppmValue = clampInt(ppmValue + delta, 0, 100);
  updatePpmValueOnly();
}

void changeVac(int delta) {
  vacValue = clampInt(vacValue + delta, 0, 100);
  updateVacValueOnly();
}

void changeBacklight(int delta) {
  backlightLevel = clampInt(backlightLevel + delta, 0, 10);
  applyBacklight();
  updateSettingsBacklightOnly();
}

void doAction(int action, bool heldFast) {
  if (action == ACT_NONE) return;

  switch (action) {
    case ACT_HOME: drawHome(); break;
    case ACT_PPM: drawControlFrame(true); break;
    case ACT_VAC: drawControlFrame(false); break;
    case ACT_LIVE: drawLive(); break;
    case ACT_MANUAL: drawManual(); break;
    case ACT_SETTINGS: drawSettings(); break;
    case ACT_RUN_TOGGLE:
      appRunning = !appRunning;
      redrawCurrentScreen();
      break;
    case ACT_ESTOP_OPEN:
      previousScreen = currentScreen;
      drawEstopConfirm();
      break;
    case ACT_ESTOP_FIRE:
      forceAllOutputsSafeSimulationOnly();
      drawEstopLatched();
      break;
    case ACT_ESTOP_CANCEL:
      if (currentScreen == SCREEN_ESTOP_LATCHED) drawHome();
      else {
        currentScreen = previousScreen;
        redrawCurrentScreen();
      }
      break;
    case ACT_PPM_PLUS:
      changePpm(heldFast ? 10 : 1);
      break;
    case ACT_PPM_MINUS:
      changePpm(heldFast ? -10 : -1);
      break;
    case ACT_VAC_PLUS:
      changeVac(heldFast ? 10 : 2);
      break;
    case ACT_VAC_MINUS:
      changeVac(heldFast ? -10 : -2);
      break;
    case ACT_THEME_TOGGLE:
      darkTheme = !darkTheme;
      initPalette();
      redrawCurrentScreen();
      break;
    case ACT_BACKLIGHT_PLUS:
      changeBacklight(1);
      break;
    case ACT_BACKLIGHT_MINUS:
      changeBacklight(-1);
      break;
    case ACT_TEST1:
    case ACT_TEST2:
    case ACT_TEST3:
      M5.Display.fillRect(0, 190, W, 10, C_OK);
      delay(70);
      redrawCurrentScreen();
      break;
  }
}

bool actionCanHold(int action) {
  return action == ACT_PPM_PLUS || action == ACT_PPM_MINUS ||
         action == ACT_VAC_PLUS || action == ACT_VAC_MINUS ||
         action == ACT_BACKLIGHT_PLUS || action == ACT_BACKLIGHT_MINUS;
}

int hitTest(int tx, int ty) {
  if (currentScreen == SCREEN_ESTOP_CONFIRM || currentScreen == SCREEN_ESTOP_LATCHED) {
    if (insideRect(tx, ty, 0, H - 24, W, 24)) return ACT_ESTOP_CANCEL;
    if (currentScreen == SCREEN_ESTOP_CONFIRM) return ACT_ESTOP_FIRE;
    return ACT_NONE;
  }

  if (insideRect(tx, ty, W - 66, 5, 60, 24)) return ACT_ESTOP_OPEN;

  if (currentScreen == SCREEN_HOME) {
    if (insideRect(tx, ty, 16, 46, 136, 62)) return ACT_RUN_TOGGLE;
    if (insideRect(tx, ty, 168, 46, 136, 62)) return ACT_LIVE;
    if (insideRect(tx, ty, 16, 122, 136, 62)) return ACT_VAC;
    if (insideRect(tx, ty, 168, 122, 136, 62)) return ACT_PPM;
    if (insideRect(tx, ty, 16, 194, 90, 32)) return ACT_MANUAL;
    if (insideRect(tx, ty, 116, 194, 90, 32)) return ACT_SETTINGS;
    if (insideRect(tx, ty, 216, 194, 88, 32)) return ACT_ESTOP_OPEN;
  }

  if (currentScreen == SCREEN_PPM) {
    if (insideRect(tx, ty, 22, 168, 132, 56)) return ACT_PPM_MINUS;
    if (insideRect(tx, ty, 166, 168, 132, 56)) return ACT_PPM_PLUS;
    if (insideRect(tx, ty, 8, 202, 70, 30)) return ACT_HOME;
  }

  if (currentScreen == SCREEN_VAC) {
    if (insideRect(tx, ty, 22, 168, 132, 56)) return ACT_VAC_MINUS;
    if (insideRect(tx, ty, 166, 168, 132, 56)) return ACT_VAC_PLUS;
    if (insideRect(tx, ty, 8, 202, 70, 30)) return ACT_HOME;
  }

  if (currentScreen == SCREEN_LIVE) {
    if (insideRect(tx, ty, 8, 202, 76, 30)) return ACT_HOME;
    if (insideRect(tx, ty, 202, 202, 100, 30)) return ACT_RUN_TOGGLE;
  }

  if (currentScreen == SCREEN_MANUAL) {
    if (insideRect(tx, ty, 18, 70, 132, 52)) return ACT_TEST1;
    if (insideRect(tx, ty, 170, 70, 132, 52)) return ACT_TEST1;
    if (insideRect(tx, ty, 18, 136, 132, 52)) return ACT_TEST2;
    if (insideRect(tx, ty, 170, 136, 132, 52)) return ACT_TEST3;
    if (insideRect(tx, ty, 8, 202, 76, 30)) return ACT_HOME;
  }

  if (currentScreen == SCREEN_SETTINGS) {
    if (insideRect(tx, ty, 152, 42, 136, 42)) return ACT_THEME_TOGGLE;
    if (insideRect(tx, ty, 18, 137, 74, 46)) return ACT_BACKLIGHT_MINUS;
    if (insideRect(tx, ty, 224, 137, 74, 46)) return ACT_BACKLIGHT_PLUS;
    if (insideRect(tx, ty, 8, 202, 76, 30)) return ACT_HOME;
  }

  return ACT_NONE;
}

void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    int action = hitTest(t.x, t.y);
    doAction(action, false);
    if (actionCanHold(action)) {
      holdActive = true;
      holdAction = action;
      holdStartedAt = millis();
      lastHoldRepeat = millis();
    } else {
      holdActive = false;
      holdAction = ACT_NONE;
    }
  }

  if (t.isPressed() && holdActive) {
    unsigned long now = millis();
    if (now - holdStartedAt >= HOLD_DELAY_MS && now - lastHoldRepeat >= HOLD_REPEAT_MS) {
      bool fast = (now - holdStartedAt >= HOLD_FAST_MS);
      doAction(holdAction, fast);
      lastHoldRepeat = now;
    }
  }

  if (t.wasReleased()) {
    holdActive = false;
    holdAction = ACT_NONE;
  }
}

// -------------------- Graph Simulation --------------------
void updateSimulatedValues() {
  unsigned long now = millis();
  if (now - lastGraphTick < 75) return;
  unsigned long elapsed = now - lastGraphTick;
  lastGraphTick = now;

  // VAC ramps toward target. This is the simulated real-time line.
  float diff = (float)vacValue - vacActual;
  vacActual += diff * 0.10;
  if (fabs(diff) < 0.25) vacActual = (float)vacValue;

  pulsePhaseMs += elapsed;
  int pulseHeight = 0;
  if (ppmValue > 0) {
    unsigned long period = 60000UL / (unsigned long)ppmValue;
    if (period < 180) period = 180;
    unsigned long pos = pulsePhaseMs % period;
    float phase = (float)pos / (float)period;
    if (phase < 0.50) {
      // rounded hump shape; PPM changes frequency/spacing, not height.
      pulseHeight = (int)(36.0 * sin(phase * 2.0 * PI));
    } else {
      pulseHeight = 0;
    }
  } else {
    pulseHeight = 0;
  }

  histIndex = (histIndex + 1) % HIST_LEN;
  vacHist[histIndex] = clampInt((int)vacActual, 0, 100);
  ppmHist[histIndex] = clampInt(pulseHeight, 0, 38);

  if (currentScreen == SCREEN_VAC) {
    drawVacGraph();
  } else if (currentScreen == SCREEN_PPM) {
    drawPpmGraph();
  } else if (currentScreen == SCREEN_LIVE) {
    updateLiveBarsOnly();
  }
}

// -------------------- Arduino Entry Points --------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  W = M5.Display.width();
  H = M5.Display.height();
  initPalette();
  applyBacklight();
  seedGraphs();

  bootAnimation();
  drawHome();

  Serial.println("8_Pulse_CoreS3_Demo loaded. Simulation only. No outputs fired.");
}

void loop() {
  M5.update();
  handleTouch();
  updateSimulatedValues();
  delay(6);
}
