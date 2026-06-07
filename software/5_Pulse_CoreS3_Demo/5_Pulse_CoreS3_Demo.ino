/*
  5_Pulse_CoreS3_Demo
  Pulse Controller UI Demo for M5Stack CoreS3 / CoreS3 SE

  Purpose:
  - Screen sizing / UI demo only
  - No real hardware outputs are fired
  - Arduino IDE + M5Unified

  This revision:
  - Darker color scheme
  - Old boot animation style restored
  - VAC replaces Suck wording
  - VAC and PPM controls allow 0 = OFF
  - Two-step E-STOP flow:
      1) Small E-STOP button opens full-screen confirmation
      2) Full red screen press performs the simulated emergency release
  - Settings screen added:
      Light/Dark mode toggle and backlight slider
*/

#include <M5Unified.h>
#include <math.h>

#define SIMULATION_ONLY true

// ---------- Screen ----------
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// ---------- Colors ----------
uint16_t C_BG;
uint16_t C_PANEL;
uint16_t C_PANEL2;
uint16_t C_TEXT;
uint16_t C_MUTED;
uint16_t C_PRIMARY;
uint16_t C_PRIMARY_D;
uint16_t C_ACCENT;
uint16_t C_ACCENT_D;
uint16_t C_GREEN;
uint16_t C_RED;
uint16_t C_RED_D;
uint16_t C_LINE;
uint16_t C_GRID;
uint16_t C_DARK;
uint16_t C_WHITE;
uint16_t C_BLACK;
uint16_t C_ORANGE;
uint16_t C_WARN;

static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return M5.Display.color565(r, g, b);
}

bool darkTheme = true;
int backlightPercent = 72;  // 0 - 100 user setting

void initPalette() {
  if (darkTheme) {
    C_BG        = rgb(7, 12, 18);       // dark blue-black
    C_PANEL     = rgb(17, 27, 39);      // dark panel
    C_PANEL2    = rgb(27, 42, 58);      // raised panel
    C_TEXT      = rgb(231, 244, 245);   // soft white
    C_MUTED     = rgb(145, 165, 170);   // muted blue-gray
    C_PRIMARY   = rgb(0, 163, 170);     // teal
    C_PRIMARY_D = rgb(0, 96, 112);      // dark teal
    C_ACCENT    = rgb(255, 174, 66);    // amber
    C_ACCENT_D  = rgb(204, 98, 27);     // burnt orange
    C_GREEN     = rgb(48, 220, 129);    // green
    C_RED       = rgb(235, 40, 54);     // red
    C_RED_D     = rgb(125, 9, 20);      // dark red
    C_LINE      = rgb(95, 225, 235);    // graph cyan
    C_GRID      = rgb(42, 63, 78);      // grid
    C_DARK      = rgb(2, 4, 8);         // outline
    C_WHITE     = rgb(255, 255, 255);
    C_BLACK     = rgb(0, 0, 0);
    C_ORANGE    = rgb(255, 139, 37);
    C_WARN      = rgb(255, 211, 67);
  } else {
    C_BG        = rgb(226, 236, 239);   // light blue-gray
    C_PANEL     = rgb(245, 250, 252);   // light panel
    C_PANEL2    = rgb(210, 226, 232);   // raised panel
    C_TEXT      = rgb(18, 32, 40);      // dark text
    C_MUTED     = rgb(78, 99, 109);     // muted text
    C_PRIMARY   = rgb(0, 134, 145);     // teal
    C_PRIMARY_D = rgb(0, 91, 104);      // dark teal
    C_ACCENT    = rgb(228, 126, 32);    // orange
    C_ACCENT_D  = rgb(171, 80, 15);     // dark orange
    C_GREEN     = rgb(32, 180, 104);    // green
    C_RED       = rgb(218, 35, 50);     // red
    C_RED_D     = rgb(142, 16, 27);     // dark red
    C_LINE      = rgb(0, 110, 130);     // graph teal
    C_GRID      = rgb(178, 199, 207);   // grid
    C_DARK      = rgb(64, 82, 90);      // outline
    C_WHITE     = rgb(255, 255, 255);
    C_BLACK     = rgb(0, 0, 0);
    C_ORANGE    = rgb(255, 139, 37);
    C_WARN      = rgb(255, 211, 67);
  }
}

