#include <M5Unified.h>

// =========================================================
// 9_Pulse_CoreS3_Demo
// Screen/UI sizing demo only. No hardware outputs are fired.
// =========================================================

// ----- Display size -----
const int SW = 320;
const int SH = 240;

// ----- Screen IDs. Use int, not custom enum, to avoid Arduino prototype issues. -----
const int SCREEN_BOOT = 0;
const int SCREEN_HOME = 1;
const int SCREEN_VAC = 2;
const int SCREEN_PPM = 3;
const int SCREEN_LIVE = 4;
const int SCREEN_SETTINGS = 5;
const int SCREEN_DIAG = 6;
const int SCREEN_ESTOP_CONFIRM = 7;
const int SCREEN_ESTOP_LATCHED = 8;

// ----- App state. Keep all globals before any function. -----
int currentScreen = SCREEN_BOOT;
int previousScreen = SCREEN_HOME;

bool darkTheme = true;
int backlightStep = 7;       // 1-10
int vacSet = 40;             // 0-100%
int ppmSet = 24;             // 0-100 demo scale / pumps per minute
float vacActual = 0.0;
bool running = false;
bool emergencyLatched = false;

// ----- Palette -----
uint16_t C_BG;
uint16_t C_PANEL;
uint16_t C_PANEL2;
uint16_t C_TEXT;
uint16_t C_MUTED;
uint16_t C_ACCENT;
uint16_t C_ACCENT2;
uint16_t C_WARN;
uint16_t C_RED;
uint16_t C_GREEN;
uint16_t C_GRAPH;
uint16_t C_GRID;

// ----- Timing -----
unsigned long lastGraphUpdate = 0;
unsigned long lastTouchTick = 0;
unsigned long lastValueRedraw = 0;
unsigned long activeHoldStart = 0;
unsigned long lastHoldRepeat = 0;
int activeHoldAction = 0;
int lastTouchX = -1;
int lastTouchY = -1;

// ----- Graph buffers -----
const int GRAPH_W = 210;
const int GRAPH_H = 92;
int vacHistory[GRAPH_W];
int ppmHistory[GRAPH_W];
int graphIndex = 0;
float ppmPhase = 0.0;

// ----- Button layout helpers -----
const int TOP_H = 30;
const int ESTOP_X = 258;
const int ESTOP_Y = 4;
const int ESTOP_W = 58;
const int ESTOP_H = 24;

// Button action IDs
const int ACT_NONE = 0;
const int ACT_VAC_MINUS = 1;
const int ACT_VAC_PLUS = 2;
const int ACT_PPM_MINUS = 3;
const int ACT_PPM_PLUS = 4;
const int ACT_BACKLIGHT_MINUS = 5;
const int ACT_BACKLIGHT_PLUS = 6;

// Forward declarations kept simple: no custom types in signatures.
void initPalette();
void applyBacklight();
void setScreen(int s);
void drawTopBar(const char* title);
void drawEStopButton();
void drawHome();
void drawVacScreen(bool full);
void drawPpmScreen(bool full);
void drawLiveScreen(bool full);
void drawSettingsScreen(bool full);
void drawDiagScreen();
void drawEStopConfirm();
void drawEStopLatched();
void drawBootAnimation();
void drawBigButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t txt, int textSize);
void drawSmallButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t txt);
void drawValueBox(int x, int y, int w, int h, const char* label, int value, const char* suffix);
void drawVacGraph();
void drawPpmGraph();
void drawLiveBars();
void updateGraphs();
void handleTouch();
void startHoldAction(int action);
void processHoldAction();
void performAction(int action, bool fast);
void allOutputsSafe();
bool hit(int tx, int ty, int x, int y, int w, int h);
int clampInt(int v, int lo, int hi);

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  initPalette();
  applyBacklight();

  for (int i = 0; i < GRAPH_W; i++) {
    vacHistory[i] = 0;
    ppmHistory[i] = 0;
  }

  drawBootAnimation();
  setScreen(SCREEN_HOME);
}

