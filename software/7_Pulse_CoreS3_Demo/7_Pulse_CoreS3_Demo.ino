#include <M5Unified.h>

// =============================================================
// 7_Pulse_CoreS3_Demo
// Screen / touch demo only. No real hardware outputs are fired.
// Globals are intentionally declared BEFORE any functions to avoid
// Arduino IDE auto-prototype scope errors.
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
int backlightPercent = 80;
int ppmValue = 42;       // 0 = OFF
int vacValue = 55;       // 0 = OFF, 100 = MAX

// Touch hold/repeat state
bool holdActive = false;
int holdAction = 0;
unsigned long holdStartedAt = 0;
unsigned long lastHoldRepeat = 0;
const unsigned long HOLD_DELAY_MS = 360;
const unsigned long HOLD_REPEAT_MS = 95;

// Redraw / animation timing
unsigned long lastGraphTick = 0;
unsigned long lastClockTick = 0;
int graphOffset = 0;

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
    C_BG = M5.Display.color565(15, 18, 28);
    C_PANEL = M5.Display.color565(28, 34, 50);
    C_PANEL2 = M5.Display.color565(38, 45, 65);
    C_TEXT = WHITE;
    C_MUTED = M5.Display.color565(165, 175, 195);
    C_ACCENT = M5.Display.color565(70, 190, 255);
    C_ACCENT2 = M5.Display.color565(170, 120, 255);
    C_WARN = M5.Display.color565(255, 185, 65);
    C_DANGER = M5.Display.color565(230, 45, 55);
    C_OK = M5.Display.color565(60, 210, 125);
    C_LINE = M5.Display.color565(80, 95, 125);
  } else {
    C_BG = M5.Display.color565(232, 238, 246);
    C_PANEL = M5.Display.color565(255, 255, 255);
    C_PANEL2 = M5.Display.color565(216, 226, 240);
    C_TEXT = M5.Display.color565(25, 31, 42);
    C_MUTED = M5.Display.color565(85, 96, 115);
    C_ACCENT = M5.Display.color565(0, 112, 190);
    C_ACCENT2 = M5.Display.color565(115, 60, 190);
    C_WARN = M5.Display.color565(210, 130, 15);
    C_DANGER = M5.Display.color565(210, 35, 45);
    C_OK = M5.Display.color565(20, 150, 80);
    C_LINE = M5.Display.color565(145, 160, 180);
  }
}

int brightnessToHardware() {
  // Keep a small minimum so the display does not appear dead during demos.
  return map(backlightPercent, 0, 100, 25, 255);
}

void applyBacklight() {
  M5.Display.setBrightness(brightnessToHardware());
}

void forceAllOutputsSafeSimulationOnly() {
  // FUTURE REAL HARDWARE SAFETY LOCATION:
  // - pump OFF
  // - close/disable drive outputs
  // - release positive pressure
  // - release negative pressure / VAC
  // - force MOSFET/relay/solenoid pins to safe state
  appRunning = false;
}

// -------------------- Drawing Helpers --------------------
void setText(int size, uint16_t color) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color);
}

void drawHeader(const char* title) {
  M5.Display.fillRect(0, 0, W, 34, C_PANEL2);
  setText(2, C_TEXT);
  M5.Display.setCursor(10, 8);
  M5.Display.print(title);

  // Small top-right E-STOP entry button. It opens confirmation only.
  M5.Display.fillRoundRect(W - 72, 4, 66, 26, 6, C_DANGER);
  setText(1, WHITE);
  M5.Display.setCursor(W - 62, 13);
  M5.Display.print("E-STOP");
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
  if (tx < x + 3) tx = x + 3;
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label);
}

void drawBackButton() {
  drawButton(8, 202, 76, 30, "HOME", C_PANEL2, C_TEXT, 1);
}

void drawValuePill(int x, int y, int w, int h, const char* label, int value, const char* suffix, uint16_t color) {
  M5.Display.fillRoundRect(x, y, w, h, 12, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 12, C_LINE);
  setText(1, C_MUTED);
  M5.Display.setCursor(x + 12, y + 9);
  M5.Display.print(label);
  setText(3, color);
  M5.Display.setCursor(x + 12, y + 28);
  M5.Display.print(value);
  if (suffix) M5.Display.print(suffix);
}

