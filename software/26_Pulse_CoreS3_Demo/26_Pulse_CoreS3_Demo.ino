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

// =====================================================
//  PulseCore / Pluto 9000 - CoreS3/SE Demo Iteration 26
//  Moving closer to real hardware test mode.
// =====================================================

// ------------------ GLOBAL APP STATE ------------------
Preferences prefs;
int webPort = 80;
WebServer* serverPtr = nullptr;
#define server (*serverPtr)
DNSServer dnsServer;
M5Canvas bootSprite(&M5.Display);

const char* AP_NAME = "Pluto9000";
const char* AP_PASS = "";            // Open ad-hoc AP for current demo build
const byte DNS_PORT = 53;
const char* APP_VERSION = "v26";

// CoreS3 microSD wiring
const int SD_CS_PIN   = 4;
const int SD_SCK_PIN  = 36;
const int SD_MISO_PIN = 35;
const int SD_MOSI_PIN = 37;

// Real output pins for bench testing from the Diag screen
const int PIN_SOL_VAC     = 5;
const int PIN_SOL_PULSE   = 6;
const int PIN_SOL_RELEASE = 7;
const int PIN_SOL_PUMP    = 18;

const int PPM_MIN = 40;
const int PPM_MAX = 300;
const int PPM_DEFAULT = 60;

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
bool mobileMode = true;
bool simpleHome = false;
bool showAdvancedWeb = true;
int webRefreshSec = 1;
String staSsid = "";
String staPass = "";

bool showVac = true;
bool showPpm = true;
bool showLive = true;
bool showDiag = true;
bool showSettings = true;
bool showWifi = true;

int vacTarget = 45;           // 0-100%
float vacActual = 0.0f;       // simulated slow vacuum response
int ppmTarget = PPM_DEFAULT;  // locked 40-300 PPM
int backlightStep = 7;        // 1-10
int vacRampSpeed = 5;         // 1-10; lower = slower reaction
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

bool solVacOn = false;
bool solPulseOn = false;
bool solReleaseOn = false;
bool solPumpOn = false;
bool pulseDemoMode = false;
bool pulseDemoShotActive = false;
unsigned long lastPulseDemoMs = 0;
unsigned long pulseDemoShotStartMs = 0;
const unsigned long PULSE_DEMO_PERIOD_MS = 3000;
const unsigned long PULSE_DEMO_ON_MS = 450;

const int LIVE_POINTS = 96;
int liveVacHist[LIVE_POINTS];
int livePpmHist[LIVE_POINTS];
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
void handleWifiScan();
void handleWifiJoin();
void handleLogList();
void handleLogData();
void stopWebServerNow();
void writePeriodicSdSample();
void addLivePoint();
void updateLiveGraphs();
void startWebServer();
String htmlPage();
void markSettingsDirty();
void saveSettingsNow();
void logSdEvent(const char* eventName);
void ensureStatsHtml();
bool sdLoggingReady();
void beginSessionLog(bool resetFiles);
float ppmWaveValue(unsigned long ms);
void configureOutputPins();
void applyHardwareOutputs();
void allOutputsOff();
String wifiStatusLabel();
String wifiIpLabel();
String shareLink();
void drawThemeModeButton(int x, int y, int w, int h);
void drawMilkySideToSprite(M5Canvas& s, int cx, int cy, int spurtLen);
void updatePulseDemo();
void logButtonPress(const char* label);
void logSystemProblem(const char* problem);
String entryTimeString();
String dailyLogPath();

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

void configureOutputPins() {
  pinMode(PIN_SOL_VAC, OUTPUT);
  pinMode(PIN_SOL_PULSE, OUTPUT);
  pinMode(PIN_SOL_RELEASE, OUTPUT);
  pinMode(PIN_SOL_PUMP, OUTPUT);
  allOutputsOff();
}

void applyHardwareOutputs() {
  digitalWrite(PIN_SOL_VAC, solVacOn ? HIGH : LOW);
  digitalWrite(PIN_SOL_PULSE, solPulseOn ? HIGH : LOW);
  digitalWrite(PIN_SOL_RELEASE, solReleaseOn ? HIGH : LOW);
  digitalWrite(PIN_SOL_PUMP, solPumpOn ? HIGH : LOW);
}

void allOutputsOff() {
  solVacOn = false;
  solPulseOn = false;
  solReleaseOn = false;
  solPumpOn = false;
  pulseDemoMode = false;
  pulseDemoShotActive = false;
  digitalWrite(PIN_SOL_VAC, LOW);
  digitalWrite(PIN_SOL_PULSE, LOW);
  digitalWrite(PIN_SOL_RELEASE, LOW);
  digitalWrite(PIN_SOL_PUMP, LOW);
}

String wifiStatusLabel() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.SSID();
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_AP_STA) return String(AP_NAME);
  return String("Not connected");
}

String wifiIpLabel() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  wifi_mode_t mode = WiFi.getMode();
  if ((mode == WIFI_AP || mode == WIFI_AP_STA) && wifiActive) return WiFi.softAPIP().toString();
  return String("0.0.0.0");
}

String shareLink() {
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
  // Daily log filename. Fallback uses compile date until RTC/WiFi time is added.
  char mon[4] = {0};
  int day = 1;
  int year = 2026;
  sscanf(__DATE__, "%3s %d %d", mon, &day, &year);
  String stem = twoDigits(monthFromCompile(mon));
  stem += "-"; stem += twoDigits(day);
  stem += "-"; stem += String(year);
  return stem;
}

String entryTimeString() {
  // HH-MM-SS entry time. For now uptime-derived so it advances while offline.
  unsigned long total = (millis() - bootMs) / 1000;
  unsigned long hh = (total / 3600) % 24;
  unsigned long mm = (total % 3600) / 60;
  unsigned long ss = total % 60;
  return twoDigits(hh) + "-" + twoDigits(mm) + "-" + twoDigits(ss);
}

String dailyLogPath() {
  return "/logs/" + timestampFileStem() + ".log";
}

bool nameLooksLikeLog(String n) {
  n.replace("/logs/", "");
  n.replace("/", "");
  if (!n.endsWith(".log")) return false;
  // Daily format: MM-DD-YYYY.log
  return n.length() == 14 && n.charAt(2) == '-' && n.charAt(5) == '-';
}