int brightnessToHardware() {
  return map(backlightPercent, 0, 100, 30, 255);
}

void applyBacklight() {
  M5.Display.setBrightness(brightnessToHardware());
}
// ---------- Screen IDs ----------
static const int SCREEN_HOME = 0;
static const int SCREEN_PPM = 1;
static const int SCREEN_VAC = 2;
static const int SCREEN_STATUS = 3;
static const int SCREEN_DIAG = 4;
static const int SCREEN_SETTINGS = 5;
static const int SCREEN_ESTOP_CONFIRM = 6;
static const int SCREEN_ESTOP_LATCHED = 7;

int currentScreen = SCREEN_HOME;
int previousOperatingScreen = SCREEN_HOME;
bool running = false;
bool estopLatched = false;
int ppmValue = 48;  // 0 - 120 pumps per minute; 0 = OFF
int vacValue = 55;  // 0 - 100 percent; 0 = OFF

unsigned long lastGraph = 0;
int graphScrollX = 0;

// ---------- Hardware safety placeholder ----------
void forceAllOutputsSafe() {
  // UI DEMO ONLY. No actual pins are controlled in this sketch.
  // Future real hardware version should do this immediately:
  // - Pump MOSFET/PWM OFF
  // - Pulse solenoid OFF / vent path safe
  // - Primary suction valve released
  // - Pulse strength valve released
  // - Release valve opened to atmosphere as designed
  // - Any positive/negative pressure released immediately
  running = false;
  Serial.println("SIM E-STOP: all positive and negative pressure released; outputs safe.");
}

// ---------- Helpers ----------
bool insideRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void clearScreen() {
  M5.Display.fillScreen(C_BG);
}

void buttonRect(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t textColor, int textSize) {
  M5.Display.fillRoundRect(x, y, w, h, 12, fill);
  M5.Display.drawRoundRect(x, y, w, h, 12, rgb(73, 94, 108));
  M5.Display.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, C_DARK);

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(textColor);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
  M5.Display.setTextDatum(top_left);
}

void smallEstopButton() {
  buttonRect(232, 7, 80, 27, "E-STOP", C_RED, C_WHITE, 1);
}

void header(const char* title, bool showBack) {
  M5.Display.fillRoundRect(8, 6, 304, 32, 8, C_PANEL);
  M5.Display.drawRoundRect(8, 6, 304, 32, 8, C_GRID);

  if (showBack) {
    M5.Display.fillRoundRect(14, 10, 38, 24, 7, C_PANEL2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_TEXT);
    M5.Display.setCursor(25, 13);
    M5.Display.print("<");
  }

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(showBack ? 58 : 18, 14);
  M5.Display.print(title);

  smallEstopButton();
}

void valuePill(int x, int y, int w, const char* label, const char* value, uint16_t accent) {
  M5.Display.fillRoundRect(x, y, w, 42, 11, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, 42, 11, C_GRID);
  M5.Display.fillRoundRect(x + 5, y + 5, 9, 32, 4, accent);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(x + 21, y + 7);
  M5.Display.print(label);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(x + 21, y + 22);
  M5.Display.print(value);
}

