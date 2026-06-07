#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SD.h>

// ============================================================
// 11_Pulse_CoreS3_Demo
// PulseCore Pluto 9000 touchscreen + Wi-Fi configuration demo
// M5Stack CoreS3/SE - Arduino IDE
// Simulation only: this sketch does NOT fire GPIO outputs.
// ============================================================

// ---------- Screen/state IDs ----------
enum ScreenId {
  SCREEN_HOME,
  SCREEN_VAC,
  SCREEN_PPM,
  SCREEN_LIVE,
  SCREEN_DIAG,
  SCREEN_SETTINGS,
  SCREEN_WIFI,
  SCREEN_ESTOP_CONFIRM,
  SCREEN_ESTOP_LATCHED
};

// ---------- Global app state declared before ALL functions ----------
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
bool largeButtons = true;
bool showLiveMenu = true;
bool showDiagMenu = true;
bool showSettingsMenu = true;
bool webRunning = false;
bool sdReady = false;

int vacTarget = 35;       // 0-100%, 0 = off
int vacActual = 0;        // simulated slow ramp toward target
int ppmTarget = 42;       // 0-60, 0 = off, 60 = 0.5 sec up + 0.5 sec down
int backlightStep = 7;    // 1-10
int uiScale = 1;          // 1 normal, 2 large

uint16_t C_BG, C_CARD, C_CARD2, C_TEXT, C_MUTED, C_ACCENT, C_ACCENT2, C_WARN, C_DANGER, C_OK, C_LINE, C_GRAPH;

int sw = 320;
int sh = 240;

unsigned long lastGraphTick = 0;
unsigned long lastVacRampTick = 0;
unsigned long lastLiveTick = 0;
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

// SoftAP / captive portal web config
const char* AP_SSID = "Pluto9000";
const char* AP_PASS = "";     // open demo AP. Add password later if wanted.
IPAddress apIP(192, 168, 4, 1);
DNSServer dnsServer;
WebServer server(80);
Preferences prefs;

// Graph buffers
const int GRAPH_W = 190;
int vacGraph[GRAPH_W];
int ppmGraph[GRAPH_W];
int ppmPhasePercent = 0;

// ---------- Utility ----------
int clampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

bool inRect(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx <= x + w && ty >= y && ty <= y + h;
}

String checked(bool v) {
  return v ? "checked" : "";
}