void ensureStatsHtml() {
  if (!sdReady) return;
  SD.remove("/stats.html");
  File f = SD.open("/stats.html", FILE_WRITE);
  if (!f) return;

  f.println(F("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"));
  f.println(F("<title>Pluto 9000 Stats</title><style>"));
  f.println(F(":root{font-family:Arial,Helvetica,sans-serif;background:#0d1320;color:#eef5ff}body{margin:0;background:linear-gradient(135deg,#0d1320,#172033 70%,#0b0f18)}.wrap{max-width:1120px;margin:auto;padding:16px}.hero,.card{background:#182235;border:1px solid #2a3a53;border-radius:22px;padding:18px;margin-bottom:14px;box-shadow:0 14px 35px #0006}.hero{display:flex;gap:18px;align-items:center}.brand{font-size:36px;font-weight:900}.sub{color:#aebed3;font-size:16px;line-height:1.45}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:12px}.big{font-size:34px;font-weight:900}.label{color:#aebed3;text-transform:uppercase;font-size:12px;letter-spacing:.08em}.note{color:#aebed3;font-size:14px;line-height:1.45}.milky{width:92px;height:130px;position:relative;flex:0 0 auto}.bottle{position:absolute;left:20px;top:25px;width:52px;height:84px;background:#fff;border-radius:14px;border:3px solid #b8d2ea}.neck{position:absolute;left:31px;top:5px;width:30px;height:28px;background:#f3fbff;border-radius:8px;border:3px solid #b8d2ea}.cap{position:absolute;left:29px;top:0;width:34px;height:9px;background:#409be6;border-radius:4px}.face{position:absolute;left:31px;top:48px;font-size:20px;color:#05080c}.tag{position:absolute;left:26px;top:82px;background:#2d8cd2;color:#fff;font-weight:900;border-radius:7px;padding:3px 7px;font-size:12px}.puddle{position:absolute;left:8px;bottom:4px;width:76px;height:14px;background:#fff;border-radius:50%;opacity:.85}.drop{position:absolute;left:44px;top:110px;width:9px;height:16px;background:#fff;border-radius:50% 50% 60% 60%}.mascot2{transform:scaleX(-1)}button,select,input{font-size:16px;border-radius:12px;border:1px solid #34465f;padding:12px;background:#22314a;color:#eef5ff}canvas{width:100%;height:230px;background:#0f1724;border:1px solid #30445f;border-radius:16px}table{width:100%;border-collapse:collapse;background:#182235;border-radius:16px;overflow:hidden}th,td{padding:9px;border-bottom:1px solid #2d405b;text-align:left;font-size:13px}th{background:#22314a}.pill{display:inline-block;border-radius:999px;background:#22314a;color:#aebed3;padding:8px 11px;margin:3px}@media(max-width:650px){.wrap{padding:10px}.hero{display:block}.milky{margin:auto}.brand{font-size:29px}.big{font-size:28px}th,td{font-size:11px;padding:7px}.hideMob{display:none}canvas{height:180px}}"));
  f.println(F("</style></head><body><div class='wrap'><div class='hero'><div class='milky'><div class='neck'></div><div class='cap'></div><div class='bottle'></div><div class='face'>^^</div><div class='tag'>milky</div><div class='drop'></div><div class='puddle'></div></div><div><div class='brand'>Pluto 9000 Stats</div><p class='sub'>This report reads the daily log files stored in <b>/logs</b> on the SD card. The daily logs are the source of truth; this page turns the useful parts into readable stats.</p></div><div class='milky mascot2'><div class='neck'></div><div class='cap'></div><div class='bottle'></div><div class='face'>--</div><div class='tag'>milky</div><div class='drop'></div><div class='puddle'></div></div></div>"));
  f.println(F("<div class='card'><div class='label'>Load log</div><select id='logSelect'></select> <button onclick='loadSelected()'>Load</button><p class='note'>Open from Pluto web control at <b>192.168.69.69/stats.html</b>. If opened directly from the SD card, use the file picker.</p><input type='file' id='filePick' accept='.log,.csv'></div>"));
  f.println(F("<div class='grid'><div class='card'><div class='label'>Samples</div><div class='big' id='sampleCount'>0</div></div><div class='card'><div class='label'>Run time</div><div class='big' id='runTime'>00:00:00</div></div><div class='card'><div class='label'>Avg VAC</div><div class='big' id='avgVac'>0%</div></div><div class='card'><div class='label'>Avg PPM</div><div class='big' id='avgPpm'>0</div></div><div class='card'><div class='label'>Max VAC</div><div class='big' id='maxVac'>0%</div></div><div class='card'><div class='label'>Events</div><div class='big' id='eventCount'>0</div></div></div>"));
  f.println(F("<div class='card'><span class='pill'>Starts: <b id='starts'>0</b></span><span class='pill'>Stops: <b id='stops'>0</b></span><span class='pill'>Settings changes: <b id='changes'>0</b></span><span class='pill'>Problems: <b id='problems'>0</b></span><span class='pill'>E-stop/release: <b id='estops'>0</b></span></div>"));
  f.println(F("<div class='card'><div class='label'>VAC, PPM, and pulse waveform</div><canvas id='chart'></canvas><p class='note'>Green = VAC actual. Blue = PPM scaled to the chart. Yellow = pulse waveform/frequency.</p></div><div class='card'><table><thead><tr><th>Time</th><th>Event</th><th>VAC target</th><th>VAC actual</th><th>PPM</th><th class='hideMob'>Pulse</th><th class='hideMob'>Running</th></tr></thead><tbody id='rows'></tbody></table></div>"));
  f.println(F("<script>let rows=[];function parseCsv(t){let a=t.trim().split(/\\r?\\n/).filter(Boolean);if(!a.length)return[];let h=a.shift().split(',');return a.map(line=>{let p=line.split(',');let o={};h.forEach((k,i)=>o[k]=p[i]);return o})}function hms(sec){sec=Math.max(0,Math.round(sec));let h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60),s=sec%60;return String(h).padStart(2,'0')+':'+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0')}function draw(){let c=document.getElementById('chart'),r=c.getBoundingClientRect(),d=window.devicePixelRatio||1;c.width=r.width*d;c.height=r.height*d;let ctx=c.getContext('2d');ctx.scale(d,d);ctx.clearRect(0,0,r.width,r.height);ctx.strokeStyle='#2d405b';for(let i=1;i<4;i++){let y=r.height*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(r.width,y);ctx.stroke()}if(!rows.length)return;function line(key,max,color){ctx.strokeStyle=color;ctx.lineWidth=3;ctx.beginPath();rows.forEach((o,i)=>{let x=i/(rows.length-1||1)*r.width;let y=r.height-10-(Number(o[key]||0)/max)*(r.height-22);if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.stroke()}line('vacActual',100,'#36d399');line('ppm',300,'#3aa8ff');line('pulseWave',100,'#ffbc4b')}function render(t){rows=parseCsv(t);let samples=rows.filter(o=>o.event&&o.event.includes('running sample'));let av=0,ap=0,maxv=0,ev=0,starts=0,stops=0,chg=0,prob=0,est=0;rows.forEach(o=>{if(o.event){ev++;let e=o.event.toLowerCase();if(e.includes('run start'))starts++;if(e.includes('run stop'))stops++;if(e.includes('change')||e.includes('button'))chg++;if(e.includes('problem')||e.includes('error'))prob++;if(e.includes('release')||e.includes('estop'))est++;}if(o.running==='1'||(o.event||'').includes('running sample')){av+=Number(o.vacActual||0);ap+=Number(o.ppm||0);maxv=Math.max(maxv,Number(o.vacActual||0));}});let n=samples.length||rows.filter(o=>o.running==='1').length||1;document.getElementById('sampleCount').textContent=rows.length;document.getElementById('runTime').textContent=hms(samples.length*2.5);document.getElementById('avgVac').textContent=Math.round(av/n)+'%';document.getElementById('avgPpm').textContent=Math.round(ap/n);document.getElementById('maxVac').textContent=Math.round(maxv)+'%';document.getElementById('eventCount').textContent=ev;document.getElementById('starts').textContent=starts;document.getElementById('stops').textContent=stops;document.getElementById('changes').textContent=chg;document.getElementById('problems').textContent=prob;document.getElementById('estops').textContent=est;document.getElementById('rows').innerHTML=rows.slice(-100).reverse().map(o=>`<tr><td>${o.time||o.uptime||''}</td><td>${o.event||''}</td><td>${o.vacTarget||''}%</td><td>${o.vacActual||''}%</td><td>${o.ppm||''}</td><td class='hideMob'>${o.pulseWave||''}</td><td class='hideMob'>${o.running==='1'?'yes':'no'}</td></tr>`).join('');draw()}async function init(){try{let list=await fetch('/api/loglist').then(r=>r.json());let sel=document.getElementById('logSelect');sel.innerHTML=list.files.map(f=>`<option>${f}</option>`).join('');if(list.files[0])loadSelected()}catch(e){document.getElementById('logSelect').innerHTML='<option>Open via device web page</option>'}}async function loadSelected(){let f=document.getElementById('logSelect').value;if(!f)return;let t=await fetch('/api/logdata?file='+encodeURIComponent(f)).then(r=>r.text());render(t)}document.getElementById('filePick').onchange=e=>{let file=e.target.files[0];if(file){let fr=new FileReader();fr.onload=()=>render(fr.result);fr.readAsText(file)}};window.onresize=draw;init();</script></div></body></html>"));
  f.close();
}