void drawGraphFrame(int x, int y, int w, int h, const char* label) {
  M5.Display.fillRoundRect(x, y, w, h, 12, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 12, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(x + 10, y + 7);
  M5.Display.print(label);
}

void drawGraphGrid(int gx, int gy, int gw, int gh) {
  for (int xx = gx; xx < gx + gw; xx += 30) M5.Display.drawFastVLine(xx, gy, gh, C_GRID);
  for (int yy = gy; yy < gy + gh; yy += 20) M5.Display.drawFastHLine(gx, yy, gw, C_GRID);
}

void drawPPMWave(int x, int y, int w, int h) {
  int gx = x + 9;
  int gy = y + 24;
  int gw = w - 18;
  int gh = h - 32;
  M5.Display.fillRect(gx, gy, gw, gh, rgb(10, 18, 27));
  drawGraphGrid(gx, gy, gw, gh);

  int mid = gy + gh / 2;
  if (ppmValue <= 0) {
    M5.Display.drawFastHLine(gx, mid, gw, C_MUTED);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_MUTED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("OFF", gx + gw / 2, mid);
    M5.Display.setTextDatum(top_left);
    return;
  }

  int amp = max(10, gh / 2 - 5);
  float freq = 0.075f + (float)ppmValue * 0.0036f;
  int lastX = gx;
  int lastY = mid;
  for (int i = 0; i < gw; i++) {
    int local = (i + graphScrollX) % max(12, 180 - ppmValue);
    float s;
    if (local < 18) s = 1.0f;
    else if (local < 34) s = -0.65f;
    else s = -0.12f * sinf((i + graphScrollX) * freq);
    int yy = mid - (int)(s * amp * 0.72f);
    int xx = gx + i;
    if (i > 0) M5.Display.drawLine(lastX, lastY, xx, yy, C_LINE);
    lastX = xx;
    lastY = yy;
  }
}

void drawVACWave(int x, int y, int w, int h) {
  int gx = x + 9;
  int gy = y + 24;
  int gw = w - 18;
  int gh = h - 32;
  M5.Display.fillRect(gx, gy, gw, gh, rgb(10, 18, 27));
  drawGraphGrid(gx, gy, gw, gh);

  // Visual percent scale: 0% is flat/off at bottom, 100% fills nearly the full graph.
  int bottom = gy + gh - 4;
  int topLimit = gy + 4;
  int levelY = map(vacValue, 0, 100, bottom, topLimit);

  if (vacValue <= 0) {
    M5.Display.drawFastHLine(gx, bottom, gw, C_MUTED);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_MUTED);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("VAC OFF", gx + gw / 2, gy + gh / 2);
    M5.Display.setTextDatum(top_left);
    return;
  }

  // Dark filled area below current VAC level.
  M5.Display.fillRect(gx, levelY, gw, bottom - levelY + 1, rgb(0, 58, 70));

  int rippleAmp = map(vacValue, 0, 100, 1, 12);
  float rateFactor = max(0, ppmValue) * 0.0028f + 0.055f;
  int lastX = gx;
  int lastY = levelY;
  for (int i = 0; i < gw; i++) {
    int xx = gx + i;
    int yy = levelY + (int)(sinf((i + graphScrollX) * rateFactor) * rippleAmp);
    if (yy < topLimit) yy = topLimit;
    if (yy > bottom) yy = bottom;
    if (i > 0) M5.Display.drawLine(lastX, lastY, xx, yy, C_ACCENT);
    lastX = xx;
    lastY = yy;
  }

  // Right-side 0-100 marks.
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(gx + gw - 28, gy + 2);
  M5.Display.print("100");
  M5.Display.setCursor(gx + gw - 18, bottom - 8);
  M5.Display.print("0");
}

// ---------- Old boot animation style ----------
void drawMilkBottle(int cx, int cy, int flameLength) {
  for (int i = 0; i < flameLength; i += 8) {
    int fx = cx - 48 - i;
    int fy = cy + ((i / 8) % 2 == 0 ? -9 : 8);
    uint16_t col = (i % 24 == 0) ? C_ORANGE : C_WARN;
    M5.Display.fillTriangle(fx, fy, fx + 32, fy - 18, fx + 26, fy + 18, col);
    M5.Display.fillTriangle(fx + 10, fy, fx + 28, fy - 11, fx + 25, fy + 11, rgb(255, 80, 25));
  }

  M5.Display.drawFastHLine(cx - 120, cy - 34, 45, C_PRIMARY_D);
  M5.Display.drawFastHLine(cx - 138, cy, 55, C_PRIMARY_D);
  M5.Display.drawFastHLine(cx - 112, cy + 36, 38, C_PRIMARY_D);

  M5.Display.fillRoundRect(cx - 24, cy - 38, 56, 82, 14, rgb(232, 246, 238));
  M5.Display.drawRoundRect(cx - 24, cy - 38, 56, 82, 14, C_WHITE);
  M5.Display.fillRoundRect(cx - 12, cy - 68, 32, 34, 8, rgb(245, 255, 250));
  M5.Display.drawRoundRect(cx - 12, cy - 68, 32, 34, 8, C_WHITE);
  M5.Display.fillRoundRect(cx - 16, cy - 82, 40, 16, 6, C_GREEN);
  M5.Display.drawRoundRect(cx - 16, cy - 82, 40, 16, 6, C_WHITE);

  M5.Display.fillRoundRect(cx - 18, cy - 10, 46, 28, 7, C_PRIMARY_D);
  M5.Display.drawRoundRect(cx - 18, cy - 10, 46, 28, 7, C_GREEN);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_WHITE);
  M5.Display.drawString("MILK", cx + 5, cy + 4);
  M5.Display.setTextDatum(top_left);

  M5.Display.drawFastVLine(cx - 12, cy - 24, 34, C_WHITE);
  M5.Display.fillCircle(cx + 42, cy - 26, 3, C_WHITE);
  M5.Display.fillCircle(cx + 50, cy - 8, 2, C_WHITE);
  M5.Display.fillCircle(cx + 40, cy + 20, 2, C_WHITE);
}