void initPalette() {
  if (darkTheme) {
    C_BG      = M5.Display.color565(5, 8, 14);
    C_CARD    = M5.Display.color565(18, 24, 36);
    C_CARD2   = M5.Display.color565(30, 38, 54);
    C_TEXT    = M5.Display.color565(236, 242, 248);
    C_MUTED   = M5.Display.color565(132, 148, 166);
    C_ACCENT  = M5.Display.color565(56, 218, 188);
    C_ACCENT2 = M5.Display.color565(116, 142, 255);
    C_WARN    = M5.Display.color565(255, 190, 64);
    C_DANGER  = M5.Display.color565(226, 24, 48);
    C_OK      = M5.Display.color565(48, 208, 108);
    C_LINE    = M5.Display.color565(64, 78, 98);
    C_GRAPH   = M5.Display.color565(255, 210, 75);
  } else {
    C_BG      = M5.Display.color565(230, 238, 246);
    C_CARD    = M5.Display.color565(248, 250, 252);
    C_CARD2   = M5.Display.color565(218, 230, 242);
    C_TEXT    = M5.Display.color565(18, 28, 42);
    C_MUTED   = M5.Display.color565(86, 100, 120);
    C_ACCENT  = M5.Display.color565(0, 155, 135);
    C_ACCENT2 = M5.Display.color565(60, 90, 216);
    C_WARN    = M5.Display.color565(210, 128, 20);
    C_DANGER  = M5.Display.color565(210, 20, 42);
    C_OK      = M5.Display.color565(20, 150, 72);
    C_LINE    = M5.Display.color565(165, 185, 205);
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
  M5.Display.fillScreen(C_BG);   // Used only on screen changes or hard mode changes.
}

void shiftGraph(int* graph, int w, int val) {
  for (int i = 0; i < w - 1; i++) graph[i] = graph[i + 1];
  graph[w - 1] = val;
}

// ---------- Settings persistence ----------
void saveConfigToNVS() {
  prefs.begin("pluto9000", false);
  prefs.putBool("dark", darkTheme);
  prefs.putBool("large", largeButtons);
  prefs.putBool("showLive", showLiveMenu);
  prefs.putBool("showDiag", showDiagMenu);
  prefs.putBool("showSet", showSettingsMenu);
  prefs.putInt("vac", vacTarget);
  prefs.putInt("ppm", ppmTarget);
  prefs.putInt("back", backlightStep);
  prefs.end();
}

void saveConfigToSD() {
  if (!sdReady) return;
  File f = SD.open("/pluto9000.cfg", FILE_WRITE);
  if (!f) return;
  f.println("# Pluto 9000 saved demo settings");
  f.print("darkTheme="); f.println(darkTheme ? 1 : 0);
  f.print("largeButtons="); f.println(largeButtons ? 1 : 0);
  f.print("showLive="); f.println(showLiveMenu ? 1 : 0);
  f.print("showDiag="); f.println(showDiagMenu ? 1 : 0);
  f.print("showSettings="); f.println(showSettingsMenu ? 1 : 0);
  f.print("vacTarget="); f.println(vacTarget);
  f.print("ppmTarget="); f.println(ppmTarget);
  f.print("backlightStep="); f.println(backlightStep);
  f.close();
}

void saveConfig() {
  saveConfigToNVS();
  saveConfigToSD();
}

void loadConfig() {
  prefs.begin("pluto9000", true);
  darkTheme = prefs.getBool("dark", true);
  largeButtons = prefs.getBool("large", true);
  showLiveMenu = prefs.getBool("showLive", true);
  showDiagMenu = prefs.getBool("showDiag", true);
  showSettingsMenu = prefs.getBool("showSet", true);
  vacTarget = clampInt(prefs.getInt("vac", 35), 0, 100);
  ppmTarget = clampInt(prefs.getInt("ppm", 42), 0, 60);
  backlightStep = clampInt(prefs.getInt("back", 7), 1, 10);
  prefs.end();
}

// ---------- Drawing primitives ----------
void textAt(int x, int y, const char* txt, uint16_t color, uint16_t bg, int size) {
  M5.Display.setTextSize(size);
  M5.Display.setTextColor(color, bg);
  M5.Display.setCursor(x, y);
  M5.Display.print(txt);
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t bg, uint16_t fg, int textSize) {
  M5.Display.fillRoundRect(x, y, w, h, 12, bg);
  M5.Display.drawRoundRect(x, y, w, h, 12, C_LINE);
  M5.Display.setTextSize(textSize);
  M5.Display.setTextColor(fg, bg);
  int16_t tw = M5.Display.textWidth(label);
  int tx = x + (w - tw) / 2;
  int ty = y + (h - (8 * textSize)) / 2;
  M5.Display.setCursor(tx, ty);
  M5.Display.print(label);
}

void drawHeader(const char* title, bool showBack, bool showEStop) {
  M5.Display.fillRoundRect(4, 4, 312, 34, 9, C_CARD);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(showBack ? 50 : 14, 12);
  M5.Display.print(title);

  if (showBack) {
    M5.Display.fillRoundRect(8, 8, 36, 26, 7, C_CARD2);
    M5.Display.setTextColor(C_TEXT, C_CARD2);
    M5.Display.setCursor(18, 14);
    M5.Display.print("<");
  }
  if (showEStop) {
    M5.Display.fillRoundRect(258, 8, 54, 26, 7, C_DANGER);
    M5.Display.setTextColor(TFT_WHITE, C_DANGER);
    M5.Display.setTextSize(1);
    M5.Display.setCursor(266, 17);
    M5.Display.print("E-STOP");
  }
}

void drawValueBox(int x, int y, int w, int h, const char* label, int value, const char* suffix, uint16_t accent) {
  M5.Display.fillRoundRect(x, y, w, h, 12, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 12, accent);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 10, y + 8);
  M5.Display.print(label);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(x + 12, y + 26);
  M5.Display.print(value);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + w - 36, y + 48);
  M5.Display.print(suffix);
}

void goScreen(ScreenId s) {
  currentScreen = s;
  touchHeld = false;
  heldAction = ACT_NONE;
  fillAppBg();
  if (s == SCREEN_HOME) drawHeader("Pluto 9000", false, true);
  if (s == SCREEN_VAC) drawHeader("VAC Setpoint", true, true);
  if (s == SCREEN_PPM) drawHeader("PPM Timing", true, true);
  if (s == SCREEN_LIVE) drawHeader("Live Monitor", true, true);
  if (s == SCREEN_DIAG) drawHeader("Manual Test", true, true);
  if (s == SCREEN_SETTINGS) drawHeader("Settings", true, true);
  if (s == SCREEN_WIFI) drawHeader("Wi-Fi Config", true, true);
}

// ---------- Boot animation: Milky + PulseCore + Pluto 9000 ----------
void drawPulseCoreLogo(int cx, int y) {
  uint16_t core = M5.Display.color565(52, 218, 188);
  uint16_t ring = M5.Display.color565(100, 125, 255);
  uint16_t bg = M5.Display.color565(5, 8, 14);
  M5.Display.fillCircle(cx - 78, y + 15, 16, ring);
  M5.Display.fillCircle(cx - 78, y + 15, 8, bg);
  M5.Display.drawCircle(cx - 78, y + 15, 20, core);
  M5.Display.drawLine(cx - 78, y - 9, cx - 78, y - 20, core);
  M5.Display.drawLine(cx - 78, y + 39, cx - 78, y + 50, core);
  M5.Display.drawLine(cx - 102, y + 15, cx - 115, y + 15, core);
  M5.Display.drawLine(cx - 54, y + 15, cx - 42, y + 15, core);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, bg);
  M5.Display.setCursor(cx - 36, y + 4);
  M5.Display.print("Pulse");
  M5.Display.setTextColor(core, bg);
  M5.Display.print("Core");
}