void beginSessionLog(bool resetFiles) {
  if (!sdReady) return;
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  ensureStatsHtml();
  sessionLogPath = dailyLogPath();
  if (resetFiles && SD.exists(sessionLogPath)) SD.remove(sessionLogPath);

  const char* header = "ms,time,uptime,event,vacTarget,vacActual,ppm,pulseWave,running,theme,backlightStep,heap,solPump,solPulse,solVac,solRelease";

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
  f.print(running ? 1 : 0); f.print(',');
  f.print(darkTheme ? "dark" : "light"); f.print(',');
  f.print(backlightStep); f.print(',');
  f.print(ESP.getFreeHeap()); f.print(',');
  f.print(solPumpOn ? 1 : 0); f.print(',');
  f.print(solPulseOn ? 1 : 0); f.print(',');
  f.print(solVacOn ? 1 : 0); f.print(',');
  f.println(solReleaseOn ? 1 : 0);
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
    if (!SD.exists("/logs")) SD.mkdir("/logs");
    File root = SD.open("/logs");
    File file = root.openNextFile();
    while (file) {
      String n = String(file.name());
      if (!file.isDirectory() && nameLooksLikeLog(n)) {
        n.replace("/logs/", "");
        n.replace("/", "");
        if (!first) j += ",";
        j += "\"" + n + "\"";
        first = false;
      }
      file = root.openNextFile();
    }
    root.close();
  }
  j += "]}";
  return j;
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
  prefs.putBool("mobile", mobileMode);
  prefs.putBool("simple", simpleHome);
  prefs.putBool("advweb", showAdvancedWeb);
  prefs.putInt("webref", webRefreshSec);
  prefs.putInt("webPort", webPort);
  prefs.putString("staSsid", staSsid);
  prefs.putString("staPass", staPass);
  prefs.putBool("shVac", showVac);
  prefs.putBool("shPpm", showPpm);
  prefs.putBool("shLive", showLive);
  prefs.putBool("shDiag", showDiag);
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
      f.print("graphSpeed="); f.println(graphSpeed);
      f.print("releaseMs="); f.println(releaseMs);
      f.print("largeButtons="); f.println(largeButtons ? 1 : 0);
      f.print("webControlEnabled="); f.println(webControlEnabled ? 1 : 0);
      f.print("mobileMode="); f.println(mobileMode ? 1 : 0);
      f.print("simpleHome="); f.println(simpleHome ? 1 : 0);
      f.print("showAdvancedWeb="); f.println(showAdvancedWeb ? 1 : 0);
      f.print("webRefreshSec="); f.println(webRefreshSec);
      f.print("webPort="); f.println(webPort);
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
  vacTarget = prefs.getInt("vac", 45);
  ppmTarget = prefs.getInt("ppm", PPM_DEFAULT);
  darkTheme = prefs.getBool("dark", true);
  backlightStep = prefs.getInt("bl", 7);
  vacRampSpeed = prefs.getInt("ramp", 5);
  graphSpeed = prefs.getInt("gspd", 4);
  releaseMs = prefs.getInt("rel", 1200);
  largeButtons = prefs.getBool("large", true);
  webControlEnabled = prefs.getBool("web", true);
  mobileMode = prefs.getBool("mobile", true);
  simpleHome = prefs.getBool("simple", false);
  showAdvancedWeb = prefs.getBool("advweb", true);
  webRefreshSec = prefs.getInt("webref", 1);
  webPort = prefs.getInt("webPort", 80);
  staSsid = prefs.getString("staSsid", "");
  staPass = prefs.getString("staPass", "");
  showVac = prefs.getBool("shVac", true);
  showPpm = prefs.getBool("shPpm", true);
  showLive = prefs.getBool("shLive", true);
  showDiag = prefs.getBool("shDiag", true);
  showSettings = prefs.getBool("shSet", true);
  showWifi = prefs.getBool("shWifi", true);
  screenScale = prefs.getInt("scale", 2);
  prefs.end();

  vacTarget = clampInt(vacTarget, 0, 100);
  ppmTarget = clampInt(ppmTarget, PPM_MIN, PPM_MAX);
  backlightStep = clampInt(backlightStep, 1, 10);
  vacRampSpeed = clampInt(vacRampSpeed, 1, 10);
  graphSpeed = clampInt(graphSpeed, 1, 10);
  webRefreshSec = clampInt(webRefreshSec, 1, 5);
  webPort = clampInt(webPort, 80, 65535);
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
  s.fillRoundRect(cx - 20, cy + 3, 45, 23, 7, blue);
  s.setTextSize(2);
  s.setTextColor(TFT_WHITE, blue);
  int labelW = s.textWidth("milky");
  s.setCursor(cx - labelW / 2 + 2, cy + 7);
  s.print("milky");
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
  int x = 12, y = 82, w = 296, h = 92;
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
  // Yellow pulse waveform overlay, so every line graph shows pulse behavior.
  float msPerPixel = 4000.0f / (float)gw;
  lastX = gx;
  lastY = gy + gh;
  unsigned long now = millis();
  for (int px = 0; px < gw; px++) {
    unsigned long sampleT = now - (unsigned long)((gw - px) * msPerPixel);
    float pv = ppmWaveValue(sampleT);
    int yy = gy + gh - (int)(pv * (gh - 8));
    if (px > 0) M5.Display.drawLine(lastX, lastY, gx + px, yy, C_LINE);
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

  // PPM is true frequency: 60 PPM = 1 pulse/second, 120 PPM = 2 pulses/second.
  // graphSpeed now means how many seconds of history are visible across the graph.
  float msPerPixel = ((float)graphSpeed * 1000.0f) / (float)gw;
  int lastX = gx;
  int lastY = gy + gh;
  unsigned long now = millis();
  for (int px = 0; px < gw; px++) {
    unsigned long sampleT = now - (unsigned long)((gw - px) * msPerPixel);
    float v = ppmWaveValue(sampleT);
    int yy = gy + gh - (int)(v * (gh - 7));
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

void addLivePoint() {
  liveVacHist[liveHistIndex] = clampInt((int)vacActual, 0, 100);
  livePpmHist[liveHistIndex] = clampInt(ppmTarget, PPM_MIN, PPM_MAX);
  liveHistIndex++;
  if (liveHistIndex >= LIVE_POINTS) liveHistIndex = 0;
}

void drawLiveGraphFrame() {
  M5.Display.fillRoundRect(8, 42, 304, 160, 8, C_PANEL);
  M5.Display.drawRoundRect(8, 42, 304, 160, 8, C_GRID);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_PANEL);
  M5.Display.setCursor(16, 49);
  M5.Display.print("Live lines: VAC + PPM + pulse wave");
  M5.Display.setCursor(18, 67);
  M5.Display.print("VAC 0-100%");
  M5.Display.setCursor(210, 67);
  M5.Display.print("PPM 40-300");
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
  int lastPpmY = y + h - map(livePpmHist[liveHistIndex], PPM_MIN, PPM_MAX, 0, h);
  for (int i = 1; i < LIVE_POINTS; i++) {
    int idx = liveHistIndex + i;
    if (idx >= LIVE_POINTS) idx -= LIVE_POINTS;
    int px = x + map(i, 0, LIVE_POINTS - 1, 0, w);
    int vy = y + h - map(liveVacHist[idx], 0, 100, 0, h);
    int py = y + h - map(livePpmHist[idx], PPM_MIN, PPM_MAX, 0, h);
    M5.Display.drawLine(lastVacX, lastVacY, px, vy, C_ACCENT2);
    M5.Display.drawLine(lastPpmX, lastPpmY, px, py, C_ACCENT);
    lastVacX = px; lastVacY = vy;
    lastPpmX = px; lastPpmY = py;
  }
  int lastPulseX = x;
  int lastPulseY = y + h;
  unsigned long now = millis();
  float msPerPixel = 6000.0f / (float)w;
  for (int pxo = 0; pxo < w; pxo++) {
    unsigned long sampleT = now - (unsigned long)((w - pxo) * msPerPixel);
    float pv = ppmWaveValue(sampleT);
    int yy = y + h - (int)(pv * (h - 10));
    if (pxo > 0) M5.Display.drawLine(lastPulseX, lastPulseY, x + pxo, yy, C_LINE);
    lastPulseX = x + pxo;
    lastPulseY = yy;
  }
  M5.Display.fillRoundRect(12, 205, 72, 31, 7, C_PANEL2);
  drawCenteredText("BACK", 12, 205, 72, 31, 2, C_TEXT, C_PANEL2);
  M5.Display.fillRoundRect(220, 205, 88, 31, 7, running ? C_WARN : C_GOOD);
  drawCenteredText(running ? "STOP" : "START", 220, 205, 88, 31, 2, TFT_BLACK, running ? C_WARN : C_GOOD);
}

// ------------------ SCREEN DRAWING ------------------
void refreshVacValue() {
  M5.Display.fillRoundRect(162, 39, 144, 34, 8, C_PANEL2);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(C_TEXT, C_PANEL2);
  M5.Display.setCursor(174, 48);
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

  if (simpleHome) {
    if (showVac) drawButton(10, 72, 145, 64, "VAC", C_BUTTON2, C_ACCENT2, C_TEXT, 3);
    if (showPpm) drawButton(165, 72, 145, 64, "PPM", C_BUTTON, C_ACCENT, C_TEXT, 3);
    drawButton(10, 150, 145, 64, running ? "STOP" : "START", running ? C_WARN : C_GOOD, running ? C_WARN : C_GOOD, TFT_BLACK, 3);
    drawButton(165, 150, 145, 64, "MORE", C_PANEL2, C_GRID, C_TEXT, 3);
    return;
  }

  if (showVac) drawButton(10, 70, 145, 68, "VAC", C_BUTTON2, C_ACCENT2, C_TEXT, 3);
  if (showPpm) drawButton(165, 70, 145, 68, "PPM", C_BUTTON, C_ACCENT, C_TEXT, 3);
  drawButton(10, 148, 145, 68, running ? "STOP" : "START", running ? C_WARN : C_GOOD, running ? C_WARN : C_GOOD, TFT_BLACK, 3);
  drawButton(165, 148, 145, 68, "MORE", C_PANEL2, C_GRID, C_TEXT, 3);
}

void drawVacScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("VAC Control");
  refreshVacValue();
  drawButton(12, 38, 120, 38, "HOME", C_PANEL2, C_GRID, C_TEXT, 2);
  drawGraphFrame(12, 82, 296, 92, "VAC actual ramp 0-100%", true);
  updateVacGraph();
  drawButton(19, 178, 132, 53, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButton(169, 178, 132, 53, "+", C_BUTTON2, C_ACCENT2, C_TEXT, 5);
}

void drawPpmScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("PPM Control");
  refreshPpmValue();
  drawButton(201, 39, 105, 29, "HOME", C_PANEL2, C_GRID, C_TEXT, 2);
  drawGraphFrame(12, 72, 296, 98, "true pulse frequency 40-300 PPM", false);
  updatePpmGraph();
  drawButton(19, 178, 132, 53, "-", C_BUTTON, C_GRID, C_TEXT, 5);
  drawButton(169, 178, 132, 53, "+", C_BUTTON2, C_ACCENT, C_TEXT, 5);
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
  if (showDiag) drawButton(165, y1, 145, 54, "TEST", C_BUTTON, C_GRID, C_TEXT, 3);
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
  M5.Display.setCursor(12, 45);
  M5.Display.print("SSID:");
  M5.Display.setTextColor(C_ACCENT2, C_BG);
  M5.Display.setCursor(78, 45);
  M5.Display.print(wifiStatusLabel());
  M5.Display.setTextColor(C_TEXT, C_BG);
  M5.Display.setCursor(12, 78);
  M5.Display.print("IP:");
  M5.Display.setTextColor(C_ACCENT, C_BG);
  M5.Display.setCursor(52, 78);
  M5.Display.print(wifiIpLabel());
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 108);
  M5.Display.print("Port: ");
  M5.Display.print(webPort);
  drawButton(16, 138, 288, 48, wifiActive ? "TURN WEBSERVER OFF" : "TURN WEBSERVER ON", wifiActive ? C_WARN : C_GOOD, wifiActive ? C_WARN : C_GOOD, TFT_BLACK, 2);
  drawBackButton();
}