void drawTinyStatusBar() {
  M5.Display.fillRect(0, H - 12, W, 12, C_PANEL2);
  setText(1, C_MUTED);
  M5.Display.setCursor(8, H - 10);
  M5.Display.print("DEMO ONLY  |  NO OUTPUTS ACTIVE");
}

void drawWaveGraph(int x, int y, int w, int h, int amount, bool pulseShape, uint16_t color) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);

  // Grid
  for (int gx = x + 20; gx < x + w; gx += 30) M5.Display.drawLine(gx, y + 6, gx, y + h - 6, C_PANEL2);
  for (int gy = y + 18; gy < y + h; gy += 18) M5.Display.drawLine(x + 6, gy, x + w - 6, gy, C_PANEL2);

  if (amount <= 0) {
    setText(2, C_MUTED);
    M5.Display.setCursor(x + w / 2 - 24, y + h / 2 - 8);
    M5.Display.print("OFF");
    return;
  }

  int mid = y + h / 2;
  int amp = map(amount, 0, 100, 2, (h / 2) - 10);
  if (pulseShape) {
    // square-ish pulse train: PPM affects spacing and scroll feel
    int spacing = map(clampInt(ppmValue, 1, 100), 1, 100, 72, 14);
    int highW = max(5, spacing / 3);
    int start = x + 7 - (graphOffset % spacing);
    for (int px = start; px < x + w - 8; px += spacing) {
      int x1 = max(px, x + 7);
      int x2 = min(px + highW, x + w - 8);
      M5.Display.drawLine(x1, mid + amp, x1, mid - amp, color);
      M5.Display.drawLine(x1, mid - amp, x2, mid - amp, color);
      M5.Display.drawLine(x2, mid - amp, x2, mid + amp, color);
      M5.Display.drawLine(x2, mid + amp, min(px + spacing, x + w - 8), mid + amp, color);
    }
  } else {
    // VAC graph: always slides; amplitude follows VAC percentage
    int lastX = x + 7;
    int lastY = mid;
    for (int i = 0; i < w - 15; i++) {
      float phase = (float)(i + graphOffset) * 0.13;
      int yy = mid + (int)(sin(phase) * amp);
      int xx = x + 7 + i;
      if (i > 0) M5.Display.drawLine(lastX, lastY, xx, yy, color);
      lastX = xx;
      lastY = yy;
    }
  }
}

// -------------------- Screens --------------------
void drawBootBottle(int x, int y, int eyeDir) {
  // Flame trail
  M5.Display.fillTriangle(x - 48, y + 20, x - 17, y + 8, x - 20, y + 35, M5.Display.color565(255, 80, 25));
  M5.Display.fillTriangle(x - 38, y + 22, x - 12, y + 13, x - 14, y + 31, M5.Display.color565(255, 185, 30));

  // Bottle body
  M5.Display.fillRoundRect(x, y, 52, 76, 12, M5.Display.color565(244, 248, 255));
  M5.Display.drawRoundRect(x, y, 52, 76, 12, M5.Display.color565(70, 90, 125));
  M5.Display.fillRoundRect(x + 12, y - 22, 28, 25, 6, M5.Display.color565(235, 242, 255));
  M5.Display.drawRoundRect(x + 12, y - 22, 28, 25, 6, M5.Display.color565(70, 90, 125));
  M5.Display.fillRect(x + 9, y + 48, 34, 16, M5.Display.color565(215, 235, 255));

  // Face
  int off = eyeDir * 3;
  M5.Display.fillCircle(x + 17, y + 27, 8, WHITE);
  M5.Display.fillCircle(x + 36, y + 27, 8, WHITE);
  M5.Display.fillCircle(x + 17 + off, y + 27, 3, BLACK);
  M5.Display.fillCircle(x + 36 + off, y + 27, 3, BLACK);
  M5.Display.drawArc(x + 26, y + 43, 10, 6, 20, 160, BLACK);
}

void bootAnimation() {
  M5.Display.fillScreen(C_BG);
  setText(2, C_TEXT);
  M5.Display.setCursor(72, 22);
  M5.Display.print("PULSE DEMO");

  for (int x = -60; x <= 132; x += 8) {
    M5.Display.fillRect(0, 54, W, 142, C_BG);
    drawBootBottle(x, 96, 1);
    delay(45);
  }

  for (int i = 0; i < 3; i++) {
    M5.Display.fillRect(70, 54, 180, 142, C_BG);
    drawBootBottle(132, 96, 1);
    delay(290);
    M5.Display.fillRect(70, 54, 180, 142, C_BG);
    drawBootBottle(132, 96, -1);
    delay(290);
  }

  for (int x = 132; x <= 360; x += 9) {
    M5.Display.fillRect(0, 54, W, 142, C_BG);
    drawBootBottle(x, 96, 1);
    delay(38);
  }
}