void drawMilkyBottle(int x, int y, int eyeOffset) {
  uint16_t glass = M5.Display.color565(230, 244, 255);
  uint16_t milk = M5.Display.color565(255, 255, 248);
  uint16_t outline = M5.Display.color565(18, 28, 40);
  uint16_t cap = M5.Display.color565(82, 178, 255);
  uint16_t label = M5.Display.color565(70, 222, 190);
  uint16_t shade = M5.Display.color565(190, 220, 238);

  // subtle speed/splash marks, no fire
  M5.Display.drawLine(x - 42, y + 42, x - 10, y + 42, M5.Display.color565(80, 120, 160));
  M5.Display.drawLine(x - 30, y + 58, x - 4, y + 58, M5.Display.color565(80, 120, 160));
  M5.Display.fillCircle(x - 18, y + 28, 3, milk);
  M5.Display.fillCircle(x - 32, y + 70, 2, milk);

  M5.Display.fillRoundRect(x, y + 16, 88, 68, 16, glass);
  M5.Display.fillRoundRect(x + 6, y + 46, 76, 32, 12, milk);
  M5.Display.drawRoundRect(x, y + 16, 88, 68, 16, outline);
  M5.Display.drawLine(x + 72, y + 24, x + 82, y + 72, shade);

  M5.Display.fillRoundRect(x + 16, y - 6, 56, 28, 9, glass);
  M5.Display.drawRoundRect(x + 16, y - 6, 56, 28, 9, outline);
  M5.Display.fillRoundRect(x + 24, y - 18, 40, 14, 5, cap);
  M5.Display.drawRoundRect(x + 24, y - 18, 40, 14, 5, outline);

  M5.Display.fillRoundRect(x + 12, y + 52, 64, 23, 7, label);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK, label);
  M5.Display.setCursor(x + 27, y + 60);
  M5.Display.print("milky");

  M5.Display.fillCircle(x + 31, y + 34, 10, TFT_WHITE);
  M5.Display.fillCircle(x + 59, y + 34, 10, TFT_WHITE);
  M5.Display.fillCircle(x + 31 + eyeOffset, y + 34, 4, TFT_BLACK);
  M5.Display.fillCircle(x + 59 + eyeOffset, y + 34, 4, TFT_BLACK);
  M5.Display.drawLine(x + 36, y + 47, x + 52, y + 47, outline);
}

void bootAnimation() {
  uint16_t bootBg = M5.Display.color565(5, 8, 14);
  M5.Display.fillScreen(bootBg);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(M5.Display.color565(132, 148, 166), bootBg);
  M5.Display.setCursor(92, 12);
  M5.Display.print("controller boot sequence");

  // Milky enters. Only redraw the animation band, not the whole screen.
  for (int x = -110; x <= 112; x += 8) {
    M5.Display.fillRect(0, 38, 320, 130, bootBg);
    drawMilkyBottle(x, 76, 2);
    delay(42);
  }

  // Milky looks around.
  int eyes[5] = {5, -5, 5, 0, 0};
  for (int i = 0; i < 5; i++) {
    M5.Display.fillRect(0, 38, 320, 130, bootBg);
    drawMilkyBottle(112, 76, eyes[i]);
    delay(310);
  }

  // Brand reveal: PulseCore rises, Pluto 9000 descends.
  for (int step = 0; step <= 54; step += 3) {
    M5.Display.fillRect(0, 38, 320, 190, bootBg);
    drawMilkyBottle(112, 76, 0);
    drawPulseCoreLogo(160, 218 - step);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE, bootBg);
    M5.Display.setCursor(74, -42 + step);
    M5.Display.print("PLUTO");
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(M5.Display.color565(70, 222, 190), bootBg);
    M5.Display.setCursor(116, -10 + step);
    M5.Display.print("9000");
    delay(45);
  }

  delay(550);

  for (int x = 112; x <= 345; x += 10) {
    M5.Display.fillRect(0, 38, 320, 130, bootBg);
    drawMilkyBottle(x, 76, 3);
    delay(34);
  }
}