void bootAnimation() {
  M5.Display.fillScreen(C_BG);

  for (int frame = 0; frame < 30; frame++) {
    M5.Display.fillScreen(C_BG);
    int x = -45 + frame * 13;
    int y = 154 - frame * 2 + ((frame % 4) - 2) * 2;
    drawMilkBottle(x, y, 100);

    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(C_GREEN);
    M5.Display.drawString("PULSE", 160, 183);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_MUTED);
    M5.Display.drawString("screen sizing demo", 160, 211);
    M5.Display.setTextDatum(top_left);
    delay(85);
  }

  M5.Display.fillScreen(C_BG);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_GREEN);
  M5.Display.drawString("PULSE", 160, 90);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.drawString("SYSTEM READY", 160, 130);
  M5.Display.setTextDatum(top_left);
  delay(650);
}

// ---------- Screens ----------
void drawHome() {
  currentScreen = SCREEN_HOME;
  clearScreen();
  header("PULSE CONTROL", false);

  char vacBuf[24];
  char ppmBuf[24];
  if (vacValue <= 0) snprintf(vacBuf, sizeof(vacBuf), "OFF");
  else snprintf(vacBuf, sizeof(vacBuf), "%d%%", vacValue);
  if (ppmValue <= 0) snprintf(ppmBuf, sizeof(ppmBuf), "OFF");
  else snprintf(ppmBuf, sizeof(ppmBuf), "%d PPM", ppmValue);

  // Swapped from prior layout: VAC first, PPM second.
  valuePill(12, 48, 142, "VAC", vacBuf, C_ACCENT);
  valuePill(166, 48, 142, "Pumps per minute", ppmBuf, C_PRIMARY);

  buttonRect(12, 98, 142, 46, running ? "STOP" : "START", running ? C_RED_D : C_GREEN, running ? C_WHITE : C_BLACK, 2);
  buttonRect(166, 98, 142, 46, "STATUS", C_PANEL2, C_TEXT, 2);

  buttonRect(12, 150, 142, 40, "VAC", C_ACCENT_D, C_WHITE, 2);
  buttonRect(166, 150, 142, 40, "PPM", C_PRIMARY_D, C_WHITE, 2);

  buttonRect(12, 196, 142, 36, "TEST", C_PANEL2, C_TEXT, 2);
  buttonRect(166, 196, 142, 36, "SETTINGS", C_PANEL2, C_TEXT, 1);
}

void drawPPMScreen() {
  currentScreen = SCREEN_PPM;
  clearScreen();
  header("PUMPS PER MINUTE", true);

  M5.Display.setTextSize(4);
  M5.Display.setTextColor(C_PRIMARY);
  M5.Display.setCursor(26, 49);
  if (ppmValue <= 0) M5.Display.print("OFF");
  else M5.Display.print(ppmValue);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(138, 62);
  M5.Display.print("PPM");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(24, 88);
  M5.Display.print("Adjust squeezes / pumps per minute");

  drawGraphFrame(16, 105, 288, 76, "PPM rate visualizer");
  drawPPMWave(16, 105, 288, 76);

  buttonRect(18, 190, 86, 42, "-", C_PANEL2, C_TEXT, 4);
  buttonRect(116, 190, 86, 42, "+", C_PRIMARY_D, C_WHITE, 4);
  buttonRect(214, 190, 90, 42, "HOME", C_PANEL2, C_TEXT, 1);
}