void loop() {
  M5.update();
  handleTouch();

  if (millis() - lastGraphUpdate > 90) {
    lastGraphUpdate = millis();
    updateGraphs();

    if (currentScreen == SCREEN_VAC) {
      drawVacGraph();
      if (millis() - lastValueRedraw > 150) {
        drawValueBox(18, 45, 128, 50, "VAC", vacSet, "%");
        lastValueRedraw = millis();
      }
    } else if (currentScreen == SCREEN_PPM) {
      drawPpmGraph();
      if (millis() - lastValueRedraw > 150) {
        drawValueBox(18, 45, 128, 50, "PPM", ppmSet, "");
        lastValueRedraw = millis();
      }
    } else if (currentScreen == SCREEN_LIVE) {
      drawLiveBars();
    }
  }
}

void initPalette() {
  if (darkTheme) {
    C_BG = M5.Display.color565(12, 16, 24);
    C_PANEL = M5.Display.color565(25, 34, 48);
    C_PANEL2 = M5.Display.color565(35, 48, 66);
    C_TEXT = M5.Display.color565(240, 247, 255);
    C_MUTED = M5.Display.color565(138, 154, 170);
    C_ACCENT = M5.Display.color565(0, 210, 190);
    C_ACCENT2 = M5.Display.color565(255, 190, 64);
    C_WARN = M5.Display.color565(255, 120, 45);
    C_RED = M5.Display.color565(220, 20, 35);
    C_GREEN = M5.Display.color565(20, 200, 90);
    C_GRAPH = M5.Display.color565(80, 220, 255);
    C_GRID = M5.Display.color565(55, 68, 82);
  } else {
    C_BG = M5.Display.color565(230, 238, 245);
    C_PANEL = M5.Display.color565(255, 255, 255);
    C_PANEL2 = M5.Display.color565(212, 225, 238);
    C_TEXT = M5.Display.color565(20, 28, 38);
    C_MUTED = M5.Display.color565(85, 100, 120);
    C_ACCENT = M5.Display.color565(0, 120, 150);
    C_ACCENT2 = M5.Display.color565(255, 155, 40);
    C_WARN = M5.Display.color565(230, 90, 30);
    C_RED = M5.Display.color565(210, 25, 40);
    C_GREEN = M5.Display.color565(0, 155, 70);
    C_GRAPH = M5.Display.color565(20, 130, 180);
    C_GRID = M5.Display.color565(180, 195, 210);
  }
}

void applyBacklight() {
  backlightStep = clampInt(backlightStep, 1, 10);
  int brightness = map(backlightStep, 1, 10, 35, 255);
  M5.Display.setBrightness(brightness);
}

void setScreen(int s) {
  if (s != SCREEN_ESTOP_CONFIRM && s != SCREEN_ESTOP_LATCHED) previousScreen = currentScreen;
  currentScreen = s;
  lastValueRedraw = 0;
  activeHoldAction = ACT_NONE;

  if (s == SCREEN_HOME) drawHome();
  else if (s == SCREEN_VAC) drawVacScreen(true);
  else if (s == SCREEN_PPM) drawPpmScreen(true);
  else if (s == SCREEN_LIVE) drawLiveScreen(true);
  else if (s == SCREEN_SETTINGS) drawSettingsScreen(true);
  else if (s == SCREEN_DIAG) drawDiagScreen();
  else if (s == SCREEN_ESTOP_CONFIRM) drawEStopConfirm();
  else if (s == SCREEN_ESTOP_LATCHED) drawEStopLatched();
}

bool hit(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void drawTopBar(const char* title) {
  M5.Display.fillRect(0, 0, SW, TOP_H, C_PANEL2);
  M5.Display.setTextDatum(ML_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.drawString(title, 8, 15);
  drawEStopButton();
}

void drawEStopButton() {
  M5.Display.fillRoundRect(ESTOP_X, ESTOP_Y, ESTOP_W, ESTOP_H, 6, C_RED);
  M5.Display.drawRoundRect(ESTOP_X, ESTOP_Y, ESTOP_W, ESTOP_H, 6, C_TEXT);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, C_RED);
  M5.Display.drawString("E-STOP", ESTOP_X + ESTOP_W / 2, ESTOP_Y + ESTOP_H / 2);
}

void drawBigButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t txt, int textSize) {
  M5.Display.fillRoundRect(x, y, w, h, 14, fill);
  M5.Display.drawRoundRect(x, y, w, h, 14, C_TEXT);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(txt, fill);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void drawSmallButton(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t txt) {
  M5.Display.fillRoundRect(x, y, w, h, 8, fill);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_TEXT);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(txt, fill);
  M5.Display.drawString(label, x + w / 2, y + h / 2);
}

