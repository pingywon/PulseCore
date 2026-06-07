/*
  4_Pulse_CoreS3_Demo
  Pulse Controller UI Demo for M5Stack CoreS3 / CoreS3 SE

  Purpose:
  - Screen sizing / UI demo only
  - No real hardware outputs are fired
  - Arduino IDE + M5Unified

  Install libraries:
  - M5Unified
  - M5GFX

  Board:
  - M5Stack CoreS3
*/

#include <M5Unified.h>
#include <math.h>

// ---------- Screen ----------
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;

// ---------- Fresh lighter color scheme ----------
static const uint16_t C_BG        = 0xF79E; // soft cream
static const uint16_t C_PANEL     = 0xFFFF; // white
static const uint16_t C_PANEL2    = 0xE73C; // pale blue/gray
static const uint16_t C_TEXT      = 0x2104; // deep slate
static const uint16_t C_MUTED     = 0x6B4D; // muted gray-blue
static const uint16_t C_PRIMARY   = 0x049F; // calm blue
static const uint16_t C_PRIMARY_D = 0x0378; // darker blue
static const uint16_t C_ACCENT    = 0xFCA0; // warm orange
static const uint16_t C_ACCENT_D  = 0xE3C0; // darker orange
static const uint16_t C_GREEN     = 0x46A6; // soft green
static const uint16_t C_RED       = 0xEAA9; // soft red
static const uint16_t C_LINE      = 0x9D76; // lavender/blue graph line
static const uint16_t C_GRID      = 0xD69A; // grid
static const uint16_t C_DARK      = 0x18E3; // dark outline

// ---------- App state ----------
enum ScreenId {
  SCREEN_HOME,
  SCREEN_PPM,
  SCREEN_SUCK,
  SCREEN_STATUS,
  SCREEN_DIAG
};

ScreenId currentScreen = SCREEN_HOME;
bool running = false;
int ppmValue = 48;     // squeezes / pumps per minute
int suckValue = 55;    // 1 - 100 percent

unsigned long lastFrame = 0;
unsigned long lastGraph = 0;
int graphScrollX = 0;
float phase = 0.0f;

// ---------- Helper drawing ----------
void clearScreen() {
  M5.Display.fillScreen(C_BG);
}

bool insideRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

void header(const char* title, bool showBack) {
  M5.Display.fillRoundRect(8, 6, 304, 32, 10, C_PANEL);
  M5.Display.drawRoundRect(8, 6, 304, 32, 10, C_GRID);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(showBack ? 60 : 18, 14);
  M5.Display.print(title);

  if (showBack) {
    M5.Display.fillRoundRect(14, 10, 38, 24, 7, C_PANEL2);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(C_TEXT);
    M5.Display.setCursor(24, 14);
    M5.Display.print("<");
  }
}

void buttonRect(int x, int y, int w, int h, const char* label, uint16_t fill, uint16_t textColor, int textSize = 2) {
  M5.Display.fillRoundRect(x, y, w, h, 14, fill);
  M5.Display.drawRoundRect(x, y, w, h, 14, C_DARK);
  M5.Display.setTextColor(textColor);
  M5.Display.setTextSize(textSize);

  int16_t tw = M5.Display.textWidth(label);
  int16_t th = 8 * textSize;
  M5.Display.setCursor(x + (w - tw) / 2, y + (h - th) / 2 + 2);
  M5.Display.print(label);
}

void pill(int x, int y, int w, const char* label, const char* value, uint16_t accent) {
  M5.Display.fillRoundRect(x, y, w, 40, 12, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, 40, 12, C_GRID);
  M5.Display.fillRoundRect(x + 5, y + 5, 8, 30, 4, accent);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(x + 20, y + 7);
  M5.Display.print(label);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(x + 20, y + 20);
  M5.Display.print(value);
}

void footerNav() {
  buttonRect(8, 196, 72, 36, "HOME", C_PANEL, C_TEXT, 1);
  buttonRect(86, 196, 72, 36, "PPM", C_PANEL, C_TEXT, 1);
  buttonRect(164, 196, 72, 36, "SUCK", C_PANEL, C_TEXT, 1);
  buttonRect(242, 196, 70, 36, "DIAG", C_PANEL, C_TEXT, 1);
}