// ---------- Wi-Fi web portal ----------
String htmlPage() {
  String ip = WiFi.softAPIP().toString();
  String s = "";
  s += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  s += "<title>Pluto 9000 Control</title>";
  s += "<style>body{margin:0;background:#070b12;color:#edf5fb;font-family:Arial,sans-serif;}";
  s += ".wrap{max-width:860px;margin:auto;padding:18px}.hero{background:linear-gradient(135deg,#111d2b,#111827 55%,#052b2a);border:1px solid #28455a;border-radius:24px;padding:22px;box-shadow:0 18px 45px #0008}.brand{font-size:34px;font-weight:900;letter-spacing:.4px}.sub{color:#91a9bb;margin-top:6px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:14px;margin-top:16px}.card{background:#111827;border:1px solid #26364b;border-radius:20px;padding:16px}.card h2{font-size:19px;margin:0 0 10px}.row{display:flex;gap:10px;align-items:center;justify-content:space-between;margin:13px 0}.val{font-size:28px;font-weight:800;color:#42dcc1}.range{width:100%}input[type=range]{accent-color:#42dcc1}.pill{display:inline-block;border:1px solid #35516a;border-radius:999px;padding:6px 10px;color:#b5c7d8;font-size:13px}.save{width:100%;border:0;border-radius:18px;padding:18px;background:#42dcc1;color:#061112;font-size:22px;font-weight:900;margin-top:18px}.danger{background:#3a0b12;border-color:#8c1b2e}.small{font-size:13px;color:#91a9bb;line-height:1.45}label{font-weight:700}input[type=number]{width:86px;background:#060a10;color:#fff;border:1px solid #3a4f68;border-radius:10px;padding:8px;font-size:18px}.toggle{transform:scale(1.35)}</style>";
  s += "<script>function sync(id,v){document.getElementById(id).innerText=v}</script></head><body><div class='wrap'>";
  s += String("<div class='hero'><div class='brand'>PulseCore Pluto 9000</div><div class='sub'>Connected to ad-hoc Wi-Fi: <b>Pluto9000</b> &bull; Web control at <b>") + ip + "</b></div>";
  s += "<div style='margin-top:12px'><span class='pill'>Demo UI</span> <span class='pill'>Settings save to memory</span> <span class='pill'>SD backup if available</span></div></div>";
  s += "<form method='POST' action='/save'><div class='grid'>";

  s += "<div class='card'><h2>Primary Control</h2>";
  s += String("<div class='row'><label>VAC setpoint</label><span class='val'><span id='vacv'>") + String(vacTarget) + "</span>%</span></div>";
  s += String("<input class='range' name='vac' type='range' min='0' max='100' value='") + String(vacTarget) + "' oninput='sync(\"vacv\",this.value)'>";
  s += String("<div class='row'><label>PPM</label><span class='val'><span id='ppmv'>") + String(ppmTarget) + "</span></span></div>";
  s += String("<input class='range' name='ppm' type='range' min='0' max='60' value='") + String(ppmTarget) + "' oninput='sync(\"ppmv\",this.value)'>";
  s += "<p class='small'>1 PPM = 30 seconds up and 30 seconds down. 60 PPM = 0.5 seconds up and 0.5 seconds down.</p></div>";

  s += "<div class='card'><h2>Screen + UI</h2>";
  s += String("<div class='row'><label>Theme</label><select name='theme' style='font-size:18px;padding:8px;border-radius:10px;background:#060a10;color:#fff'><option value='dark'") + String(darkTheme ? " selected" : "") + ">Dark</option><option value='light'" + String(!darkTheme ? " selected" : "") + ">Light</option></select></div>";
  s += String("<div class='row'><label>Backlight</label><span class='val'><span id='backv'>") + String(backlightStep) + "</span>/10</span></div>";
  s += String("<input class='range' name='back' type='range' min='1' max='10' value='") + String(backlightStep) + "' oninput='sync(\"backv\",this.value)'>";
  s += String("<div class='row'><label>Large buttons</label><input class='toggle' type='checkbox' name='large' ") + checked(largeButtons) + "></div></div>";

  s += "<div class='card'><h2>Menu Builder</h2>";
  s += String("<div class='row'><label>Show Live screen</label><input class='toggle' type='checkbox' name='showlive' ") + checked(showLiveMenu) + "></div>";
  s += String("<div class='row'><label>Show Manual Test</label><input class='toggle' type='checkbox' name='showdiag' ") + checked(showDiagMenu) + "></div>";
  s += String("<div class='row'><label>Show Settings</label><input class='toggle' type='checkbox' name='showset' ") + checked(showSettingsMenu) + "></div>";
  s += "<p class='small'>Future builds can use these to hide whole screens or simplify the touchscreen interface.</p></div>";

  s += "<div class='card danger'><h2>Safety Behavior</h2><p class='small'>The touchscreen E-stop uses two steps: first opens the red unit E-stop screen, second press actually triggers the simulated pressure release. Real output code will force pump off and vent positive/negative pressure here.</p></div>";

  s += "</div><button class='save' type='submit'>SAVE TO PLUTO 9000</button></form></div></body></html>";
  return s;
}