void drawVACScreen() {
  currentScreen = SCREEN_VAC;
  clearScreen();
  header("VAC CONTROL", true);

  M5.Display.setTextSize(4);
  M5.Display.setTextColor(C_ACCENT);
  M5.Display.setCursor(32, 49);
  if (vacValue <= 0) M5.Display.print("OFF");
  else M5.Display.print(vacValue);

  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(138, 62);
  M5.Display.print("VAC");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(24, 88);
  M5.Display.print("Adjust VAC strength from 0 to 100%");

  drawGraphFrame(16, 105, 288, 76, "VAC percent visualizer");
  drawVACWave(16, 105, 288, 76);

  buttonRect(18, 190, 86, 42, "-", C_PANEL2, C_TEXT, 4);
  buttonRect(116, 190, 86, 42, "+", C_ACCENT_D, C_WHITE, 4);
  buttonRect(214, 190, 90, 42, "HOME", C_PANEL2, C_TEXT, 1);
}

void drawStatusScreen() {
  currentScreen = SCREEN_STATUS;
  clearScreen();
  header("LIVE STATUS", true);

  char ppmBuf[24];
  char vacBuf[24];
  snprintf(ppmBuf, sizeof(ppmBuf), ppmValue <= 0 ? "OFF" : "%d PPM", ppmValue);
  snprintf(vacBuf, sizeof(vacBuf), vacValue <= 0 ? "OFF" : "%d%%", vacValue);

  valuePill(12, 48, 142, "VAC", vacBuf, C_ACCENT);
  valuePill(166, 48, 142, "Pumps per minute", ppmBuf, C_PRIMARY);

  drawGraphFrame(16, 100, 288, 76, running ? "Operating simulation" : "Idle simulation");
  drawPPMWave(16, 100, 288, 76);

  buttonRect(18, 190, 138, 42, running ? "STOP" : "START", running ? C_RED_D : C_GREEN, running ? C_WHITE : C_BLACK, 2);
  buttonRect(166, 190, 138, 42, "HOME", C_PANEL2, C_TEXT, 2);
}

void drawDiagScreen() {
  currentScreen = SCREEN_DIAG;
  clearScreen();
  header("MANUAL TEST", true);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(18, 48);
  M5.Display.print("Demo buttons only - no outputs fired");

  buttonRect(16, 68, 138, 46, "PUMP", C_PANEL2, C_TEXT, 2);
  buttonRect(166, 68, 138, 46, "PULSE", C_PANEL2, C_TEXT, 2);
  buttonRect(16, 124, 138, 46, "RELEASE", C_PANEL2, C_TEXT, 2);
  buttonRect(166, 124, 138, 46, "VALVE", C_PANEL2, C_TEXT, 2);
  buttonRect(18, 190, 286, 42, "HOME", C_PRIMARY_D, C_WHITE, 2);
}

void drawSettingsScreen() {
  currentScreen = SCREEN_SETTINGS;
  clearScreen();
  header("SETTINGS", true);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(20, 48);
  M5.Display.print("User screen preferences - demo only");

  M5.Display.fillRoundRect(18, 66, 284, 42, 11, C_PANEL);
  M5.Display.drawRoundRect(18, 66, 284, 42, 11, C_GRID);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(30, 78);
  M5.Display.print("MODE");
  buttonRect(178, 71, 110, 32, darkTheme ? "DARK" : "LIGHT", darkTheme ? C_PRIMARY_D : C_ACCENT, darkTheme ? C_WHITE : C_BLACK, 2);

  M5.Display.fillRoundRect(18, 118, 284, 54, 12, C_PANEL);
  M5.Display.drawRoundRect(18, 118, 284, 54, 12, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(30, 126);
  M5.Display.print("BACKLIGHT");

  char lightBuf[16];
  snprintf(lightBuf, sizeof(lightBuf), "%d%%", backlightPercent);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(236, 126);
  M5.Display.print(lightBuf);

  int sx = 30;
  int sy = 150;
  int sw = 246;
  int filled = map(backlightPercent, 0, 100, 0, sw);
  M5.Display.fillRoundRect(sx, sy, sw, 12, 6, darkTheme ? rgb(35, 52, 66) : rgb(193, 211, 218));
  M5.Display.fillRoundRect(sx, sy, filled, 12, 6, C_PRIMARY);
  M5.Display.fillCircle(sx + filled, sy + 6, 9, C_ACCENT);

  buttonRect(18, 188, 76, 42, "-", C_PANEL2, C_TEXT, 4);
  buttonRect(102, 188, 76, 42, "+", C_PRIMARY_D, C_WHITE, 4);
  buttonRect(188, 188, 114, 42, "HOME", C_PANEL2, C_TEXT, 2);
}

void drawEstopConfirmScreen() {
  currentScreen = SCREEN_ESTOP_CONFIRM;
  M5.Display.fillScreen(C_RED);
  M5.Display.fillRect(0, 216, SCREEN_W, 24, C_GREEN);

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(C_WHITE);
  M5.Display.setTextSize(4);
  M5.Display.drawString("E-STOP", 160, 58);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PRESS RED SCREEN", 160, 114);
  M5.Display.drawString("TO RELEASE ALL", 160, 142);
  M5.Display.drawString("PRESSURE", 160, 168);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_BLACK);
  M5.Display.drawString("CANCEL / RETURN", 160, 228);
  M5.Display.setTextDatum(top_left);
}