void drawMiniGraphFrame(int x, int y, int w, int h, const char* label) {
  M5.Display.fillRoundRect(x, y, w, h, 12, C_PANEL);
  M5.Display.drawRoundRect(x, y, w, h, 12, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(x + 10, y + 7);
  M5.Display.print(label);
  for (int gx = x + 10; gx < x + w - 10; gx += 30) {
    M5.Display.drawFastVLine(gx, y + 24, h - 34, C_GRID);
  }
  for (int gy = y + 32; gy < y + h - 8; gy += 24) {
    M5.Display.drawFastHLine(x + 8, gy, w - 16, C_GRID);
  }
}

// Draws graph content only inside box, minimizing full-screen flashes.
void drawScrollingWave(int x, int y, int w, int h, int rate, int strength, uint16_t lineColor) {
  int gx = x + 9;
  int gy = y + 24;
  int gw = w - 18;
  int gh = h - 32;

  M5.Display.fillRect(gx, gy, gw, gh, C_PANEL);

  for (int xx = gx; xx < gx + gw; xx += 30) M5.Display.drawFastVLine(xx, gy, gh, C_GRID);
  for (int yy = gy; yy < gy + gh; yy += 24) M5.Display.drawFastHLine(gx, yy, gw, C_GRID);

  int mid = gy + gh / 2;
  int amp = map(strength, 1, 100, 8, gh / 2 - 4);
  float freq = rate / 60.0f;
  float tightness = 0.13f + freq * 0.08f;

  int lastX = gx;
  int lastY = mid;
  for (int i = 0; i < gw; i++) {
    float t = (i + graphScrollX) * tightness;
    // A rounded pulse/squeeze wave, not just a pure sine.
    float s = sinf(t);
    float shaped = (s > 0) ? powf(s, 0.65f) : s * 0.45f;
    int yy = mid - (int)(shaped * amp);
    int xx = gx + i;
    if (i > 0) M5.Display.drawLine(lastX, lastY, xx, yy, lineColor);
    lastX = xx;
    lastY = yy;
  }

  // Moving indicator dot
  int dotX = gx + gw - 20;
  float s = sinf((gw - 20 + graphScrollX) * tightness);
  float shaped = (s > 0) ? powf(s, 0.65f) : s * 0.45f;
  int dotY = mid - (int)(shaped * amp);
  M5.Display.fillCircle(dotX, dotY, 5, C_ACCENT);
}

// ---------- Boot animation: flaming cartoon milk bottle ----------
void drawBottle(int cx, int cy, int eyeDir, bool flameOn) {
  // Flame trail
  if (flameOn) {
    M5.Display.fillTriangle(cx - 62, cy + 4, cx - 98, cy - 18, cx - 86, cy + 18, C_ACCENT_D);
    M5.Display.fillTriangle(cx - 56, cy, cx - 86, cy - 10, cx - 78, cy + 12, C_ACCENT);
    M5.Display.fillCircle(cx - 96, cy - 15, 6, C_ACCENT);
    M5.Display.fillCircle(cx - 103, cy + 10, 5, C_ACCENT_D);
  }

  // Bottle tilted slightly, cartoon style using simple primitives
  M5.Display.fillRoundRect(cx - 42, cy - 34, 76, 60, 14, 0xFFFF);
  M5.Display.drawRoundRect(cx - 42, cy - 34, 76, 60, 14, C_DARK);
  M5.Display.fillRoundRect(cx + 10, cy - 52, 28, 26, 7, 0xFFFF);
  M5.Display.drawRoundRect(cx + 10, cy - 52, 28, 26, 7, C_DARK);
  M5.Display.fillRect(cx + 14, cy - 57, 20, 8, C_PRIMARY);
  M5.Display.drawRect(cx + 14, cy - 57, 20, 8, C_DARK);

  // Milk label
  M5.Display.fillRoundRect(cx - 30, cy + 3, 50, 17, 6, C_PANEL2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_PRIMARY_D);
  M5.Display.setCursor(cx - 21, cy + 8);
  M5.Display.print("MILK");

  // Cartoon eyes
  int pupilOffset = eyeDir * 3;
  M5.Display.fillCircle(cx - 17, cy - 15, 10, 0xFFFF);
  M5.Display.fillCircle(cx + 10, cy - 15, 10, 0xFFFF);
  M5.Display.drawCircle(cx - 17, cy - 15, 10, C_DARK);
  M5.Display.drawCircle(cx + 10, cy - 15, 10, C_DARK);
  M5.Display.fillCircle(cx - 17 + pupilOffset, cy - 14, 4, C_DARK);
  M5.Display.fillCircle(cx + 10 + pupilOffset, cy - 14, 4, C_DARK);

  // Smile
  M5.Display.drawArc(cx - 4, cy - 1, 16, 8, 20, 160, C_DARK);
}

void bootAnimation() {
  M5.Display.fillScreen(C_BG);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(72, 20);
  M5.Display.print("Pulse Demo");

  // Fly in slowly
  for (int x = -80; x <= 160; x += 8) {
    M5.Display.fillRect(0, 55, SCREEN_W, 145, C_BG);
    drawBottle(x, 122, 1, true);
    delay(45);
  }

  // Stop in middle and look around
  int looks[] = {1, -1, 1, 0};
  for (int i = 0; i < 4; i++) {
    M5.Display.fillRect(0, 55, SCREEN_W, 145, C_BG);
    drawBottle(160, 122, looks[i], true);
    delay(450);
  }

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(96, 206);
  M5.Display.print("screen sizing build 4");
  delay(500);

  // Fly off screen
  for (int x = 160; x <= 400; x += 10) {
    M5.Display.fillRect(0, 55, SCREEN_W, 145, C_BG);
    drawBottle(x, 122, 1, true);
    delay(35);
  }
}

// ---------- Screens ----------
void drawHome() {
  currentScreen = SCREEN_HOME;
  clearScreen();
  header("PULSE CONTROL", false);

  char ppmBuf[24];
  snprintf(ppmBuf, sizeof(ppmBuf), "%d PPM", ppmValue);
  char suckBuf[24];
  snprintf(suckBuf, sizeof(suckBuf), "%d%% Suck", suckValue);

  pill(12, 48, 142, "Pumps per minute", ppmBuf, C_PRIMARY);
  pill(166, 48, 142, "Suck Level", suckBuf, C_ACCENT);

  buttonRect(12, 98, 142, 56, running ? "STOP" : "START", running ? C_RED : C_GREEN, C_TEXT, 2);
  buttonRect(166, 98, 142, 56, "STATUS", C_PANEL, C_TEXT, 2);

  buttonRect(12, 162, 142, 50, "PPM", C_PRIMARY, 0xFFFF, 2);
  buttonRect(166, 162, 142, 50, "SUCK", C_ACCENT, C_TEXT, 2);
}

void drawPPMScreen() {
  currentScreen = SCREEN_PPM;
  clearScreen();
  header("PUMPS PER MINUTE", true);

  M5.Display.setTextSize(4);
  M5.Display.setTextColor(C_PRIMARY_D);
  M5.Display.setCursor(30, 50);
  M5.Display.print(ppmValue);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(118, 62);
  M5.Display.print("PPM");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(28, 88);
  M5.Display.print("Adjust squeezes / pumps per minute");

  drawMiniGraphFrame(16, 105, 288, 76, "Rate visualizer");
  drawScrollingWave(16, 105, 288, 76, ppmValue, 65, C_PRIMARY);

  buttonRect(18, 190, 86, 42, "-", C_PANEL2, C_TEXT, 4);
  buttonRect(116, 190, 86, 42, "+", C_PRIMARY, 0xFFFF, 4);
  buttonRect(214, 190, 90, 42, "HOME", C_PANEL, C_TEXT, 1);
}

void drawSuckScreen() {
  currentScreen = SCREEN_SUCK;
  clearScreen();
  header("SUCK CONTROL", true);

  M5.Display.setTextSize(4);
  M5.Display.setTextColor(C_ACCENT_D);
  M5.Display.setCursor(34, 50);
  M5.Display.print(suckValue);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT);
  M5.Display.setCursor(122, 62);
  M5.Display.print("% SUCK");

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(28, 88);
  M5.Display.print("Adjust suction feel from 1 to 100%");

  drawMiniGraphFrame(16, 105, 288, 76, "Suck strength visualizer");
  drawScrollingWave(16, 105, 288, 76, ppmValue, suckValue, C_ACCENT_D);

  buttonRect(18, 190, 86, 42, "-", C_PANEL2, C_TEXT, 4);
  buttonRect(116, 190, 86, 42, "+", C_ACCENT, C_TEXT, 4);
  buttonRect(214, 190, 90, 42, "HOME", C_PANEL, C_TEXT, 1);
}