void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleSave() {
  if (server.hasArg("vac")) vacTarget = clampInt(server.arg("vac").toInt(), 0, 100);
  if (server.hasArg("ppm")) ppmTarget = clampInt(server.arg("ppm").toInt(), 0, 60);
  if (server.hasArg("back")) backlightStep = clampInt(server.arg("back").toInt(), 1, 10);
  if (server.hasArg("theme")) darkTheme = (server.arg("theme") == "dark");
  largeButtons = server.hasArg("large");
  showLiveMenu = server.hasArg("showlive");
  showDiagMenu = server.hasArg("showdiag");
  showSettingsMenu = server.hasArg("showset");
  initPalette();
  applyBacklight();
  saveConfig();
  server.sendHeader("Location", "/");
  server.send(303);
  // Redraw only after a web save, because theme/menu may have changed.
  if (currentScreen == SCREEN_HOME) drawHome();
}

void handleNotFound() {
  server.send(200, "text/html", htmlPage());
}

void startWebPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", apIP);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);
  server.begin();
  webRunning = true;
}

// ---------- Emergency stop ----------
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
  vacActual = 0;
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
  M5.Display.setCursor(34, 38);
  M5.Display.print("STOPPED");
  M5.Display.setTextSize(2);
  M5.Display.setCursor(28, 84);
  M5.Display.print("PRESSURE RELEASED");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(32, 130);
  M5.Display.print("Pump OFF  |  VAC vented  |  Pulse open");
  M5.Display.setCursor(46, 150);
  M5.Display.print("All simulated outputs forced safe");
  M5.Display.fillRect(0, 216, 320, 24, C_OK);
  M5.Display.setTextColor(TFT_BLACK, C_OK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(79, 221);
  M5.Display.print("RETURN HOME");
}

// ---------- Screens ----------
void drawHome() {
  goScreen(SCREEN_HOME);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 44);
  M5.Display.print("Primary controls - simulation only");

  drawValueBox(12, 58, 142, 64, "VAC", vacTarget, "%", C_ACCENT);
  drawValueBox(166, 58, 142, 64, "PPM", ppmTarget, "", C_ACCENT2);

  drawButton(12, 134, 142, 46, running ? "STOP" : "START", running ? C_DANGER : C_OK, TFT_WHITE, 2);
  drawButton(166, 134, 142, 46, "LIVE", C_CARD2, C_TEXT, 2);
  drawButton(12, 188, 68, 38, "VAC", C_CARD2, C_TEXT, 2);
  drawButton(88, 188, 68, 38, "PPM", C_CARD2, C_TEXT, 2);
  drawButton(164, 188, 68, 38, "WEB", C_CARD2, C_TEXT, 2);
  drawButton(240, 188, 68, 38, "MORE", C_CARD2, C_TEXT, 2);
}

void drawMoreScreen() {
  goScreen(SCREEN_DIAG);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 44);
  M5.Display.print("Extra screens enabled from web menu");
  drawButton(14, 66, 292, 48, "MANUAL TEST", C_CARD2, C_TEXT, 2);
  drawButton(14, 126, 292, 48, "SETTINGS", C_CARD2, C_TEXT, 2);
  drawButton(14, 186, 292, 38, "WEB CONFIG", C_CARD2, C_TEXT, 2);
}

void drawGraphFrame(int x, int y, int w, int h, const char* title, bool percentAxis) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 8, y + 6);
  M5.Display.print(title);
  if (percentAxis) {
    M5.Display.setCursor(x + 5, y + 22); M5.Display.print("100");
    M5.Display.setCursor(x + 11, y + h - 14); M5.Display.print("0");
    M5.Display.drawLine(x + 30, y + 22, x + 30, y + h - 18, C_LINE);
    M5.Display.drawLine(x + 30, y + h - 18, x + w - 8, y + h - 18, C_LINE);
  } else {
    M5.Display.drawFastHLine(x + 10, y + (h / 2), w - 20, C_LINE);
  }
}

void renderVacGraph() {
  int x = 10, y = 54, w = 204, h = 112;
  M5.Display.fillRect(x + 31, y + 19, w - 42, h - 38, C_CARD);
  int top = y + 22;
  int bottom = y + h - 18;
  int gx = x + 32;
  int gw = w - 44;
  int targetY = map(vacTarget, 0, 100, bottom, top);
  M5.Display.drawFastHLine(gx, targetY, gw, C_WARN);
  for (int i = 1; i < gw && i < GRAPH_W; i++) {
    int y1 = map(vacGraph[GRAPH_W - gw + i - 1], 0, 100, bottom, top);
    int y2 = map(vacGraph[GRAPH_W - gw + i], 0, 100, bottom, top);
    M5.Display.drawLine(gx + i - 1, y1, gx + i, y2, C_ACCENT);
  }
}