void drawValueBox(int x, int y, int w, int h, const char* label, int value, const char* suffix) {
  M5.Display.fillRoundRect(x, y, w, h, 10, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 10, C_ACCENT);
  M5.Display.setTextDatum(ML_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.drawString(label, x + 8, y + 12);

  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  char buf[16];
  snprintf(buf, sizeof(buf), "%d%s", value, suffix);
  M5.Display.drawString(buf, x + w / 2, y + 33);
}

void drawHome() {
  M5.Display.fillScreen(C_BG);
  drawTopBar("Pulse Controller");

  drawBigButton(12, 42, 143, 62, "VAC", C_ACCENT, TFT_BLACK, 3);
  drawBigButton(165, 42, 143, 62, "PPM", C_ACCENT2, TFT_BLACK, 3);
  drawBigButton(12, 116, 143, 52, running ? "STOP" : "START", running ? C_RED : C_GREEN, TFT_WHITE, 2);
  drawBigButton(165, 116, 143, 52, "LIVE", C_PANEL2, C_TEXT, 2);
  drawBigButton(12, 180, 143, 46, "TEST", C_PANEL2, C_TEXT, 2);
  drawBigButton(165, 180, 143, 46, "SETTINGS", C_PANEL2, C_TEXT, 2);
}

void drawVacScreen(bool full) {
  if (full) {
    M5.Display.fillScreen(C_BG);
    drawTopBar("VAC Control");
    drawValueBox(18, 45, 128, 50, "VAC", vacSet, "%");
    drawBigButton(18, 154, 132, 70, "-", C_PANEL2, C_TEXT, 5);
    drawBigButton(170, 154, 132, 70, "+", C_ACCENT, TFT_BLACK, 5);
    drawSmallButton(155, 48, 44, 24, "BACK", C_PANEL2, C_TEXT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_MUTED, C_BG);
    M5.Display.drawString("0-100% ramp monitor", 18, 107);
    drawVacGraph();
  }
}

void drawPpmScreen(bool full) {
  if (full) {
    M5.Display.fillScreen(C_BG);
    drawTopBar("PPM Control");
    drawValueBox(18, 45, 128, 50, "PPM", ppmSet, "");
    drawBigButton(18, 154, 132, 70, "-", C_PANEL2, C_TEXT, 5);
    drawBigButton(170, 154, 132, 70, "+", C_ACCENT2, TFT_BLACK, 5);
    drawSmallButton(155, 48, 44, 24, "BACK", C_PANEL2, C_TEXT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_MUTED, C_BG);
    M5.Display.drawString("Pumps per minute frequency", 18, 107);
    drawPpmGraph();
  }
}

void drawLiveScreen(bool full) {
  if (full) {
    M5.Display.fillScreen(C_BG);
    drawTopBar("Live Status");
    drawSmallButton(8, 204, 66, 28, "BACK", C_PANEL2, C_TEXT);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_TEXT, C_BG);
    M5.Display.drawString("PPM", 22, 58);
    M5.Display.drawString("VAC", 22, 132);
    drawLiveBars();
  }
}

void drawLiveBars() {
  // PPM bar
  int ppmMax = 100;
  int ppmFill = map(clampInt(ppmSet, 0, ppmMax), 0, ppmMax, 0, 210);
  M5.Display.fillRoundRect(76, 47, 220, 34, 8, C_PANEL);
  M5.Display.drawRoundRect(76, 47, 220, 34, 8, C_GRID);
  if (ppmFill > 0) M5.Display.fillRoundRect(80, 51, ppmFill, 26, 6, C_ACCENT2);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  char pbuf[16]; snprintf(pbuf, sizeof(pbuf), "%d", ppmSet);
  M5.Display.drawString(pbuf, 186, 64);

  // VAC bar
  int vacFill = map(clampInt(vacSet, 0, 100), 0, 100, 0, 210);
  M5.Display.fillRoundRect(76, 121, 220, 34, 8, C_PANEL);
  M5.Display.drawRoundRect(76, 121, 220, 34, 8, C_GRID);
  if (vacFill > 0) M5.Display.fillRoundRect(80, 125, vacFill, 26, 6, C_ACCENT);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL);
  char vbuf[16]; snprintf(vbuf, sizeof(vbuf), "%d%%", vacSet);
  M5.Display.drawString(vbuf, 186, 138);
}