void drawDiagScreen() {
  M5.Display.fillScreen(C_BG);
  drawHeader("Manual Test");
  uint16_t pulseFill = C_BUTTON;
  uint16_t pulseText = C_TEXT;
  const char* pulseLabel = "PULSE";
  if (pulseDemoMode) {
    pulseLabel = solPulseOn ? "PULSE FIRE" : "PULSE WAIT";
    pulseFill = solPulseOn ? C_GOOD : C_PANEL2;
    pulseText = solPulseOn ? TFT_BLACK : C_TEXT;
  }
  drawButton(12, 48, 140, 52, solPumpOn ? "PUMP ON" : "PUMP", solPumpOn ? C_GOOD : C_BUTTON, solPumpOn ? C_GOOD : C_GRID, solPumpOn ? TFT_BLACK : C_TEXT, 2);
  drawButton(168, 48, 140, 52, pulseLabel, pulseFill, pulseFill, pulseText, 2);
  drawButton(12, 112, 140, 52, solVacOn ? "VAC ON" : "VAC", solVacOn ? C_GOOD : C_BUTTON2, solVacOn ? C_GOOD : C_GRID, solVacOn ? TFT_BLACK : C_TEXT, 2);
  drawButton(168, 112, 140, 52, solReleaseOn ? "RELEASE ON" : "RELEASE", solReleaseOn ? C_WARN : C_WARN, solReleaseOn ? C_WARN : C_WARN, TFT_BLACK, 2);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(C_MUTED, C_BG);
  M5.Display.setCursor(12, 176);
  M5.Display.print("Bench test outputs: G18 pump  G6 pulse  G5 vac  G7 release");
  drawBackButton();
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
  ppmTarget = clampInt(ppmTarget + delta, PPM_MIN, PPM_MAX);
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
  int step = 1;
  if (held >= TURBO_AFTER_MS) step = 10;
  else if (held >= FAST_AFTER_MS) step = 5;
  if (holdControl == 1) changeVac(-step);
  if (holdControl == 2) changeVac(step);
  if (holdControl == 3) changePpm(-step);
  if (holdControl == 4) changePpm(step);
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


void updatePulseDemo() {
  if (!pulseDemoMode) return;
  unsigned long now = millis();
  if (!pulseDemoShotActive && now - lastPulseDemoMs >= PULSE_DEMO_PERIOD_MS) {
    solPulseOn = true;
    pulseDemoShotActive = true;
    pulseDemoShotStartMs = now;
    lastPulseDemoMs = now;
    applyHardwareOutputs();
    logSdEvent("pulse demo fire");
    if (currentScreen == SCREEN_DIAG) uiNeedsFullRedraw = true;
  }
  if (pulseDemoShotActive && now - pulseDemoShotStartMs >= PULSE_DEMO_ON_MS) {
    solPulseOn = false;
    pulseDemoShotActive = false;
    applyHardwareOutputs();
    if (currentScreen == SCREEN_DIAG) uiNeedsFullRedraw = true;
  }
}

// ------------------ TOUCH HANDLING ------------------
void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    int x = t.x;
    int y = t.y;
    logButtonPress("screen touch");

    if (currentScreen != SCREEN_ESTOP_CONFIRM && currentScreen != SCREEN_ESTOP_ACTIVE && insideRect(x, y, 246, 3, 70, 28)) {
      setScreen(SCREEN_ESTOP_CONFIRM);
      return;
    }

    if (currentScreen == SCREEN_HOME) {
      if (simpleHome) {
        if (showVac && insideRect(x, y, 10, 72, 145, 64)) { setScreen(SCREEN_VAC); return; }
        if (showPpm && insideRect(x, y, 165, 72, 145, 64)) { setScreen(SCREEN_PPM); return; }
        if (insideRect(x, y, 10, 150, 145, 64)) { running = !running; logSdEvent(running ? "run start" : "run stop"); uiNeedsFullRedraw = true; return; }
        if (insideRect(x, y, 165, 150, 145, 64)) { setScreen(SCREEN_MORE); return; }
        return;
      }
      if (showVac && insideRect(x, y, 10, 70, 145, 68)) { setScreen(SCREEN_VAC); return; }
      if (showPpm && insideRect(x, y, 165, 70, 145, 68)) { setScreen(SCREEN_PPM); return; }
      if (insideRect(x, y, 10, 148, 145, 68)) { running = !running; logSdEvent(running ? "run start" : "run stop"); uiNeedsFullRedraw = true; return; }
      if (insideRect(x, y, 165, 148, 145, 68)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_VAC) {
      if (insideRect(x, y, 12, 38, 120, 38)) { setScreen(SCREEN_HOME); return; }
      if (insideRect(x, y, 19, 178, 132, 53)) beginHold(1);
      else if (insideRect(x, y, 169, 178, 132, 53)) beginHold(2);
    }

    if (currentScreen == SCREEN_PPM) {
      if (insideRect(x, y, 12, 38, 120, 38)) { setScreen(SCREEN_HOME); return; }
      if (insideRect(x, y, 19, 178, 132, 53)) beginHold(3);
      else if (insideRect(x, y, 169, 178, 132, 53)) beginHold(4);
    }

    if (currentScreen == SCREEN_MORE) {
      if (showLive && insideRect(x, y, 10, 50, 145, 54)) { setScreen(SCREEN_LIVE); return; }
      if (showDiag && insideRect(x, y, 165, 50, 145, 54)) { setScreen(SCREEN_DIAG); return; }
      if (showSettings && insideRect(x, y, 10, 114, 145, 54)) { setScreen(SCREEN_SETTINGS); return; }
      if (insideRect(x, y, 165, 114, 145, 54)) { setScreen(SCREEN_WIFI); return; }
      if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_HOME); return; }
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
      else if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_LIVE) {
      if (insideRect(x, y, 12, 205, 72, 31)) { setScreen(SCREEN_MORE); return; }
      if (insideRect(x, y, 220, 205, 88, 31)) {
        running = !running;
        logSdEvent(running ? "run start" : "run stop");
        uiNeedsFullRedraw = true;
        return;
      }
    }

    if (currentScreen == SCREEN_WIFI) {
      if (insideRect(x, y, 16, 160, 288, 40)) {
        if (wifiActive) {
          webControlEnabled = false;
          markSettingsDirty();
          wifiShutdownRequested = true;
        } else {
          webControlEnabled = true;
          markSettingsDirty();
          startWebServer();
        }
        uiNeedsFullRedraw = true;
        return;
      }
      if (insideRect(x, y, 4, 207, 72, 29)) { setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_DIAG) {
      if (insideRect(x, y, 12, 48, 140, 52)) { solPumpOn = !solPumpOn; applyHardwareOutputs(); uiNeedsFullRedraw = true; logSdEvent(solPumpOn ? "pump output on" : "pump output off"); return; }
      if (insideRect(x, y, 168, 48, 140, 52)) { pulseDemoMode = !pulseDemoMode; solPulseOn = pulseDemoMode; pulseDemoShotActive = pulseDemoMode; pulseDemoShotStartMs = millis(); lastPulseDemoMs = millis(); applyHardwareOutputs(); uiNeedsFullRedraw = true; logSdEvent(pulseDemoMode ? "pulse demo on" : "pulse demo off"); return; }
      if (insideRect(x, y, 12, 112, 140, 52)) { solVacOn = !solVacOn; applyHardwareOutputs(); uiNeedsFullRedraw = true; logSdEvent(solVacOn ? "vac output on" : "vac output off"); return; }
      if (insideRect(x, y, 168, 112, 140, 52)) { solReleaseOn = !solReleaseOn; applyHardwareOutputs(); uiNeedsFullRedraw = true; logSdEvent(solReleaseOn ? "release output on" : "release output off"); return; }
      if (insideRect(x, y, 4, 207, 72, 29)) { allOutputsOff(); setScreen(SCREEN_MORE); return; }
    }

    if (currentScreen == SCREEN_ESTOP_CONFIRM) {
      // Bottom green 10% is CANCEL ONLY. It must never stop automation or release pressure.
      if (insideRect(x, y, 0, 216, 320, 24)) { setScreen(previousScreen); return; }
      // Only the red confirmation area triggers actual emergency release.
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
  if (now - lastLiveSampleMs >= 250) {
    lastLiveSampleMs = now;
    addLivePoint();
  }
  if (currentScreen == SCREEN_LIVE && running && now - lastLiveMs >= 250) {
    lastLiveMs = now;
    updateLiveGraphs();
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
  h.reserve(43000);
  h += F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>");
  h += F("<title>Pluto 9000</title><style>");
  h += F(":root{--bg:#090d14;--card:#141c29;--card2:#1d2b3a;--text:#f2f7ff;--muted:#aab7c9;--blue:#40aaff;--green:#78e6be;--red:#e62630;--yellow:#ffbc4b;--line:#2d3c52;--purple:#b990ff}");
  h += F("*{box-sizing:border-box}html{scroll-behavior:auto}body{margin:0;background:radial-gradient(circle at top,#17253a,#090d14 58%);color:var(--text);font-family:Arial,Helvetica,sans-serif;-webkit-text-size-adjust:100%}header{padding:18px 16px;background:#0d1420;border-bottom:1px solid #263245;position:sticky;top:0;z-index:5}h1{margin:0;font-size:34px;letter-spacing:.5px}.sub{color:var(--muted);font-size:15px;margin-top:6px;line-height:1.35}.wrap{padding:14px;max-width:1120px;margin:auto}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(285px,1fr));gap:14px}.card{background:linear-gradient(180deg,var(--card),#101723);border:1px solid #263245;border-radius:20px;padding:17px;box-shadow:0 12px 28px rgba(0,0,0,.25)}h2{margin:0 0 12px;font-size:25px}.big{font-size:50px;font-weight:900;line-height:1}.row{display:flex;gap:10px;align-items:center;flex-wrap:wrap}.label{color:var(--muted);font-size:16px;margin:11px 0 7px;line-height:1.35}.note{color:var(--muted);font-size:15px;line-height:1.5}.smallcap{letter-spacing:.08em;text-transform:uppercase;color:var(--muted);font-size:12px;font-weight:800}input[type=range]{width:100%;accent-color:var(--green);height:34px}input[type=number],input[type=text],select{width:100%;background:#0b111b;border:1px solid #304158;color:var(--text);border-radius:14px;padding:13px;font-size:19px}button{border:0;border-radius:16px;padding:15px 17px;font-size:18px;font-weight:900;color:#081019;background:var(--green);min-height:58px;touch-action:manipulation}.ghost{background:#223247;color:var(--text);border:1px solid #34465f}.danger{background:var(--red);color:white}.warn{background:var(--yellow);color:#140c00}.blue{background:var(--blue);color:#06101a}.purple{background:var(--purple);color:#100719}.pill{display:inline-block;border-radius:999px;background:#223247;color:var(--muted);padding:9px 12px;margin:4px 4px 0 0}.toggle{display:flex;align-items:center;justify-content:space-between;gap:12px;border:1px solid #2e3d52;border-radius:16px;padding:13px;margin:9px 0;background:#101723}.toggle span{font-size:17px}.toggle input{width:28px;height:28px}.share{font-size:21px;background:#0b111b;border:1px solid #304158;padding:14px;border-radius:14px;word-break:break-all}.meter{height:25px;border-radius:999px;background:#243145;overflow:hidden;margin-top:9px}.fill{height:100%;background:linear-gradient(90deg,var(--blue),var(--green));width:0%}.mini{font-size:13px;color:var(--muted);line-height:1.45}.graph{width:100%;height:118px;background:#0b111b;border:1px solid #304158;border-radius:14px;margin-top:10px}.divider{height:1px;background:#263245;margin:14px 0}nav{display:flex;gap:10px;flex-wrap:wrap;margin-top:12px}nav a{color:var(--text);text-decoration:none;background:#1a2637;padding:11px 14px;border-radius:999px;border:1px solid #2b3d55;font-weight:800}.dangerBox{border:1px solid #74303a;background:#201018;border-radius:18px;padding:14px;margin-top:12px}details{border:1px solid #2e3d52;border-radius:18px;padding:10px 12px;background:#101723;margin-top:12px}summary{font-size:20px;font-weight:900;cursor:pointer;list-style:none}summary::-webkit-details-marker{display:none}.confirmBox{display:none;margin-top:12px}.confirmBox.show{display:block}.mobileOnly{display:none}");
  h += F("@media(max-width:650px){body.mobile h1{font-size:30px}body.mobile header{padding:15px 12px}body.mobile .wrap{padding:10px}body.mobile .grid{display:block}body.mobile .card{margin:0 0 12px;padding:18px;border-radius:22px}body.mobile h2{font-size:26px}body.mobile .big{font-size:56px}body.mobile button{width:100%;font-size:21px;min-height:68px;margin:3px 0}body.mobile .row{display:grid;grid-template-columns:1fr;gap:8px}body.mobile nav{display:grid;grid-template-columns:1fr 1fr}body.mobile nav a{text-align:center;font-size:17px;padding:14px 8px}body.mobile .label{font-size:18px}body.mobile .toggle span{font-size:18px}body.mobile .mobileOnly{display:block}.hideMobile{display:none}}body.light{--bg:#eef5fb;--card:#ffffff;--card2:#dce9f5;--text:#17202d;--muted:#5b6877;background:#eef5fb}body.light header{background:#ffffff}body.light input[type=number],body.light input[type=text],body.light select,body.light .share,body.light .graph{background:#eef5fb;color:#17202d}</style></head><body class='");
  if (mobileMode) h += F("mobile ");
  if (!darkTheme) h += F("light");
  h += F("'><header><h1>Pluto 9000</h1><div class='sub'>PulseCore Web Control - WiFi: Pluto9000 - open network - IP: 192.168.69.69</div><nav><a href='#dash'>Dashboard</a><a href='#control'>Tune</a><a href='#pulse'>Pulse</a><a href='#layout'>Layout</a><a href='#more'>More</a><a href='#system'>System</a></nav></header><div class='wrap'>");

  // Dashboard now includes direct controls, not just monitoring.
  h += F("<section id='dash' class='grid'>");
  h += F("<div class='card'><div class='smallcap'>Live vacuum</div><h2>VAC</h2><div class='big'><span id='vacNow'>"); h += String((int)vacActual); h += F("</span>%</div><div class='label'>Target: <span id='vacTarget'>"); h += String(vacTarget); h += F("</span>%</div><div class='meter'><div id='vacFill' class='fill'></div></div><div class='label'>Dashboard control</div><input id='dashVac' type='range' min='0' max='100' value='"); h += String(vacTarget); h += F("' oninput='dashVacPreview(this.value)' onchange='setVal(\"vac\",this.value)'><div class='row'><button class='ghost' onclick='stepVac(-5)'>VAC -</button><button onclick='stepVac(5)'>VAC +</button></div></div>");
  h += F("<div class='card'><div class='smallcap'>Pulse frequency</div><h2>PPM</h2><div class='big'><span id='ppmNow'>"); h += String(ppmTarget); h += F("</span></div><div class='label'><span id='ppsNow'>"); h += String((float)ppmTarget / 60.0f, 2); h += F("</span> pulses/second - 40-300 PPM</div><div class='meter'><div id='ppmFill' class='fill'></div></div><canvas id='pulseCanvas' class='graph'></canvas><div class='label'>Dashboard control</div><input id='dashPpm' type='range' min='40' max='300' value='"); h += String(ppmTarget); h += F("' oninput='dashPpmPreview(this.value)' onchange='setVal(\"ppm\",this.value)'><div class='row'><button class='ghost' onclick='stepPpm(-10)'>PPM -</button><button onclick='stepPpm(10)'>PPM +</button></div></div>");
  h += F("<div class='card'><div class='smallcap'>Machine state</div><h2>Status</h2><p><span class='pill' id='runPill'>"); h += running ? "RUNNING" : "STOPPED"; h += F("</span><span class='pill' id='sdPill'>SD: "); h += sdReady ? "READY" : "NOT FOUND"; h += F("</span></p><div class='row'><button id='runToggleBtn' onclick='toggleRun()' class='"); h += running ? "danger" : ""; h += F("'>"); h += running ? "STOP" : "START"; h += F("</button><button class='danger' onclick='api(\"/api/estop\")'>E-STOP</button></div><div class='divider'></div><div class='label'>Share browser control</div><div class='share' id='shareLink'>"); h += shareLink(); h += F("</div><button class='ghost' onclick='copyLink()'>COPY LINK</button></div>");
  h += F("<div class='card'><div class='smallcap'>SD data viewer</div><h2>Stats Report</h2><p class='note'>Open the <b>stats.html</b> report stored on the SD card. It reads the daily log files in /logs and turns them into simple charts and tables.</p><p><span class='pill'>Stats: <span id='statsReady'>"); h += (sdReady && SD.exists("/stats.html")) ? "READY" : "NOT READY"; h += F("</span></span><span class='pill'>Log: <span id='currentLogMini'>"); h += sessionLogPath.length() ? sessionLogPath : "none"; h += F("</span></span></p><div class='row'><button class='blue' onclick='openStatsReport()'>OPEN STATS.HTML</button><button class='ghost' onclick='location.href=\"/current.log\"'>VIEW TODAY LOG</button></div><p class='mini'>No SD card is required for normal use. SD only adds logging and the stats report.</p></div>");
  h += F("</section>");

  h += F("<section id='control' class='grid'><div class='card'><h2>VAC Tuning</h2><div class='label'>Set point 0-100%. Actual VAC ramps slower so it behaves like a real vacuum system.</div><input id='vac' type='range' min='0' max='100' value='"); h += String(vacTarget); h += F("' oninput='liveNum(\"vacNum\",this.value+\"%\")' onchange='setVal(\"vac\",this.value)'><div class='big'><span id='vacNum'>"); h += String(vacTarget); h += F("%</span></div><div class='label'>VAC reaction speed</div><input id='ramp' type='range' min='1' max='10' value='"); h += String(vacRampSpeed); h += F("' onchange='setVal(\"ramp\",this.value)'><p class='mini'>Lower = softer/slower catch-up. Higher = faster simulated response.</p></div>");
  h += F("<div class='card'><h2>PPM Tuning</h2><div class='label'>Range is locked from 40 to 300 PPM. 60 PPM = 1 pulse/second. 120 PPM = 2 pulses/second.</div><input id='ppm' type='range' min='40' max='300' value='"); h += String(ppmTarget); h += F("' oninput='ppmLocal(this.value)' onchange='setVal(\"ppm\",this.value)'><div class='big'><span id='ppmNum'>"); h += String(ppmTarget); h += F("</span></div><div class='label'><span id='ppsNum'>"); h += String((float)ppmTarget / 60.0f, 2); h += F("</span> pulses per second</div><div class='label'>Graph window seconds</div><input type='range' min='1' max='10' value='"); h += String(graphSpeed); h += F("' onchange='setVal(\"graph\",this.value)'></div>");
  h += F("<div class='card'><h2>Presets</h2><div class='row'><button onclick='preset(40,25)'>40</button><button onclick='preset(60,45)'>60</button><button onclick='preset(120,55)'>120</button><button onclick='preset(180,65)'>180</button><button onclick='preset(240,70)'>240</button><button onclick='preset(300,80)'>300</button></div><p class='mini'>Preset buttons update the live demo. Use Save to keep them after reboot.</p></div></section>");

  h += F("<section id='pulse' class='grid'><div class='card'><h2>Pulse Timing</h2><p class='note'>The pulse graph uses real time spacing. 60 PPM shows one pulse every second. 120 PPM shows two pulses every second. 300 PPM shows five pulses every second.</p><div class='divider'></div><p><span class='pill'>40 PPM = 0.67/sec</span><span class='pill'>60 PPM = 1/sec</span><span class='pill'>120 PPM = 2/sec</span><span class='pill'>300 PPM = 5/sec</span></p></div>");
  h += F("<div class='card'><h2>Web Options</h2><div class='toggle'><span>Mobile mode</span><input type='checkbox' "); h += checked(mobileMode); h += F(" onchange='setVal(\"mobile\",this.checked?1:0)'></div><div class='toggle'><span>Show advanced web sections</span><input type='checkbox' "); h += checked(showAdvancedWeb); h += F(" onchange='setVal(\"advweb\",this.checked?1:0)'></div><div class='label'>Web refresh seconds</div><input type='range' min='1' max='5' value='"); h += String(webRefreshSec); h += F("' onchange='setVal(\"webref\",this.value)'><div class='toggle'><span>Simple M5 home screen</span><input type='checkbox' "); h += checked(simpleHome); h += F(" onchange='setVal(\"simple\",this.checked?1:0)'></div></div></section>");

  h += F("<section id='layout' class='grid'><div class='card'><h2>Device Layout</h2><div class='toggle'><span>Large buttons</span><input type='checkbox' "); h += checked(largeButtons); h += F(" onchange='setVal(\"large\",this.checked?1:0)'></div><div class='label'>Screen scale idea</div><input type='range' min='1' max='3' value='"); h += String(screenScale); h += F("' onchange='setVal(\"scale\",this.value)'><div class='label'>Backlight step</div><input type='range' min='1' max='10' value='"); h += String(backlightStep); h += F("' onchange='setVal(\"bl\",this.value)'></div>");
  h += F("<div class='card'><h2>Screen Visibility</h2><div class='toggle'><span>VAC screen</span><input type='checkbox' "); h += checked(showVac); h += F(" onchange='setVal(\"showVac\",this.checked?1:0)'></div><div class='toggle'><span>PPM screen</span><input type='checkbox' "); h += checked(showPpm); h += F(" onchange='setVal(\"showPpm\",this.checked?1:0)'></div><div class='toggle'><span>Live screen</span><input type='checkbox' "); h += checked(showLive); h += F(" onchange='setVal(\"showLive\",this.checked?1:0)'></div><div class='toggle'><span>Manual test screen</span><input type='checkbox' "); h += checked(showDiag); h += F(" onchange='setVal(\"showDiag\",this.checked?1:0)'></div><div class='toggle'><span>Settings screen</span><input type='checkbox' "); h += checked(showSettings); h += F(" onchange='setVal(\"showSettings\",this.checked?1:0)'></div><div class='toggle'><span>WiFi screen</span><input type='checkbox' "); h += checked(showWifi); h += F(" onchange='setVal(\"showWifi\",this.checked?1:0)'></div></div></section>");

  h += F("<section id='more' class='grid'><div class='card'><h2>More</h2><p class='note'>Extra tools live here so the dashboard stays clean. The stats report below is the easy way to read the data recorded on the SD card.</p><div class='row'><button onclick='api(\"/api/logsnap\")'>LOG SNAPSHOT</button><button class='blue' onclick='openStatsReport()'>OPEN STATS.HTML</button><button class='ghost' onclick='location.href=\"/current.log\"'>VIEW TODAY LOG</button></div><p class='mini'>The stats page is served from <b>/stats.html</b> and pulls data from the daily files in <b>/logs</b>.</p></div>");
  h += F("<div class='card'><h2>WiFi</h2><p><span class='pill'>Network: <span id='ssidNow'>"); h += wifiStatusLabel(); h += F("</span></span><span class='pill'>IP: <span id='ipNow'>"); h += wifiIpLabel(); h += F("</span></span><span class='pill'>Web: <span id='wifiState'>"); h += String(wifiActive ? "ON" : "OFF"); h += F("</span></span></p><p class='note'>Pluto9000 starts as an open ad-hoc web control network. You can also scan for a nearby router and join it. When DHCP gives the unit an IP, it will show here.</p><div class='row'><button class='ghost' onclick='scanWifi()'>SCAN NETWORKS</button><button class='warn' onclick='turnWifiOff()'>TURN WEBSERVER OFF</button></div><div class='label'>Found networks</div><select id='wifiList'></select><div class='label'>Password if needed</div><input id='wifiPass' type='password' autocomplete='off' placeholder='WiFi password'><button class='blue' onclick='joinWifi()'>CONNECT TO SELECTED WIFI</button><p class='mini' id='wifiMsg'></p></div>");
h += F("<div class='card'><h2>SD Card Logging</h2><p><span class='pill'>SD: <span id='sdWeb'>"); h += sdReady ? "READY" : "NOT FOUND"; h += F("</span></span><span class='pill'>Logging: <span id='sdData'>"); h += sdDataReady ? "ON" : "NOT SET UP"; h += F("</span></span><span class='pill'>Events: <span id='sdEvents'>"); h += String(sdEventCount); h += F("</span></span></p><p class='note'><b>SD card is optional.</b> Pluto 9000 can run normally without an SD card. The SD card is only for recording logs, saving session stats, and creating <b>stats.html</b>.</p><details><summary>SD setup tools</summary><p class='note'>Use this only if you want data recording. This prepares Pluto data files on the SD card and clears/recreates Pluto-generated logs, <b>stats.html</b>, <b>pluto9000.cfg</b>, and daily files in <b>/logs</b>. It does not low-level format the card.</p><button class='ghost' onclick='showSdConfirm()'>SET UP / RESET SD LOGGING</button><div id='sdConfirm' class='confirmBox'><div class='dangerBox'><h2>Confirm SD setup</h2><p class='note'>This will reset Pluto-generated SD data files. The unit does not need an SD card to operate.</p><div class='row'><button class='danger' onclick='formatSd()'>YES, SET UP SD LOGGING</button><button class='ghost' onclick='hideSdConfirm()'>CANCEL</button></div></div></div><p class='mini' id='formatMsg'></p></details></div>");
  h += F("<div class='card'><h2>Safety</h2><div class='label'>Emergency release timing: <span id='releaseView'>"); h += String(releaseMs); h += F("</span> ms</div><input type='range' min='250' max='5000' step='250' value='"); h += String(releaseMs); h += F("' oninput='liveNum(\"releaseView\",this.value)' onchange='setVal(\"release\",this.value)'><p class='mini'>Demo only. Final hardware version will force pump off and open/close the correct valves to release positive and negative pressure immediately.</p><button class='danger' onclick='api(\"/api/estop\")'>OPEN E-STOP</button><button class='warn' onclick='api(\"/api/release\")'>SIM RELEASE</button></div></section>");

  if (showAdvancedWeb) {
    h += F("<section id='advanced' class='grid'><div class='card'><h2>Advanced Ideas</h2><p class='note'>Later this can hold valve calibration, pump PWM limits, sensor offsets, vacuum leak checks, service diagnostics, and named profiles.</p></div><div class='card'><h2>Future Profiles</h2><div class='row'><button class='ghost'>Save Slot 1</button><button class='ghost'>Save Slot 2</button><button class='ghost'>Save Slot 3</button></div><p class='mini'>Placeholders only for now. They make room for user-programmable modes later.</p></div></section>");
  }

  h += F("<section id='system' class='grid'><div class='card'><h2>System</h2><p><span class='pill'>Heap: <span id='heap'>"); h += String(ESP.getFreeHeap()); h += F("</span></span><span class='pill'>Uptime: <span id='uptime'>0</span>s</span><span class='pill'>Saved: <span id='saved'>"); h += settingsDirty ? "NO" : "YES"; h += F("</span></span></p><div class='row'><button onclick='api(\"/api/save\")'>SAVE SETTINGS</button><button class='ghost' onclick='api(\"/api/reload\")'>RELOAD UI</button></div></div></section>");

  h += F("</div><script>");
  h += F("let refreshSec="); h += String(webRefreshSec); h += F(";function el(id){return document.getElementById(id)}function liveNum(id,v){let e=el(id);if(e)e.innerText=v}function clamp(v,a,b){v=parseInt(v||0);return Math.max(a,Math.min(b,v))}function ppmLocal(v){v=clamp(v,40,300);liveNum('ppmNum',v);liveNum('ppsNum',(v/60).toFixed(2));liveNum('ppmNow',v);liveNum('ppsNow',(v/60).toFixed(2));let d=el('dashPpm');if(d)d.value=v;let p=el('ppm');if(p)p.value=v;drawPulse(v)}function dashPpmPreview(v){ppmLocal(v)}function dashVacPreview(v){v=clamp(v,0,100);liveNum('vacNum',v+'%');liveNum('vacTarget',v);let d=el('dashVac');if(d)d.value=v;let p=el('vac');if(p)p.value=v}function api(u){return fetch(u).then(r=>r.text()).then(t=>{poll();return t})}function setVal(k,v){return api('/api/set?'+k+'='+encodeURIComponent(v))}function stepVac(d){let v=clamp((el('dashVac')?el('dashVac').value:0)*1+d,0,100);dashVacPreview(v);setVal('vac',v)}function stepPpm(d){let v=clamp((el('dashPpm')?el('dashPpm').value:60)*1+d,40,300);ppmLocal(v);setVal('ppm',v)}function preset(ppm,vac){ppmLocal(ppm);dashVacPreview(vac);api('/api/set?ppm='+ppm+'&vac='+vac)}function copyLink(){let v=el('shareLink')?el('shareLink').innerText:'';navigator.clipboard&&navigator.clipboard.writeText(v)}function toggleRun(){let running=(el('runPill')&&el('runPill').innerText==='RUNNING');api('/api/run?state='+(running?0:1))}function showSdConfirm(){let e=el('sdConfirm');if(e)e.classList.add('show')}function hideSdConfirm(){let e=el('sdConfirm');if(e)e.classList.remove('show')}function formatSd(){fetch('/api/sdformat?confirm=YES').then(r=>r.text()).then(t=>{liveNum('formatMsg',t);hideSdConfirm();poll()})}function openStatsReport(){window.location.href='/stats.html'}function turnWifiOff(){fetch('/api/wifioff').then(()=>{liveNum('wifiState','OFF');alert('Web control is turning off. Use the device screen More - WiFi to turn it back on.')})}async function scanWifi(){let m=el('wifiMsg');if(m)m.innerText='Scanning...';let r=await fetch('/api/wifiscan',{cache:'no-store'}).then(r=>r.json());let s=el('wifiList');if(s)s.innerHTML=r.networks.map(n=>`<option value='${n.ssid}'>${n.ssid} (${n.rssi})</option>`).join('');if(m)m.innerText='Select a network and connect.'}async function joinWifi(){let ss=el('wifiList')?el('wifiList').value:'';let pw=el('wifiPass')?el('wifiPass').value:'';let m=el('wifiMsg');if(m)m.innerText='Connecting...';let t=await fetch('/api/wifijoin?ssid='+encodeURIComponent(ss)+'&pass='+encodeURIComponent(pw)).then(r=>r.text());if(m)m.innerText=t;poll()}");
  h += F("function drawPulse(ppm){let c=el('pulseCanvas');if(!c)return;ppm=clamp(ppm,40,300);let r=c.getBoundingClientRect();c.width=r.width*devicePixelRatio;c.height=r.height*devicePixelRatio;let ctx=c.getContext('2d');ctx.scale(devicePixelRatio,devicePixelRatio);ctx.clearRect(0,0,r.width,r.height);ctx.strokeStyle='#2d3c52';ctx.lineWidth=1;for(let i=1;i<4;i++){let y=r.height*i/4;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(r.width,y);ctx.stroke()}ctx.fillStyle='#aab7c9';ctx.font='12px Arial';ctx.fillText('60 PPM = 1 pulse/sec',10,16);ctx.strokeStyle='#40aaff';ctx.lineWidth=3;let period=60000/ppm;let win=4000;ctx.beginPath();for(let x=0;x<r.width;x++){let t=(x/r.width)*win;let ph=(t%period)/period;let val=Math.sin(ph*Math.PI*2);if(val<0)val=0;let y=r.height-12-val*(r.height-30);if(x===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)}ctx.stroke()}");
  h += F("function poll(){fetch('/api/state').then(r=>r.json()).then(s=>{liveNum('vacNow',s.vacActual);liveNum('vacTarget',s.vac);liveNum('ppmNow',s.ppm);liveNum('ppsNow',(s.ppm/60).toFixed(2));if(el('vacFill'))el('vacFill').style.width=s.vacActual+'%';if(el('ppmFill'))el('ppmFill').style.width=((s.ppm-40)/260*100)+'%';liveNum('runPill',s.running?'RUNNING':'STOPPED');liveNum('heap',s.heap);liveNum('uptime',s.uptime);liveNum('saved',s.saved?'YES':'NO');liveNum('sdWeb',s.sdReady?'READY':'NOT FOUND');liveNum('sdData',s.sdDataReady?'ON':'NOT SET UP');liveNum('sdEvents',s.sdEvents);liveNum('statsReady',s.statsExists?'READY':'NOT READY');liveNum('currentLogMini',s.currentLog||'none');liveNum('ssidNow',s.ssid||'');liveNum('ipNow',s.ip||'');liveNum('wifiState',s.wifiActive?'ON':'OFF');liveNum('shareLink',s.share||'');let rb=el('runToggleBtn');if(rb){rb.innerText=s.running?'STOP':'START';rb.className=s.running?'danger':'';}drawPulse(s.ppm);})}setInterval(poll,refreshSec*1000);poll();</script></body></html>");
  return h;
}
void handleRoot() { server.send(200, "text/html", htmlPage()); }

void handleState() {
  String j;
  j.reserve(380);
  j += "{";
  j += "\"vac\":" + String(vacTarget) + ",";
  j += "\"vacActual\":" + String((int)vacActual) + ",";
  j += "\"ppm\":" + String(ppmTarget) + ",";
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
  j += "\"wifiActive\":" + String(wifiActive ? "true" : "false") + ",";
  j += "\"ssid\":\"" + wifiStatusLabel() + "\",";
  j += "\"ip\":\"" + wifiIpLabel() + "\",";
  j += "\"share\":\"" + shareLink() + "\",";
  j += "\"webPort\":" + String(webPort) + ",";
  j += "\"currentLog\":\"" + sessionLogPath + "\"";
  j += "}";
  server.send(200, "application/json", j);
}
void handleSet() {
  bool paletteChanged = false;
  bool layoutChanged = false;

  if (server.hasArg("vac")) { vacTarget = clampInt(server.arg("vac").toInt(), 0, 100); vacValueDirty = true; liveValueDirty = true; }
  if (server.hasArg("ppm")) { ppmTarget = clampInt(server.arg("ppm").toInt(), PPM_MIN, PPM_MAX); ppmValueDirty = true; liveValueDirty = true; }
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
  if (server.hasArg("mobile")) { mobileMode = server.arg("mobile").toInt() == 1; }
  if (server.hasArg("simple")) { simpleHome = server.arg("simple").toInt() == 1; layoutChanged = true; }
  if (server.hasArg("advweb")) { showAdvancedWeb = server.arg("advweb").toInt() == 1; }
  if (server.hasArg("webref")) { webRefreshSec = clampInt(server.arg("webref").toInt(), 1, 5); }
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
    if (newState != running) {
      running = newState;
      logSdEvent(running ? "run start" : "run stop");
    }
  }
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
    server.send(404, "text/plain", "No daily log is set up yet. Logs are stored in /logs/MM-DD-YYYY.log after SD logging is set up.");
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
  name.replace("/logs/", "");
  name.replace("/", "");
  if (!nameLooksLikeLog(name)) { server.send(400, "text/plain", "BAD_LOG_NAME"); return; }
  String path = "/logs/" + name;
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
  WiFi.mode(WIFI_AP_STA);
  WiFi.scanDelete();
  int n = WiFi.scanNetworks(false, true);
  String j = "{\"networks\":[";
  if (n < 0) n = 0;
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + safeJsonString(WiFi.SSID(i)) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"enc\":" + String((int)WiFi.encryptionType(i)) + "}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleWifiJoin() {
  if (!server.hasArg("ssid")) { server.send(400, "text/plain", "SSID_REQUIRED"); return; }
  staSsid = server.arg("ssid");
  staPass = server.hasArg("pass") ? server.arg("pass") : "";
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 9000) {
    server.handleClient();
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  markSettingsDirty();
  uiNeedsFullRedraw = true;
  if (WiFi.status() == WL_CONNECTED) {
    logSdEvent("wifi sta connected");
    server.send(200, "text/plain", "CONNECTED " + WiFi.localIP().toString());
  } else {
    logSystemProblem("wifi sta connect failed");
    server.send(200, "text/plain", "CONNECT_FAILED");
  }
}

void handleWifiOff() {
  webControlEnabled = false;
  markSettingsDirty();
  server.send(200, "text/plain", "WEB_OFF");
  wifiShutdownRequested = true;
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
  if (!SD.exists("/logs")) SD.mkdir("/logs");
  File root = SD.open("/logs");
  File file = root.openNextFile();
  while (file) {
    String n = String(file.name());
    file.close();
    if (nameLooksLikeLog(n)) {
      n.replace("/logs/", "");
      n.replace("/", "");
      SD.remove("/logs/" + n);
    }
    file = root.openNextFile();
  }
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

void startWebServer() {
  if (wifiActive) return;
  if (serverPtr != nullptr) { delete serverPtr; serverPtr = nullptr; }
  serverPtr = new WebServer(webPort);
  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192,168,69,69);
  IPAddress apGW(192,168,69,69);
  IPAddress apSN(255,255,255,0);
  WiFi.softAPConfig(apIP, apGW, apSN);
  WiFi.softAP(AP_NAME);
  if (staSsid.length() > 0) WiFi.begin(staSsid.c_str(), staPass.c_str());
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
  server.on("/stats.html", handleStatsFile);
  server.on("/current.log", handleLogFile);
  server.on("/api/logsnap", handleLogSnapshot);
  server.on("/api/sdinfo", handleSdInfo);
  server.on("/api/sdformat", handleSdFormat);
  server.on("/api/wifioff", handleWifiOff);
  server.on("/api/wifiscan", handleWifiScan);
  server.on("/api/wifijoin", handleWifiJoin);
  server.on("/api/loglist", handleLogList);
  server.on("/api/logdata", handleLogData);

  // Common captive portal detection URLs
  server.on("/generate_204", handleRoot);
  server.on("/gen_204", handleRoot);
  server.on("/hotspot-detect.html", handleRoot);
  server.on("/fwlink", handleRoot);
  server.onNotFound(handleRoot);
  server.begin();
  wifiActive = true;
  wifiShutdownRequested = false;
}

void stopWebServerNow() {
  if (serverPtr != nullptr) {
    server.stop();
    delete serverPtr;
    serverPtr = nullptr;
  }
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  if (WiFi.status() == WL_CONNECTED) WiFi.mode(WIFI_STA);
  else WiFi.mode(WIFI_OFF);
  wifiActive = false;
  wifiShutdownRequested = false;
}

// ------------------ SETUP / LOOP ------------------
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  bootMs = millis();

  configureOutputPins();
  loadSettings();
  initPalette();
  applyBacklight();

  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  sdReady = SD.begin(SD_CS_PIN, SPI, 25000000);
  if (sdReady) {
    sdDataReady = SD.exists("/stats.html") || SD.exists(dailyLogPath());
    if (sdDataReady) {
      beginSessionLog(false);
      logSdEvent("boot");
    }
  } else {
    Serial.println("SD init failed");
  }
  for (int i = 0; i < LIVE_POINTS; i++) {
    liveVacHist[i] = (int)vacActual;
    livePpmHist[i] = ppmTarget;
  }

  bootAnimation();

  // Start Wi-Fi after boot animation so clients do not connect before loop services HTTP/DNS.
  if (webControlEnabled) startWebServer();

  uiNeedsFullRedraw = true;
}

void loop() {
  M5.update();
  if (wifiActive && serverPtr != nullptr) {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  if (wifiShutdownRequested) stopWebServerNow();

  updateVacPhysics();
  updatePulseDemo();
  writePeriodicSdSample();
  handleTouch();

  if (uiNeedsFullRedraw) drawCurrentScreen();
  updateActiveScreenRegions();
  flushSettingsIfIdle();

  // Tiny RTOS yield. No Arduino delay() here.
  vTaskDelay(1);
}