void drawEstopLatchedScreen() {
  currentScreen = SCREEN_ESTOP_LATCHED;
  M5.Display.fillScreen(C_RED_D);
  M5.Display.fillRect(0, 216, SCREEN_W, 24, C_GREEN);

  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(C_WHITE);
  M5.Display.setTextSize(4);
  M5.Display.drawString("STOPPED", 160, 52);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PRESSURE RELEASED", 160, 108);
  M5.Display.setTextSize(1);
  M5.Display.drawString("Positive and negative pressure forced safe", 160, 145);
  M5.Display.drawString("Simulation only - no real outputs fired", 160, 166);

  M5.Display.setTextColor(C_BLACK);
  M5.Display.drawString("RETURN TO CONTROLS", 160, 228);
  M5.Display.setTextDatum(top_left);
}

void redrawCurrentScreen() {
  if (currentScreen == SCREEN_HOME) drawHome();
  else if (currentScreen == SCREEN_PPM) drawPPMScreen();
  else if (currentScreen == SCREEN_VAC) drawVACScreen();
  else if (currentScreen == SCREEN_STATUS) drawStatusScreen();
  else if (currentScreen == SCREEN_DIAG) drawDiagScreen();
  else if (currentScreen == SCREEN_SETTINGS) drawSettingsScreen();
  else if (currentScreen == SCREEN_ESTOP_CONFIRM) drawEstopConfirmScreen();
  else if (currentScreen == SCREEN_ESTOP_LATCHED) drawEstopLatchedScreen();
}

void openEstopConfirm() {
  if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_LATCHED) {
    previousOperatingScreen = currentScreen;
  }
  drawEstopConfirmScreen();
}

void returnToOperatingScreen() {
  estopLatched = false;
  if (previousOperatingScreen == SCREEN_PPM) drawPPMScreen();
  else if (previousOperatingScreen == SCREEN_VAC) drawVACScreen();
  else if (previousOperatingScreen == SCREEN_STATUS) drawStatusScreen();
  else if (previousOperatingScreen == SCREEN_DIAG) drawDiagScreen();
  else if (previousOperatingScreen == SCREEN_SETTINGS) drawSettingsScreen();
  else drawHome();
}