void drawSettingsScreen(bool full) {
  if (full) {
    M5.Display.fillScreen(C_BG);
    drawTopBar("Settings");
    drawSmallButton(8, 204, 66, 28, "BACK", C_PANEL2, C_TEXT);

    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_TEXT, C_BG);
    M5.Display.drawString("Theme", 18, 55);
    drawBigButton(128, 42, 172, 42, darkTheme ? "DARK" : "LIGHT", C_PANEL2, C_TEXT, 2);

    M5.Display.drawString("Backlight", 18, 113);
    drawBigButton(18, 152, 72, 44, "-", C_PANEL2, C_TEXT, 3);
    drawBigButton(230, 152, 72, 44, "+", C_ACCENT, TFT_BLACK, 3);

    // 10-step slider
    M5.Display.fillRoundRect(102, 160, 116, 24, 6, C_PANEL);
    for (int i = 1; i <= 10; i++) {
      int x = 108 + (i - 1) * 11;
      uint16_t col = (i <= backlightStep) ? C_ACCENT : C_GRID;
      M5.Display.fillRect(x, 165, 8, 14, col);
    }
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_TEXT, C_BG);
    char b[8]; snprintf(b, sizeof(b), "%d/10", backlightStep);
    M5.Display.drawString(b, 160, 198);
  }
}

void drawDiagScreen() {
  M5.Display.fillScreen(C_BG);
  drawTopBar("Manual Test");
  drawSmallButton(8, 204, 66, 28, "BACK", C_PANEL2, C_TEXT);
  drawBigButton(22, 48, 126, 46, "PUMP", C_PANEL2, C_TEXT, 2);
  drawBigButton(172, 48, 126, 46, "PULSE", C_PANEL2, C_TEXT, 2);
  drawBigButton(22, 112, 126, 46, "VAC", C_PANEL2, C_TEXT, 2);
  drawBigButton(172, 112, 126, 46, "RELEASE", C_PANEL2, C_TEXT, 2);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.drawString("Demo only - outputs disabled", 160, 185);
}