void drawStatusScreen() {
  currentScreen = SCREEN_STATUS;
  clearScreen();
  header("LIVE STATUS", true);

  char ppmBuf[24];
  snprintf(ppmBuf, sizeof(ppmBuf), "%d PPM", ppmValue);
  char suckBuf[24];
  snprintf(suckBuf, sizeof(suckBuf), "%d%%", suckValue);

  pill(12, 48, 142, "Rate", ppmBuf, C_PRIMARY);
  pill(166, 48, 142, "Suck", suckBuf, C_ACCENT);

  drawMiniGraphFrame(16, 98, 288, 78, running ? "Running simulation" : "Idle simulation");
  drawScrollingWave(16, 98, 288, 78, ppmValue, suckValue, running ? C_GREEN : C_LINE);

  buttonRect(18, 190, 138, 42, running ? "STOP" : "START", running ? C_RED : C_GREEN, C_TEXT, 2);
  buttonRect(166, 190, 138, 42, "HOME", C_PANEL, C_TEXT, 2);
}

void drawDiagScreen() {
  currentScreen = SCREEN_DIAG;
  clearScreen();
  header("MANUAL TEST", true);

  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED);
  M5.Display.setCursor(18, 48);
  M5.Display.print("Demo buttons only - no outputs fired");

  buttonRect(16, 68, 138, 46, "PUMP", C_PANEL, C_TEXT, 2);
  buttonRect(166, 68, 138, 46, "PULSE", C_PANEL, C_TEXT, 2);
  buttonRect(16, 124, 138, 46, "RELEASE", C_PANEL, C_TEXT, 2);
  buttonRect(166, 124, 138, 46, "VALVE", C_PANEL, C_TEXT, 2);
  buttonRect(18, 190, 286, 42, "HOME", C_PRIMARY, 0xFFFF, 2);
}