void handleTouch(int x, int y) {
  // E-STOP confirm screen: green bottom bar cancels; red area performs actual E-stop.
  if (currentScreen == SCREEN_ESTOP_CONFIRM) {
    if (y >= 216) {
      returnToOperatingScreen();
    } else {
      estopLatched = true;
      forceAllOutputsSafe();
      drawEstopLatchedScreen();
    }
    return;
  }

  // Latched E-stop screen: only the green bottom bar returns to controls.
  if (currentScreen == SCREEN_ESTOP_LATCHED) {
    if (y >= 216) {
      returnToOperatingScreen();
    }
    return;
  }

  // Small E-STOP button on all operating screens only opens confirmation.
  if (insideRect(x, y, 232, 7, 80, 27)) {
    openEstopConfirm();
    return;
  }

  // Back button common area.
  if (currentScreen != SCREEN_HOME && insideRect(x, y, 8, 6, 52, 36)) {
    drawHome();
    return;
  }

  if (currentScreen == SCREEN_HOME) {
    if (insideRect(x, y, 12, 98, 142, 46)) {
      running = !running;
      drawHome();
    } else if (insideRect(x, y, 166, 98, 142, 46)) {
      drawStatusScreen();
    } else if (insideRect(x, y, 12, 150, 142, 40)) {
      drawVACScreen();
    } else if (insideRect(x, y, 166, 150, 142, 40)) {
      drawPPMScreen();
    } else if (insideRect(x, y, 12, 196, 142, 36)) {
      drawDiagScreen();
    } else if (insideRect(x, y, 166, 196, 142, 36)) {
      drawSettingsScreen();
    }
  }
  else if (currentScreen == SCREEN_PPM) {
    if (insideRect(x, y, 18, 190, 86, 42)) {
      ppmValue -= 2;
      if (ppmValue < 0) ppmValue = 0;
      drawPPMScreen();
    } else if (insideRect(x, y, 116, 190, 86, 42)) {
      if (ppmValue == 0) ppmValue = 2;
      else ppmValue += 2;
      if (ppmValue > 120) ppmValue = 120;
      drawPPMScreen();
    } else if (insideRect(x, y, 214, 190, 90, 42)) {
      drawHome();
    }
  }
  else if (currentScreen == SCREEN_VAC) {
    if (insideRect(x, y, 18, 190, 86, 42)) {
      vacValue -= 5;
      if (vacValue < 0) vacValue = 0;
      drawVACScreen();
    } else if (insideRect(x, y, 116, 190, 86, 42)) {
      vacValue += 5;
      if (vacValue > 100) vacValue = 100;
      drawVACScreen();
    } else if (insideRect(x, y, 214, 190, 90, 42)) {
      drawHome();
    }
  }
  else if (currentScreen == SCREEN_SETTINGS) {
    if (insideRect(x, y, 178, 71, 110, 32) || insideRect(x, y, 18, 66, 284, 42)) {
      darkTheme = !darkTheme;
      initPalette();
      drawSettingsScreen();
    } else if (insideRect(x, y, 18, 188, 76, 42)) {
      backlightPercent -= 5;
      if (backlightPercent < 0) backlightPercent = 0;
      applyBacklight();
      drawSettingsScreen();
    } else if (insideRect(x, y, 102, 188, 76, 42)) {
      backlightPercent += 5;
      if (backlightPercent > 100) backlightPercent = 100;
      applyBacklight();
      drawSettingsScreen();
    } else if (insideRect(x, y, 188, 188, 114, 42)) {
      drawHome();
    }
  }
  else if (currentScreen == SCREEN_STATUS) {
    if (insideRect(x, y, 18, 190, 138, 42)) {
      running = !running;
      drawStatusScreen();
    } else if (insideRect(x, y, 166, 190, 138, 42)) {
      drawHome();
    }
  }
  else if (currentScreen == SCREEN_DIAG) {
    if (insideRect(x, y, 18, 190, 286, 42)) {
      drawHome();
    }
  }
}

void updateLiveGraphOnly() {
  if (currentScreen == SCREEN_PPM) {
    drawPPMWave(16, 105, 288, 76);
  } else if (currentScreen == SCREEN_VAC) {
    drawVACWave(16, 105, 288, 76);
  } else if (currentScreen == SCREEN_STATUS) {
    drawPPMWave(16, 100, 288, 76);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(1);
  M5.Display.setTextDatum(top_left);
  initPalette();
  applyBacklight();

  bootAnimation();
  drawHome();

  Serial.println("5_Pulse_CoreS3_Demo loaded. UI simulation only.");
}

void loop() {
  M5.update();

  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    handleTouch(touch.x, touch.y);
  }

  unsigned long now = millis();
  if (now - lastGraph > 80) {
    lastGraph = now;
    int scrollStep = 1;
    if (ppmValue > 0) scrollStep = max(1, ppmValue / 16);
    graphScrollX += scrollStep;
    if (graphScrollX > 10000) graphScrollX = 0;
    updateLiveGraphOnly();
  }

  delay(5);
}