void renderPpmGraph() {
  int x = 10, y = 54, w = 204, h = 112;
  M5.Display.fillRect(x + 10, y + 20, w - 20, h - 38, C_CARD);
  int mid = y + 66;
  M5.Display.drawFastHLine(x + 10, mid, w - 20, C_LINE);
  for (int i = 1; i < w - 22 && i < GRAPH_W; i++) {
    int v1 = ppmGraph[GRAPH_W - (w - 22) + i - 1];
    int v2 = ppmGraph[GRAPH_W - (w - 22) + i];
    int y1 = mid - v1;
    int y2 = mid - v2;
    M5.Display.drawLine(x + 10 + i - 1, y1, x + 10 + i, y2, C_ACCENT2);
  }
}

void drawVacScreen() {
  goScreen(SCREEN_VAC);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 42);
  M5.Display.print("0% is OFF. Actual vacuum ramps slowly.");
  drawGraphFrame(10, 54, 204, 112, "Actual ramp to setpoint", true);
  renderVacGraph();
  drawValueBox(224, 54, 86, 56, "VAC", vacTarget, "%", C_ACCENT);
  drawButton(224, 120, 86, 50, "+", C_ACCENT, TFT_BLACK, 4);
  drawButton(224, 180, 86, 50, "-", C_CARD2, C_TEXT, 4);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 176);
  M5.Display.print("Yellow line = setting");
  M5.Display.setCursor(14, 192);
  M5.Display.print("Green line = system reaction");
}

void drawPpmScreen() {
  goScreen(SCREEN_PPM);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 42);
  M5.Display.print("Each pulse cycle fills one minute timing.");
  drawGraphFrame(10, 54, 204, 112, "Frequency follows PPM", false);
  renderPpmGraph();
  drawValueBox(224, 54, 86, 56, "PPM", ppmTarget, "", C_ACCENT2);
  drawButton(224, 120, 86, 50, "+", C_ACCENT2, TFT_WHITE, 4);
  drawButton(224, 180, 86, 50, "-", C_CARD2, C_TEXT, 4);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(14, 176);
  if (ppmTarget == 0) {
    M5.Display.print("0 PPM = OFF");
  } else {
    float halfCycle = 30.0 / (float)ppmTarget;
    M5.Display.print("Up/down seconds: ");
    M5.Display.print(halfCycle, 1);
  }
}

void drawBarOnly(int x, int y, int w, int h, int value, int maxValue, uint16_t color) {
  value = clampInt(value, 0, maxValue);
  int barW = map(value, 0, maxValue, 0, w - 20);
  M5.Display.fillRoundRect(x + 10, y + 24, w - 20, h - 32, 5, C_CARD2);
  M5.Display.fillRoundRect(x + 10, y + 24, barW, h - 32, 5, color);
  M5.Display.fillRect(x + 12, y + h - 24, 70, 18, C_CARD);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_CARD);
  M5.Display.setCursor(x + 12, y + h - 22);
  M5.Display.print(value);
}

void drawBarFrame(int x, int y, int w, int h, int value, int maxValue, uint16_t color, const char* label) {
  M5.Display.fillRoundRect(x, y, w, h, 8, C_CARD);
  M5.Display.drawRoundRect(x, y, w, h, 8, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(x + 8, y + 6);
  M5.Display.print(label);
  drawBarOnly(x, y, w, h, value, maxValue, color);
}

void drawLiveScreen() {
  goScreen(SCREEN_LIVE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(16, 44);
  M5.Display.print(running ? "System running - simulated" : "System idle - simulated");
  drawBarFrame(16, 64, 288, 62, ppmTarget, 60, C_ACCENT2, "PPM - Squeezes / pumps per minute");
  drawBarFrame(16, 140, 288, 62, vacActual, 100, C_ACCENT, "VAC - Actual simulated vacuum %");
  drawButton(16, 210, 88, 24, running ? "STOP" : "START", running ? C_DANGER : C_OK, TFT_WHITE, 1);
  drawButton(116, 210, 88, 24, "VAC", C_CARD2, C_TEXT, 1);
  drawButton(216, 210, 88, 24, "PPM", C_CARD2, C_TEXT, 1);
}

void updateLiveBars() {
  if (currentScreen != SCREEN_LIVE) return;
  drawBarOnly(16, 64, 288, 62, ppmTarget, 60, C_ACCENT2);
  drawBarOnly(16, 140, 288, 62, vacActual, 100, C_ACCENT);
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
  M5.Display.print("User settings saved to memory");
  drawButton(16, 62, 136, 46, darkTheme ? "DARK" : "LIGHT", C_CARD2, C_TEXT, 2);
  drawButton(168, 62, 136, 46, "TOGGLE", C_ACCENT, TFT_BLACK, 2);

  M5.Display.fillRoundRect(16, 126, 288, 74, 10, C_CARD);
  M5.Display.drawRoundRect(16, 126, 288, 74, 10, C_LINE);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_CARD);
  M5.Display.setCursor(28, 136);
  M5.Display.print("Backlight: ");
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
  drawButton(16, 206, 288, 26, "SAVE SETTINGS", C_OK, TFT_BLACK, 1);
}

void drawWifiScreen() {
  goScreen(SCREEN_WIFI);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_ACCENT, C_BG);
  M5.Display.setCursor(24, 58);
  M5.Display.print("Wi-Fi: Pluto9000");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(24, 92);
  M5.Display.print("Connect phone/laptop to Pluto9000");
  M5.Display.setCursor(24, 112);
  M5.Display.print("Then open:");
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_WARN, C_BG);
  M5.Display.setCursor(24, 136);
  M5.Display.print(WiFi.softAPIP().toString());
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(24, 172);
  M5.Display.print("The webpage saves settings to memory");
  M5.Display.setCursor(24, 188);
  M5.Display.print(sdReady ? "SD backup: READY" : "SD backup: not detected, NVS still saves");
}