void redrawCurrentScreen() {
  if (currentScreen == SCREEN_HOME) drawHome();
  else if (currentScreen == SCREEN_PPM) drawPPMScreen();
  else if (currentScreen == SCREEN_SUCK) drawSuckScreen();
  else if (currentScreen == SCREEN_STATUS) drawStatusScreen();
  else if (currentScreen == SCREEN_DIAG) drawDiagScreen();
}

void handleTouch(int x, int y) {
  // Back button common area
  if (currentScreen != SCREEN_HOME && insideRect(x, y, 8, 6, 52, 36)) {
    drawHome();
    return;
  }

  if (currentScreen == SCREEN_HOME) {
    if (insideRect(x, y, 12, 98, 142, 56)) {
      running = !running;
      drawHome();
    } else if (insideRect(x, y, 166, 98, 142, 56)) {
      drawStatusScreen();
    } else if (insideRect(x, y, 12, 162, 142, 50)) {
      drawPPMScreen();
    } else if (insideRect(x, y, 166, 162, 142, 50)) {
      drawSuckScreen();
    }
  }
  else if (currentScreen == SCREEN_PPM) {
    if (insideRect(x, y, 18, 190, 86, 42)) {
      ppmValue -= 2;
      if (ppmValue < 10) ppmValue = 10;
      drawPPMScreen();
    } else if (insideRect(x, y, 116, 190, 86, 42)) {
      ppmValue += 2;
      if (ppmValue > 120) ppmValue = 120;
      drawPPMScreen();
    } else if (insideRect(x, y, 214, 190, 90, 42)) {
      drawHome();
    }
  }
  else if (currentScreen == SCREEN_SUCK) {
    if (insideRect(x, y, 18, 190, 86, 42)) {
      suckValue -= 5;
      if (suckValue < 1) suckValue = 1;
      drawSuckScreen();
    } else if (insideRect(x, y, 116, 190, 86, 42)) {
      suckValue += 5;
      if (suckValue > 100) suckValue = 100;
      drawSuckScreen();
    } else if (insideRect(x, y, 214, 190, 90, 42)) {
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
  // No full-screen redraw: only update graph areas.
  if (currentScreen == SCREEN_PPM) {
    drawScrollingWave(16, 105, 288, 76, ppmValue, 65, C_PRIMARY);
  } else if (currentScreen == SCREEN_SUCK) {
    drawScrollingWave(16, 105, 288, 76, ppmValue, suckValue, C_ACCENT_D);
  } else if (currentScreen == SCREEN_STATUS) {
    drawScrollingWave(16, 98, 288, 78, ppmValue, suckValue, running ? C_GREEN : C_LINE);
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);

  M5.Display.setRotation(1);
  M5.Display.setBrightness(190);
  M5.Display.setTextDatum(top_left);

  bootAnimation();
  drawHome();

  Serial.println("4_Pulse_CoreS3_Demo loaded. UI simulation only.");
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
    graphScrollX += max(1, ppmValue / 16);
    if (graphScrollX > 10000) graphScrollX = 0;
    updateLiveGraphOnly();
  }

  delay(5);
}