void drawEStopConfirm() {
  M5.Display.fillScreen(C_RED);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextColor(TFT_WHITE, C_RED);
  M5.Display.setTextSize(3);
  M5.Display.drawString("E-STOP", 160, 55);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PRESS RED AREA", 160, 105);
  M5.Display.drawString("TO RELEASE PRESSURE", 160, 135);
  M5.Display.fillRect(0, 216, SW, 24, C_GREEN);
  M5.Display.setTextColor(TFT_BLACK, C_GREEN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("CANCEL / RETURN", 160, 228);
}

void drawEStopLatched() {
  M5.Display.fillScreen(C_RED);
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextColor(TFT_WHITE, C_RED);
  M5.Display.setTextSize(3);
  M5.Display.drawString("STOPPED", 160, 55);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PRESSURE RELEASED", 160, 105);
  M5.Display.drawString("POSITIVE + NEGATIVE", 160, 135);
  M5.Display.fillRect(0, 216, SW, 24, C_GREEN);
  M5.Display.setTextColor(TFT_BLACK, C_GREEN);
  M5.Display.setTextSize(2);
  M5.Display.drawString("RETURN", 160, 228);
}

void allOutputsSafe() {
  // Future real-output behavior goes here:
  // pump OFF, pulse solenoid OFF, release valve OPEN, VAC valve OPEN/SAFE,
  // positive and negative pressure released immediately.
  running = false;
  emergencyLatched = true;
  Serial.println("E-STOP: all simulated outputs safe, positive and negative pressure released.");
}

void updateGraphs() {
  // Simulated VAC ramp toward set value
  float target = (float)vacSet;
  vacActual += (target - vacActual) * 0.08;
  if (abs(target - vacActual) < 0.15) vacActual = target;

  vacHistory[graphIndex] = clampInt((int)vacActual, 0, 100);

  // PPM waveform is frequency driven. Height is constant. 0 = flat/off.
  int pval = 0;
  if (ppmSet > 0) {
    float freqStep = 0.05 + ((float)ppmSet / 100.0) * 0.85;
    ppmPhase += freqStep;
    if (ppmPhase > 6.28318) ppmPhase -= 6.28318;
    float wave = (sin(ppmPhase) + 1.0) * 0.5;
    pval = (int)(wave * 100.0);
  }
  ppmHistory[graphIndex] = pval;

  graphIndex++;
  if (graphIndex >= GRAPH_W) graphIndex = 0;
}

void drawGraphFrame(int gx, int gy, int gw, int gh, const char* lowLabel, const char* highLabel) {
  M5.Display.fillRoundRect(gx, gy, gw, gh, 8, C_PANEL);
  M5.Display.drawRoundRect(gx, gy, gw, gh, 8, C_GRID);

  M5.Display.drawLine(gx + 24, gy + 8, gx + 24, gy + gh - 12, C_GRID);
  M5.Display.drawLine(gx + 24, gy + gh - 12, gx + gw - 8, gy + gh - 12, C_GRID);

  for (int i = 1; i < 4; i++) {
    int y = gy + 8 + i * ((gh - 20) / 4);
    M5.Display.drawLine(gx + 25, y, gx + gw - 8, y, C_GRID);
  }

  M5.Display.setTextDatum(MR_DATUM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.drawString(highLabel, gx + 21, gy + 12);
  M5.Display.drawString(lowLabel, gx + 21, gy + gh - 14);
}

void drawVacGraph() {
  int gx = 88;
  int gy = 101;
  int gw = 220;
  int gh = 46;
  drawGraphFrame(gx, gy, gw, gh, "0", "100");

  int plotX = gx + 26;
  int plotY = gy + 8;
  int plotW = gw - 36;
  int plotH = gh - 20;

  // Target line
  int targetY = plotY + plotH - map(vacSet, 0, 100, 0, plotH);
  M5.Display.drawLine(plotX, targetY, plotX + plotW, targetY, C_ACCENT2);

  int prevX = plotX;
  int prevY = plotY + plotH;
  for (int i = 0; i < plotW; i++) {
    int idx = (graphIndex + i) % GRAPH_W;
    int val = vacHistory[idx];
    int x = plotX + i;
    int y = plotY + plotH - map(val, 0, 100, 0, plotH);
    if (i > 0) M5.Display.drawLine(prevX, prevY, x, y, C_GRAPH);
    prevX = x;
    prevY = y;
  }
}

void drawPpmGraph() {
  int gx = 88;
  int gy = 101;
  int gw = 220;
  int gh = 46;
  drawGraphFrame(gx, gy, gw, gh, "", "");

  int plotX = gx + 26;
  int plotY = gy + 8;
  int plotW = gw - 36;
  int plotH = gh - 20;
  int midY = plotY + plotH / 2;

  if (ppmSet == 0) {
    M5.Display.drawLine(plotX, midY, plotX + plotW, midY, C_MUTED);
    return;
  }

  int prevX = plotX;
  int prevY = midY;
  for (int i = 0; i < plotW; i++) {
    int idx = (graphIndex + i) % GRAPH_W;
    int val = ppmHistory[idx];
    int x = plotX + i;
    int y = plotY + plotH - map(val, 0, 100, 0, plotH);
    if (i > 0) M5.Display.drawLine(prevX, prevY, x, y, C_ACCENT2);
    prevX = x;
    prevY = y;
  }
}

void drawBootAnimation() {
  M5.Display.fillScreen(TFT_BLACK);

  // Fly in
  for (int x = -120; x <= 92; x += 8) {
    M5.Display.fillScreen(TFT_BLACK);
    drawCartoonMilkBottle(x, 72, 0, true);
    delay(32);
  }

  // Stop in center and look right, left, right
  for (int i = 0; i < 3; i++) {
    M5.Display.fillScreen(TFT_BLACK);
    drawCartoonMilkBottle(92, 72, 1, true);
    delay(320);
    M5.Display.fillScreen(TFT_BLACK);
    drawCartoonMilkBottle(92, 72, -1, true);
    delay(320);
  }
  M5.Display.fillScreen(TFT_BLACK);
  drawCartoonMilkBottle(92, 72, 1, true);
  delay(420);

  // Fly out
  for (int x = 92; x <= 340; x += 10) {
    M5.Display.fillScreen(TFT_BLACK);
    drawCartoonMilkBottle(x, 72, 1, true);
    delay(25);
  }
}

void drawCartoonMilkBottle(int x, int y, int eyeDir, bool flames) {
  // Flames behind bottle, bigger and recognizable. Orange outer, yellow mid, red tips.
  if (flames) {
    int fx = x - 40;
    int fy = y + 44;
    uint16_t orange = M5.Display.color565(255, 105, 0);
    uint16_t yellow = M5.Display.color565(255, 215, 40);
    uint16_t red = M5.Display.color565(220, 25, 20);
    M5.Display.fillTriangle(fx, fy + 16, fx + 38, fy - 10, fx + 70, fy + 20, orange);
    M5.Display.fillTriangle(fx - 12, fy + 38, fx + 32, fy + 4, fx + 70, fy + 42, red);
    M5.Display.fillTriangle(fx + 10, fy + 20, fx + 45, fy - 5, fx + 78, fy + 26, yellow);
    M5.Display.fillCircle(fx + 28, fy + 17, 17, orange);
    M5.Display.fillCircle(fx + 50, fy + 22, 13, yellow);
  }

  // Bottle body tilted-ish, cartoony
  uint16_t glass = M5.Display.color565(235, 250, 255);
  uint16_t milk = M5.Display.color565(255, 255, 245);
  uint16_t outline = M5.Display.color565(35, 55, 75);
  uint16_t cap = M5.Display.color565(0, 180, 210);

  M5.Display.fillRoundRect(x + 38, y + 24, 82, 100, 18, glass);
  M5.Display.drawRoundRect(x + 38, y + 24, 82, 100, 18, outline);
  M5.Display.fillRoundRect(x + 56, y + 0, 46, 35, 10, glass);
  M5.Display.drawRoundRect(x + 56, y + 0, 46, 35, 10, outline);
  M5.Display.fillRoundRect(x + 54, y - 10, 50, 18, 8, cap);
  M5.Display.drawRoundRect(x + 54, y - 10, 50, 18, 8, outline);

  // Milk fill and label
  M5.Display.fillRoundRect(x + 45, y + 58, 68, 58, 12, milk);
  M5.Display.fillRoundRect(x + 51, y + 72, 56, 25, 8, M5.Display.color565(20, 30, 45));
  M5.Display.setTextDatum(MC_DATUM);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, M5.Display.color565(20, 30, 45));
  M5.Display.drawString("MILK", x + 79, y + 84);

  // Big cartoon eyes
  int eyeOffset = eyeDir * 4;
  M5.Display.fillCircle(x + 65, y + 45, 12, TFT_WHITE);
  M5.Display.fillCircle(x + 94, y + 45, 12, TFT_WHITE);
  M5.Display.drawCircle(x + 65, y + 45, 12, outline);
  M5.Display.drawCircle(x + 94, y + 45, 12, outline);
  M5.Display.fillCircle(x + 65 + eyeOffset, y + 46, 5, TFT_BLACK);
  M5.Display.fillCircle(x + 94 + eyeOffset, y + 46, 5, TFT_BLACK);

  // Smile / motion
  M5.Display.drawArc(x + 80, y + 61, 16, 8, 20, 160, outline);
  M5.Display.drawLine(x + 30, y + 130, x + 5, y + 145, M5.Display.color565(255, 200, 60));
  M5.Display.drawLine(x + 42, y + 132, x + 18, y + 154, M5.Display.color565(255, 100, 40));
}

void startHoldAction(int action) {
  activeHoldAction = action;
  activeHoldStart = millis();
  lastHoldRepeat = 0;
  performAction(action, false);
}

void processHoldAction() {
  if (activeHoldAction == ACT_NONE) return;
  unsigned long now = millis();
  unsigned long held = now - activeHoldStart;
  unsigned long interval = (held > 3000) ? 115 : 260;
  bool fast = held > 3000;
  if (lastHoldRepeat == 0) lastHoldRepeat = now;
  if (now - lastHoldRepeat >= interval) {
    lastHoldRepeat = now;
    performAction(activeHoldAction, fast);
  }
}

void performAction(int action, bool fast) {
  int vacStep = fast ? 10 : 2;
  int ppmStep = fast ? 10 : 1;
  int lightStep = 1;

  if (action == ACT_VAC_MINUS) vacSet = clampInt(vacSet - vacStep, 0, 100);
  else if (action == ACT_VAC_PLUS) vacSet = clampInt(vacSet + vacStep, 0, 100);
  else if (action == ACT_PPM_MINUS) ppmSet = clampInt(ppmSet - ppmStep, 0, 100);
  else if (action == ACT_PPM_PLUS) ppmSet = clampInt(ppmSet + ppmStep, 0, 100);
  else if (action == ACT_BACKLIGHT_MINUS) { backlightStep = clampInt(backlightStep - lightStep, 1, 10); applyBacklight(); drawSettingsScreen(true); }
  else if (action == ACT_BACKLIGHT_PLUS) { backlightStep = clampInt(backlightStep + lightStep, 1, 10); applyBacklight(); drawSettingsScreen(true); }

  // Redraw only value areas for controls to reduce flash.
  if (currentScreen == SCREEN_VAC) drawValueBox(18, 45, 128, 50, "VAC", vacSet, "%");
  if (currentScreen == SCREEN_PPM) drawValueBox(18, 45, 128, 50, "PPM", ppmSet, "");
}

void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.isPressed()) {
    lastTouchX = t.x;
    lastTouchY = t.y;

    if (t.wasPressed()) {
      // Universal E-stop button, except on actual E-stop screens.
      if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_LATCHED && hit(t.x, t.y, ESTOP_X, ESTOP_Y, ESTOP_W, ESTOP_H)) {
        setScreen(SCREEN_ESTOP_CONFIRM);
        return;
      }

      if (currentScreen == SCREEN_HOME) {
        if (hit(t.x, t.y, 12, 42, 143, 62)) setScreen(SCREEN_VAC);
        else if (hit(t.x, t.y, 165, 42, 143, 62)) setScreen(SCREEN_PPM);
        else if (hit(t.x, t.y, 12, 116, 143, 52)) { running = !running; drawHome(); }
        else if (hit(t.x, t.y, 165, 116, 143, 52)) setScreen(SCREEN_LIVE);
        else if (hit(t.x, t.y, 12, 180, 143, 46)) setScreen(SCREEN_DIAG);
        else if (hit(t.x, t.y, 165, 180, 143, 46)) setScreen(SCREEN_SETTINGS);
      }
      else if (currentScreen == SCREEN_VAC) {
        if (hit(t.x, t.y, 155, 48, 44, 24)) setScreen(SCREEN_HOME);
        else if (hit(t.x, t.y, 18, 154, 132, 70)) startHoldAction(ACT_VAC_MINUS);
        else if (hit(t.x, t.y, 170, 154, 132, 70)) startHoldAction(ACT_VAC_PLUS);
      }
      else if (currentScreen == SCREEN_PPM) {
        if (hit(t.x, t.y, 155, 48, 44, 24)) setScreen(SCREEN_HOME);
        else if (hit(t.x, t.y, 18, 154, 132, 70)) startHoldAction(ACT_PPM_MINUS);
        else if (hit(t.x, t.y, 170, 154, 132, 70)) startHoldAction(ACT_PPM_PLUS);
      }
      else if (currentScreen == SCREEN_LIVE || currentScreen == SCREEN_DIAG) {
        if (hit(t.x, t.y, 8, 204, 66, 28)) setScreen(SCREEN_HOME);
      }
      else if (currentScreen == SCREEN_SETTINGS) {
        if (hit(t.x, t.y, 8, 204, 66, 28)) setScreen(SCREEN_HOME);
        else if (hit(t.x, t.y, 128, 42, 172, 42)) { darkTheme = !darkTheme; initPalette(); drawSettingsScreen(true); }
        else if (hit(t.x, t.y, 18, 152, 72, 44)) startHoldAction(ACT_BACKLIGHT_MINUS);
        else if (hit(t.x, t.y, 230, 152, 72, 44)) startHoldAction(ACT_BACKLIGHT_PLUS);
      }
      else if (currentScreen == SCREEN_ESTOP_CONFIRM) {
        if (hit(t.x, t.y, 0, 216, SW, 24)) setScreen(previousScreen == SCREEN_ESTOP_CONFIRM ? SCREEN_HOME : previousScreen);
        else { allOutputsSafe(); setScreen(SCREEN_ESTOP_LATCHED); }
      }
      else if (currentScreen == SCREEN_ESTOP_LATCHED) {
        if (hit(t.x, t.y, 0, 216, SW, 24)) { emergencyLatched = false; setScreen(SCREEN_HOME); }
      }
    } else {
      processHoldAction();
    }
  } else {
    activeHoldAction = ACT_NONE;
  }
}