void refreshCurrentScreenFull() {
  if (currentScreen == SCREEN_HOME) drawHome();
  else if (currentScreen == SCREEN_VAC) drawVacScreen();
  else if (currentScreen == SCREEN_PPM) drawPpmScreen();
  else if (currentScreen == SCREEN_LIVE) drawLiveScreen();
  else if (currentScreen == SCREEN_DIAG) drawDiagScreen();
  else if (currentScreen == SCREEN_SETTINGS) drawSettingsScreen();
  else if (currentScreen == SCREEN_WIFI) drawWifiScreen();
}

void updateValueAreaOnly() {
  if (currentScreen == SCREEN_VAC) {
    drawValueBox(224, 54, 86, 56, "VAC", vacTarget, "%", C_ACCENT);
    drawButton(224, 120, 86, 50, "+", C_ACCENT, TFT_BLACK, 4);
    drawButton(224, 180, 86, 50, "-", C_CARD2, C_TEXT, 4);
  } else if (currentScreen == SCREEN_PPM) {
    drawValueBox(224, 54, 86, 56, "PPM", ppmTarget, "", C_ACCENT2);
    drawButton(224, 120, 86, 50, "+", C_ACCENT2, TFT_WHITE, 4);
    drawButton(224, 180, 86, 50, "-", C_CARD2, C_TEXT, 4);
    M5.Display.fillRect(12, 174, 190, 20, C_BG);
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(C_MUTED, C_BG);
    M5.Display.setCursor(14, 176);
    if (ppmTarget == 0) {
      M5.Display.print("0 PPM = OFF");
    } else {
      float halfCycle = 30.0 / (float)ppmTarget;
      M5.Display.print("Up/down seconds: ");
      M5.Display.print(halfCycle, 1);
    }
  } else if (currentScreen == SCREEN_SETTINGS) {
    drawSettingsScreen();
  }
}

// ---------- Value adjustment ----------
void changeVac(int dir, bool fast) {
  int step = fast ? 10 : 2;
  vacTarget = clampInt(vacTarget + dir * step, 0, 100);
  updateValueAreaOnly();
}

void changePpm(int dir, bool fast) {
  int step = fast ? 10 : 1;
  ppmTarget = clampInt(ppmTarget + dir * step, 0, 60);
  updateValueAreaOnly();
}

void changeBacklight(int dir) {
  backlightStep = clampInt(backlightStep + dir, 1, 10);
  applyBacklight();
  updateValueAreaOnly();
}

void doHeldAction(bool fast) {
  if (heldAction == ACT_VAC_MINUS) changeVac(-1, fast);
  else if (heldAction == ACT_VAC_PLUS) changeVac(1, fast);
  else if (heldAction == ACT_PPM_MINUS) changePpm(-1, fast);
  else if (heldAction == ACT_PPM_PLUS) changePpm(1, fast);
  else if (heldAction == ACT_BACKLIGHT_MINUS) changeBacklight(-1);
  else if (heldAction == ACT_BACKLIGHT_PLUS) changeBacklight(1);
}

// ---------- Touch handling ----------
void commonTopTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26)) { drawHome(); return; }
  if (inRect(x, y, 258, 8, 54, 26)) { requestEStopConfirm(); return; }
}

void handleHomeTouch(int x, int y) {
  if (inRect(x, y, 258, 8, 54, 26)) { requestEStopConfirm(); return; }
  if (inRect(x, y, 12, 134, 142, 46)) { running = !running; drawHome(); return; }
  if (inRect(x, y, 166, 134, 142, 46)) { drawLiveScreen(); return; }
  if (inRect(x, y, 12, 188, 68, 38)) { drawVacScreen(); return; }
  if (inRect(x, y, 88, 188, 68, 38)) { drawPpmScreen(); return; }
  if (inRect(x, y, 164, 188, 68, 38)) { drawWifiScreen(); return; }
  if (inRect(x, y, 240, 188, 68, 38)) { drawMoreScreen(); return; }
}

void handleVacTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 224, 120, 86, 50)) { heldAction = ACT_VAC_PLUS; changeVac(1, false); return; }
  if (inRect(x, y, 224, 180, 86, 50)) { heldAction = ACT_VAC_MINUS; changeVac(-1, false); return; }
}

void handlePpmTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 224, 120, 86, 50)) { heldAction = ACT_PPM_PLUS; changePpm(1, false); return; }
  if (inRect(x, y, 224, 180, 86, 50)) { heldAction = ACT_PPM_MINUS; changePpm(-1, false); return; }
}

void handleLiveTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 16, 210, 88, 24)) { running = !running; drawLiveScreen(); return; }
  if (inRect(x, y, 116, 210, 88, 24)) { drawVacScreen(); return; }
  if (inRect(x, y, 216, 210, 88, 24)) { drawPpmScreen(); return; }
}

void handleMoreTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 14, 66, 292, 48)) { drawDiagScreen(); return; }
  if (inRect(x, y, 14, 126, 292, 48)) { drawSettingsScreen(); return; }
  if (inRect(x, y, 14, 186, 292, 38)) { drawWifiScreen(); return; }
}

void handleDiagTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
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
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
  if (inRect(x, y, 168, 62, 136, 46) || inRect(x, y, 16, 62, 136, 46)) {
    darkTheme = !darkTheme;
    initPalette();
    drawSettingsScreen();
    return;
  }
  if (inRect(x, y, 244, 143, 26, 44)) { heldAction = ACT_BACKLIGHT_MINUS; changeBacklight(-1); return; }
  if (inRect(x, y, 276, 143, 26, 44)) { heldAction = ACT_BACKLIGHT_PLUS; changeBacklight(1); return; }
  if (inRect(x, y, 16, 206, 288, 26)) { saveConfig(); drawSettingsScreen(); return; }
}

void handleWifiTouch(int x, int y) {
  if (inRect(x, y, 8, 8, 36, 26) || inRect(x, y, 258, 8, 54, 26)) { commonTopTouch(x, y); return; }
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
    else if (currentScreen == SCREEN_WIFI) handleWifiTouch(x, y);
  }

  if (t.wasReleased()) {
    touchHeld = false;
    if (heldAction == ACT_VAC_MINUS || heldAction == ACT_VAC_PLUS || heldAction == ACT_PPM_MINUS || heldAction == ACT_PPM_PLUS || heldAction == ACT_BACKLIGHT_MINUS || heldAction == ACT_BACKLIGHT_PLUS) {
      saveConfig();
    }
    heldAction = ACT_NONE;
  }

  if (touchHeld && heldAction != ACT_NONE && t.isPressed()) {
    unsigned long now = millis();
    bool fast = (now - touchStartMs) > 3000;
    unsigned long interval = fast ? 170 : 360;
    if (now - lastTouchRepeat >= interval) {
      lastTouchRepeat = now;
      doHeldAction(fast);
    }
  }
}

// ---------- Simulation updates ----------
void updateVacActual() {
  unsigned long now = millis();
  if (now - lastVacRampTick < 140) return;
  lastVacRampTick = now;
  // Vacuum does not catch up instantly. It creeps toward target.
  if (vacActual < vacTarget) vacActual++;
  else if (vacActual > vacTarget) vacActual--;
}

int pulseSampleForNow() {
  if (ppmTarget <= 0) return 0;
  unsigned long periodMs = 60000UL / (unsigned long)ppmTarget;
  unsigned long halfMs = periodMs / 2UL;
  if (halfMs < 1) halfMs = 1;
  unsigned long p = millis() % periodMs;
  int amp = 30;
  if (p < halfMs) {
    return map((int)p, 0, (int)halfMs, 0, amp);
  } else {
    return map((int)(p - halfMs), 0, (int)halfMs, amp, 0);
  }
}

void updateGraphs() {
  unsigned long now = millis();
  if (now - lastGraphTick < 90) return;
  lastGraphTick = now;
  updateVacActual();

  shiftGraph(vacGraph, GRAPH_W, vacActual);
  shiftGraph(ppmGraph, GRAPH_W, pulseSampleForNow());

  if (currentScreen == SCREEN_VAC) renderVacGraph();
  else if (currentScreen == SCREEN_PPM) renderPpmGraph();
  else if (currentScreen == SCREEN_LIVE && now - lastLiveTick > 260) {
    lastLiveTick = now;
    updateLiveBars();
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  sw = M5.Display.width();
  sh = M5.Display.height();
  Serial.begin(115200);

  loadConfig();
  initPalette();
  applyBacklight();
  for (int i = 0; i < GRAPH_W; i++) { vacGraph[i] = 0; ppmGraph[i] = 0; }

  sdReady = SD.begin();
  startWebPortal();

  bootAnimation();
  drawHome();
  Serial.println("11_Pulse_CoreS3_Demo booted. Simulation only.");
  Serial.print("AP: "); Serial.println(AP_SSID);
  Serial.print("Web: http://"); Serial.println(WiFi.softAPIP());
}

void loop() {
  if (webRunning) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  handleTouch();
  updateGraphs();
}