void drawHome() {
  currentScreen = SCREEN_HOME;
  M5.Display.fillScreen(C_BG);
  drawHeader("Pulse Control");

  drawButton(16, 46, 136, 62, appRunning ? "STOP" : "START", appRunning ? C_WARN : C_OK, BLACK, 2);
  drawButton(168, 46, 136, 62, "LIVE", C_ACCENT, WHITE, 2);

  // Swapped order: VAC first, PPM second
  drawButton(16, 122, 136, 62, "VAC", C_ACCENT2, WHITE, 2);
  drawButton(168, 122, 136, 62, "PPM", C_ACCENT, WHITE, 2);

  drawButton(16, 194, 90, 32, "TEST", C_PANEL2, C_TEXT, 1);
  drawButton(116, 194, 90, 32, "SET", C_PANEL2, C_TEXT, 1);
  drawButton(216, 194, 88, 32, "SAFE", C_DANGER, WHITE, 1);

  drawTinyStatusBar();
}

void drawControlScreen(bool ppmScreen) {
  if (ppmScreen) currentScreen = SCREEN_PPM;
  else currentScreen = SCREEN_VAC;

  M5.Display.fillScreen(C_BG);
  drawHeader(ppmScreen ? "PPM Control" : "VAC Control");

  int val = ppmScreen ? ppmValue : vacValue;
  const char* suffix = ppmScreen ? "" : "%";
  drawValuePill(14, 46, 106, 70, ppmScreen ? "Pumps/min" : "VAC", val, suffix, ppmScreen ? C_ACCENT : C_ACCENT2);

  setText(1, C_MUTED);
  M5.Display.setCursor(132, 52);
  if (ppmScreen) {
    M5.Display.print("Adjust squeezes / pumps");
    M5.Display.setCursor(132, 66);
    M5.Display.print("per minute. 0 = OFF");
  } else {
    M5.Display.print("Adjust vacuum pull");
    M5.Display.setCursor(132, 66);
    M5.Display.print("strength. 0 = OFF");
  }

  // Smaller graph so buttons can be larger
  drawWaveGraph(128, 84, 178, 62, ppmScreen ? map(ppmValue, 0, 100, 0, 85) : vacValue, ppmScreen, ppmScreen ? C_ACCENT : C_ACCENT2);

  // Big touch controls
  drawButton(20, 128, 132, 64, "-", C_PANEL2, C_TEXT, 4);
  drawButton(168, 128, 132, 64, "+", ppmScreen ? C_ACCENT : C_ACCENT2, WHITE, 4);

  drawBackButton();
  setText(1, C_MUTED);
  M5.Display.setCursor(102, 211);
  M5.Display.print("Hold + / - to scroll");
}

void drawLive() {
  currentScreen = SCREEN_LIVE;
  M5.Display.fillScreen(C_BG);
  drawHeader("Live Status");
  drawValuePill(18, 48, 126, 70, "VAC", vacValue, "%", C_ACCENT2);
  drawValuePill(176, 48, 126, 70, "PPM", ppmValue, "", C_ACCENT);
  drawWaveGraph(18, 128, 284, 56, vacValue, false, C_ACCENT2);
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
  M5.Display.fillRoundRect(102, 144, 112, 26, 8, C_PANEL);
  M5.Display.drawRoundRect(102, 144, 112, 26, 8, C_LINE);
  int fillW = map(backlightPercent, 0, 100, 0, 108);
  M5.Display.fillRoundRect(104, 146, fillW, 22, 7, C_OK);
  setText(1, C_TEXT);
  M5.Display.setCursor(137, 153);
  M5.Display.print(backlightPercent);
  M5.Display.print("%");
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
    case SCREEN_PPM: drawControlScreen(true); break;
    case SCREEN_VAC: drawControlScreen(false); break;
    case SCREEN_LIVE: drawLive(); break;
    case SCREEN_MANUAL: drawManual(); break;
    case SCREEN_SETTINGS: drawSettings(); break;
    case SCREEN_ESTOP_CONFIRM: drawEstopConfirm(); break;
    case SCREEN_ESTOP_LATCHED: drawEstopLatched(); break;
  }
}

// -------------------- Input Handling --------------------
void doAction(int action) {
  if (action == ACT_NONE) return;

  switch (action) {
    case ACT_HOME:
      drawHome();
      break;
    case ACT_PPM:
      drawControlScreen(true);
      break;
    case ACT_VAC:
      drawControlScreen(false);
      break;
    case ACT_LIVE:
      drawLive();
      break;
    case ACT_MANUAL:
      drawManual();
      break;
    case ACT_SETTINGS:
      drawSettings();
      break;
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
      ppmValue = clampInt(ppmValue + 1, 0, 100);
      drawControlScreen(true);
      break;
    case ACT_PPM_MINUS:
      ppmValue = clampInt(ppmValue - 1, 0, 100);
      drawControlScreen(true);
      break;
    case ACT_VAC_PLUS:
      vacValue = clampInt(vacValue + 1, 0, 100);
      drawControlScreen(false);
      break;
    case ACT_VAC_MINUS:
      vacValue = clampInt(vacValue - 1, 0, 100);
      drawControlScreen(false);
      break;
    case ACT_THEME_TOGGLE:
      darkTheme = !darkTheme;
      initPalette();
      redrawCurrentScreen();
      break;
    case ACT_BACKLIGHT_PLUS:
      backlightPercent = clampInt(backlightPercent + 1, 0, 100);
      applyBacklight();
      drawSettings();
      break;
    case ACT_BACKLIGHT_MINUS:
      backlightPercent = clampInt(backlightPercent - 1, 0, 100);
      applyBacklight();
      drawSettings();
      break;
    case ACT_TEST1:
    case ACT_TEST2:
    case ACT_TEST3:
      // Demo-only flash feedback
      M5.Display.fillRect(0, 190, W, 10, C_OK);
      delay(80);
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
  // Full E-stop pages have special bottom cancel bar.
  if (currentScreen == SCREEN_ESTOP_CONFIRM || currentScreen == SCREEN_ESTOP_LATCHED) {
    if (insideRect(tx, ty, 0, H - 24, W, 24)) return ACT_ESTOP_CANCEL;
    if (currentScreen == SCREEN_ESTOP_CONFIRM) return ACT_ESTOP_FIRE;
    return ACT_NONE;
  }

  // Header E-stop button on every normal screen.
  if (insideRect(tx, ty, W - 72, 4, 66, 26)) return ACT_ESTOP_OPEN;

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
    if (insideRect(tx, ty, 20, 128, 132, 64)) return ACT_PPM_MINUS;
    if (insideRect(tx, ty, 168, 128, 132, 64)) return ACT_PPM_PLUS;
    if (insideRect(tx, ty, 8, 202, 76, 30)) return ACT_HOME;
  }

  if (currentScreen == SCREEN_VAC) {
    if (insideRect(tx, ty, 20, 128, 132, 64)) return ACT_VAC_MINUS;
    if (insideRect(tx, ty, 168, 128, 132, 64)) return ACT_VAC_PLUS;
    if (insideRect(tx, ty, 8, 202, 76, 30)) return ACT_HOME;
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
    doAction(action);
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
      doAction(holdAction);
      lastHoldRepeat = now;
    }
  }

  if (t.wasReleased()) {
    holdActive = false;
    holdAction = ACT_NONE;
  }
}

void updateGraphs() {
  unsigned long now = millis();
  if (now - lastGraphTick < 70) return;
  lastGraphTick = now;
  graphOffset += 2;

  if (currentScreen == SCREEN_PPM) {
    drawWaveGraph(128, 84, 178, 62, map(ppmValue, 0, 100, 0, 85), true, C_ACCENT);
  } else if (currentScreen == SCREEN_VAC) {
    drawWaveGraph(128, 84, 178, 62, vacValue, false, C_ACCENT2);
  } else if (currentScreen == SCREEN_LIVE) {
    drawWaveGraph(18, 128, 284, 56, vacValue, false, C_ACCENT2);
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

  bootAnimation();
  drawHome();

  Serial.println("7_Pulse_CoreS3_Demo loaded. Simulation only. No outputs fired.");
}

void loop() {
  M5.update();
  handleTouch();
  updateGraphs();
  delay(8);
}
