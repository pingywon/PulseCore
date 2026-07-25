// =====================================================================
//  pf_ui.cpp -- rendering and touch
// =====================================================================
#include "pf_ui.h"
#include "pf_hal.h"

#include <M5Unified.h>
#include <math.h>

namespace pf {
namespace ui {

// ------------------------------------------------------------------ //
//  Geometry.  One 8 px grid, three fixed bands.
// ------------------------------------------------------------------ //
static const int W = PF_SCREEN_W;
static const int H = PF_SCREEN_H;

static const int TOPBAR_H = 34;
static const int NAV_H    = 46;
static const int BODY_Y   = TOPBAR_H;
static const int BODY_H   = H - TOPBAR_H - NAV_H;   // 160
static const int NAV_Y    = H - NAV_H;

// ------------------------------------------------------------------ //
//  Button identity.  A control is referred to by id everywhere.
// ------------------------------------------------------------------ //
enum BtnId {
  B_NONE = 0,
  // persistent chrome
  B_ESTOP, B_BACK,
  B_NAV_HOME, B_NAV_VAC, B_NAV_PULSE, B_NAV_MOTOR, B_NAV_RHYTHM,
  // home
  B_RUN, B_GRAPH, B_NET, B_SETTINGS,
  // steppers
  B_VAC_DN, B_VAC_UP, B_RAMP_DN, B_RAMP_UP,
  B_PPM_DN, B_PPM_UP, B_RATIO_DN, B_RATIO_UP,
  B_MOTOR_DN, B_MOTOR_UP,
  // rhythm
  B_RHY_PREV, B_RHY_NEXT, B_RHY_TOGGLE, B_RHY_SLOW, B_RHY_FAST, B_RHY_REC,
  // recorder
  B_REC_SLOT, B_REC_ARM, B_REC_TAP, B_REC_SAVE, B_REC_CLEAR,
  // settings
  B_THEME, B_BL_DN, B_BL_UP, B_AUTH, B_LIMIT_DN, B_LIMIT_UP,
  // network
  B_NET_AP, B_NET_WEB,
  // e-stop screens
  B_ES_RELEASE, B_ES_CANCEL, B_ES_RETURN,
};

struct Btn {
  uint8_t id;
  int16_t x, y, w, h;
  const char* label;
  uint8_t style;      // 0 ghost, 1 accent, 2 good, 3 warn, 4 danger, 5 flat
};

// Repeat behaviour: which ids accelerate when held.
static bool isHoldable(uint8_t id) {
  switch (id) {
    case B_VAC_DN: case B_VAC_UP:
    case B_PPM_DN: case B_PPM_UP:
    case B_RATIO_DN: case B_RATIO_UP:
    case B_MOTOR_DN: case B_MOTOR_UP:
    case B_RAMP_DN: case B_RAMP_UP:
    case B_BL_DN: case B_BL_UP:
    case B_LIMIT_DN: case B_LIMIT_UP:
    case B_RHY_SLOW: case B_RHY_FAST:
      return true;
    default: return false;
  }
}

// ------------------------------------------------------------------ //
//  Theme
// ------------------------------------------------------------------ //
struct Palette {
  uint16_t bg, panel, panel2, text, muted, accent, accent2, warn, danger, good, grid, shadow;
};
static Palette P;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) { return M5.Display.color565(r, g, b); }

static void loadPalette() {
  if (app.settings.darkTheme) {
    P.bg      = rgb(9, 13, 20);
    P.panel   = rgb(20, 27, 38);
    P.panel2  = rgb(30, 40, 54);
    P.text    = rgb(232, 238, 247);
    P.muted   = rgb(125, 143, 166);
    P.accent  = rgb(53, 198, 244);
    P.accent2 = rgb(79, 227, 168);
    P.warn    = rgb(255, 177, 61);
    P.danger  = rgb(255, 77, 90);
    P.good    = rgb(79, 227, 168);
    P.grid    = rgb(42, 54, 70);
    P.shadow  = rgb(5, 8, 12);
  } else {
    P.bg      = rgb(238, 242, 247);
    P.panel   = rgb(255, 255, 255);
    P.panel2  = rgb(226, 233, 242);
    P.text    = rgb(17, 25, 38);
    P.muted   = rgb(96, 112, 133);
    P.accent  = rgb(0, 132, 186);
    P.accent2 = rgb(0, 154, 108);
    P.warn    = rgb(199, 118, 0);
    P.danger  = rgb(214, 40, 52);
    P.good    = rgb(0, 154, 108);
    P.grid    = rgb(197, 208, 221);
    P.shadow  = rgb(180, 191, 205);
  }
}

static uint16_t styleFill(uint8_t s) {
  switch (s) {
    case 1: return P.accent;
    case 2: return P.good;
    case 3: return P.warn;
    case 4: return P.danger;
    case 5: return P.panel;
    default: return P.panel2;
  }
}
static uint16_t styleText(uint8_t s) {
  switch (s) {
    case 1: case 2: case 3: return app.settings.darkTheme ? rgb(6, 12, 18) : rgb(255, 255, 255);
    case 4: return rgb(255, 255, 255);
    default: return P.text;
  }
}

// ------------------------------------------------------------------ //
//  Canvas
// ------------------------------------------------------------------ //
static M5Canvas cv(&M5.Display);
static bool     g_haveCanvas = false;

static LovyanGFX* gfx() {
  return g_haveCanvas ? (LovyanGFX*)&cv : (LovyanGFX*)&M5.Display;
}

// ------------------------------------------------------------------ //
//  State
// ------------------------------------------------------------------ //
static int      g_screen   = SCR_HOME;
static int      g_prevScr  = SCR_HOME;
static bool     g_dirty    = true;
static uint32_t g_lastPaint = 0;

static uint8_t  g_hold      = B_NONE;
static uint32_t g_holdStart = 0;
static uint32_t g_holdLast  = 0;
static const uint32_t FIRST_REPEAT_MS = 300;
static const uint32_t REPEAT_MS       = 130;
static const uint32_t FAST_AFTER_MS   = 1200;
static const uint32_t TURBO_AFTER_MS  = 3000;

static int  g_recSlot = 0;

// network mirror
static char g_ssid[34] = "-";
static char g_ip[20]   = "0.0.0.0";
static char g_mode[28] = "Local only";
static int  g_bars     = 0;
static bool g_ap       = false;

// live history for the graph screen
static const int HIST = 118;
static uint8_t g_hVac[HIST], g_hMotor[HIST];
static uint16_t g_hPpm[HIST];
static int      g_hIdx = 0;
static uint32_t g_lastHist = 0;

// The button table for the frame we most recently drew. Hit-testing
// reads exactly this, so a control can never be drawn somewhere it
// cannot be pressed.
static const int MAX_BTNS = 20;
static Btn  g_btns[MAX_BTNS];
static int  g_btnCount = 0;

static void clearBtns() { g_btnCount = 0; }
static void addBtn(const Btn& b) { if (g_btnCount < MAX_BTNS) g_btns[g_btnCount++] = b; }

// ------------------------------------------------------------------ //
//  Primitives
// ------------------------------------------------------------------ //
static void card(int x, int y, int w, int h, uint16_t fill) {
  gfx()->fillRoundRect(x, y + 1, w, h, 10, P.shadow);
  gfx()->fillRoundRect(x, y, w, h, 10, fill);
}

static void label(const char* s, int x, int y, uint16_t col, const lgfx::IFont* f, textdatum_t d = textdatum_t::middle_center) {
  gfx()->setFont(f);
  gfx()->setTextDatum(d);
  gfx()->setTextColor(col);
  gfx()->drawString(s, x, y);
}

static void drawBtn(const Btn& b) {
  uint16_t fill = styleFill(b.style);
  bool pressed = (g_hold == b.id);
  if (pressed) fill = P.accent;
  card(b.x, b.y, b.w, b.h, fill);
  if (b.style == 0 || b.style == 5) gfx()->drawRoundRect(b.x, b.y, b.w, b.h, 10, P.grid);
  const lgfx::IFont* f = (b.h >= 44) ? &fonts::Font4 : &fonts::Font2;
  label(b.label, b.x + b.w / 2, b.y + b.h / 2, pressed ? rgb(6, 12, 18) : styleText(b.style), f);
}

static void emit(const Btn& b) { addBtn(b); drawBtn(b); }

// Segmented ring gauge. Built from filled dots rather than fillArc so
// the geometry is explicit and identical on any GFX version.
static void gauge(int cx, int cy, int radius, float frac, uint16_t on, uint16_t off) {
  const int N = 34;
  const float start = 140.0f, sweep = 260.0f;
  frac = clampf(frac, 0.0f, 1.0f);
  int lit = (int)(frac * N + 0.5f);
  for (int i = 0; i < N; i++) {
    float a = (start + sweep * (float)i / (float)(N - 1)) * 0.0174532925f;
    int x = cx + (int)(cosf(a) * radius);
    int y = cy + (int)(sinf(a) * radius);
    gfx()->fillCircle(x, y, (i < lit) ? 3 : 2, (i < lit) ? on : off);
  }
}

static void bar(int x, int y, int w, int h, float frac, uint16_t col) {
  gfx()->fillRoundRect(x, y, w, h, h / 2, P.panel2);
  int fw = (int)(w * clampf(frac, 0.0f, 1.0f));
  if (fw > 2) gfx()->fillRoundRect(x, y, fw, h, h / 2, col);
}

// ------------------------------------------------------------------ //
//  Chrome
// ------------------------------------------------------------------ //
static void wifiGlyph(int x, int y) {
  if (g_bars > 0) {
    for (int i = 0; i < 4; i++) {
      int bh = 4 + i * 3;
      gfx()->fillRect(x + i * 4, y + 13 - bh, 3, bh, (i < g_bars) ? P.good : P.grid);
    }
  } else if (g_ap) {
    label("AP", x + 8, y + 7, P.accent2, &fonts::Font0);
  } else {
    gfx()->drawLine(x, y + 2, x + 12, y + 13, P.muted);
    gfx()->drawLine(x + 12, y + 2, x, y + 13, P.muted);
  }
}

static void topBar(const char* title, bool showBack) {
  gfx()->fillRect(0, 0, W, TOPBAR_H, P.panel);
  gfx()->drawFastHLine(0, TOPBAR_H - 1, W, P.grid);

  int tx = 12;
  if (showBack) {
    emit(Btn{ B_BACK, 4, 4, 34, 26, "<", 0 });
    tx = 46;
  }
  label(title, tx, TOPBAR_H / 2 - 1, P.text, &fonts::Font2, textdatum_t::middle_left);

  wifiGlyph(200, 9);

  // The e-stop is present on every screen, always in the same place,
  // and it cuts output on press rather than opening a menu.
  emit(Btn{ B_ESTOP, 240, 3, 76, 28, "E-STOP", 4 });
}

static void navBar() {
  gfx()->fillRect(0, NAV_Y, W, NAV_H, P.panel);
  gfx()->drawFastHLine(0, NAV_Y, W, P.grid);

  struct Tab { uint8_t id; const char* text; int scr; };
  static const Tab tabs[5] = {
    { B_NAV_HOME,   "HOME",  SCR_HOME   },
    { B_NAV_VAC,    "VAC",   SCR_VAC    },
    { B_NAV_PULSE,  "PULSE", SCR_PULSE  },
    { B_NAV_MOTOR,  "MOTOR", SCR_MOTOR  },
    { B_NAV_RHYTHM, "RHYTM", SCR_RHYTHM },
  };
  int tw = W / 5;
  for (int i = 0; i < 5; i++) {
    bool sel = (g_screen == tabs[i].scr);
    int x = i * tw;
    if (sel) {
      gfx()->fillRect(x + 4, NAV_Y + 3, tw - 8, NAV_H - 6, P.panel2);
      gfx()->fillRect(x + 4, NAV_Y + 3, tw - 8, 3, P.accent);
    }
    label(tabs[i].text, x + tw / 2, NAV_Y + NAV_H / 2, sel ? P.accent : P.muted, &fonts::Font2);
    addBtn(Btn{ tabs[i].id, (int16_t)x, (int16_t)NAV_Y, (int16_t)tw, (int16_t)NAV_H, "", 0 });
  }
}

// A labelled -/+ row. Used by every adjustable value so they all behave
// and look the same.
static void stepper(int y, int h, const char* name, const char* value,
                    uint8_t idDn, uint8_t idUp, uint16_t valCol) {
  emit(Btn{ idDn, 8, (int16_t)y, 62, (int16_t)h, "-", 0 });
  emit(Btn{ idUp, (int16_t)(W - 70), (int16_t)y, 62, (int16_t)h, "+", 0 });
  card(76, y, W - 152, h, P.panel);
  label(name, W / 2, y + 12, P.muted, &fonts::Font0);
  label(value, W / 2, y + h / 2 + 6, valCol, &fonts::Font4);
}

// ------------------------------------------------------------------ //
//  Screens
// ------------------------------------------------------------------ //
static void fmtInt(char* buf, size_t n, int v, const char* suffix) {
  snprintf(buf, n, "%d%s", v, suffix ? suffix : "");
}

static void screenHome() {
  topBar("Pluto 9000", false);

  bool run = app.engine.running();
  Outputs o = app.engine.outputs();
  char b[24];

  // Three live channel strips.
  struct Row { const char* n; int v; int max; uint16_t c; bool beat; };
  Row rows[3] = {
    { "VACUUM", (int)app.engine.vacActual(), 100,               P.accent2, false },
    { "PULSE",  app.settings.ppm,            Limits::kPpmMax,   P.accent,  true  },
    { "MOTOR",  app.settings.motor,          100,               P.warn,    false },
  };
  for (int i = 0; i < 3; i++) {
    int y = BODY_Y + 4 + i * 30;
    card(8, y, W - 16, 27, P.panel);
    label(rows[i].n, 16, y + 13, P.muted, &fonts::Font0, textdatum_t::middle_left);
    fmtInt(b, sizeof(b), rows[i].v, i == 1 ? "" : "%");
    label(b, W - 46, y + 14, P.text, &fonts::Font2, textdatum_t::middle_right);
    bar(74, y + 10, 150, 8, (float)rows[i].v / (float)rows[i].max, rows[i].c);
    if (rows[i].beat) {
      bool lit = run && o.pulseOn;
      gfx()->fillCircle(W - 22, y + 13, lit ? 6 : 3, lit ? P.accent : P.grid);
    }
  }

  // Primary action + the three secondary destinations.
  emit(Btn{ B_RUN, 8, (int16_t)(BODY_Y + 96), 150, 56, run ? "STOP" : "START", (uint8_t)(run ? 3 : 2) });
  emit(Btn{ B_GRAPH,    166, (int16_t)(BODY_Y + 96), 146, 26, "GRAPHS",  0 });
  emit(Btn{ B_NET,      166, (int16_t)(BODY_Y + 126), 70, 26, "NET",     0 });
  emit(Btn{ B_SETTINGS, 242, (int16_t)(BODY_Y + 126), 70, 26, "SET",     0 });

  navBar();
}

static void screenVac() {
  topBar("Vacuum  CH1", true);
  char b[24];

  int cx = 74, cy = BODY_Y + 46;
  gauge(cx, cy, 38, app.engine.vacActual() / 100.0f, P.accent2, P.grid);
  snprintf(b, sizeof(b), "%d", (int)app.engine.vacActual());
  label(b, cx, cy, P.text, &fonts::Font4);
  label("actual %", cx, cy + 24, P.muted, &fonts::Font0);

  card(126, BODY_Y + 6, W - 134, 80, P.panel);
  label("TARGET", 138, BODY_Y + 20, P.muted, &fonts::Font0, textdatum_t::middle_left);
  snprintf(b, sizeof(b), "%d%%", app.settings.vacTarget);
  label(b, 138, BODY_Y + 46, P.accent2, &fonts::Font4, textdatum_t::middle_left);
  snprintf(b, sizeof(b), "response %d/10", app.settings.vacRamp);
  label(b, 138, BODY_Y + 72, P.muted, &fonts::Font0, textdatum_t::middle_left);
  emit(Btn{ B_RAMP_DN, (int16_t)(W - 76), (int16_t)(BODY_Y + 60), 30, 22, "-", 5 });
  emit(Btn{ B_RAMP_UP, (int16_t)(W - 42), (int16_t)(BODY_Y + 60), 30, 22, "+", 5 });

  snprintf(b, sizeof(b), "%d%%", app.settings.vacTarget);
  stepper(BODY_Y + 94, 60, "VACUUM TARGET", b, B_VAC_DN, B_VAC_UP, P.accent2);

  navBar();
}

static void screenPulse() {
  topBar("Pulsation  CH2", true);
  char b[24];

  // Live square wave, exactly as the engine will drive it: period from
  // PPM, on-fraction from the ratio.
  int gx = 8, gy = BODY_Y + 4, gw = W - 16, gh = 40;
  card(gx, gy, gw, gh, P.panel);
  int ppm = app.settings.ppm;
  if (ppm >= Limits::kPpmMin) {
    float periodMs = 60000.0f / (float)ppm;
    float windowMs = 3000.0f;
    int prevY = gy + gh - 7;
    for (int px = 0; px < gw - 8; px++) {
      float t = (float)px / (float)(gw - 8) * windowMs;
      float ph = fmodf(t, periodMs) / periodMs;
      bool on = ph < (app.settings.ratio / 100.0f);
      int y = on ? gy + 7 : gy + gh - 7;
      gfx()->drawLine(gx + 4 + px, prevY, gx + 4 + px, y, P.accent);
      prevY = y;
    }
  } else {
    label("PULSE OFF", gx + gw / 2, gy + gh / 2, P.muted, &fonts::Font2);
  }

  bool lit = app.engine.running() && app.engine.outputs().pulseOn;
  gfx()->fillCircle(gx + gw - 16, gy + 12, lit ? 7 : 4, lit ? P.accent : P.grid);

  snprintf(b, sizeof(b), "%d", ppm);
  stepper(BODY_Y + 48, 54, "PULSES PER MINUTE", ppm ? b : "OFF", B_PPM_DN, B_PPM_UP, P.accent);

  snprintf(b, sizeof(b), "%d:%d", app.settings.ratio, 100 - app.settings.ratio);
  stepper(BODY_Y + 106, 50, "RATIO  (on:off)", b, B_RATIO_DN, B_RATIO_UP, P.accent2);

  navBar();
}

static void screenMotor() {
  topBar("Motor  CH3", true);
  char b[24];

  int cx = W / 2, cy = BODY_Y + 46;
  gauge(cx, cy, 42, app.settings.motor / 100.0f, P.warn, P.grid);
  snprintf(b, sizeof(b), "%d", app.settings.motor);
  label(b, cx, cy - 2, P.text, &fonts::Font4);
  label("% power", cx, cy + 24, P.muted, &fonts::Font0);

  Outputs o = app.engine.outputs();
  snprintf(b, sizeof(b), app.settings.vacProportional || o.motorDuty ? "pwm duty %d/255" : "output %s",
           o.motorDuty);
  if (!o.motorDuty) snprintf(b, sizeof(b), "output off");
  label(b, cx, BODY_Y + 88, P.muted, &fonts::Font0);

  snprintf(b, sizeof(b), "%d%%", app.settings.motor);
  stepper(BODY_Y + 98, 56, "MOTOR POWER", b, B_MOTOR_DN, B_MOTOR_UP, P.warn);

  navBar();
}

static void screenRhythm() {
  topBar("Rhythms  CH2", true);
  char b[40];

  card(8, BODY_Y + 4, W - 16, 56, P.panel);

  bool on = app.settings.rhythmId != 0;
  bool lit = on && app.engine.running() && app.engine.outputs().pulseOn;
  gfx()->fillCircle(38, BODY_Y + 32, lit ? 13 : 8, lit ? P.accent : P.grid);
  gfx()->drawCircle(38, BODY_Y + 32, 17, on ? P.accent : P.grid);

  label(rhythmLabel(app.settings.rhythmId), 68, BODY_Y + 18, P.text, &fonts::Font2, textdatum_t::middle_left);
  const char* nota = rhythmNotation(app.settings.rhythmId);
  char shown[30];
  strncpy(shown, nota, sizeof(shown) - 1); shown[sizeof(shown) - 1] = '\0';
  label(shown, 68, BODY_Y + 36, P.warn, &fonts::Font0, textdatum_t::middle_left);
  snprintf(b, sizeof(b), "id %d  -  speed %d%%  -  CH2 only",
           app.settings.rhythmId, app.settings.rhythmSpeed);
  label(b, 68, BODY_Y + 50, P.muted, &fonts::Font0, textdatum_t::middle_left);

  emit(Btn{ B_RHY_PREV,   8, (int16_t)(BODY_Y + 66), 62, 40, "PREV", 0 });
  emit(Btn{ B_RHY_TOGGLE, 76, (int16_t)(BODY_Y + 66), 168, 40, on ? "STOP RHYTHM" : "PLAY RHYTHM", (uint8_t)(on ? 3 : 2) });
  emit(Btn{ B_RHY_NEXT,   (int16_t)(W - 70), (int16_t)(BODY_Y + 66), 62, 40, "NEXT", 0 });

  emit(Btn{ B_RHY_SLOW, 8, (int16_t)(BODY_Y + 112), 62, 40, "SLOW", 0 });
  card(76, BODY_Y + 112, W - 152, 40, P.panel);
  snprintf(b, sizeof(b), "%d%%", app.settings.rhythmSpeed);
  label("SPEED", W / 2 - 30, BODY_Y + 132, P.muted, &fonts::Font0);
  label(b, W / 2 + 24, BODY_Y + 132, P.text, &fonts::Font2);
  emit(Btn{ B_RHY_FAST, (int16_t)(W - 70), (int16_t)(BODY_Y + 112), 62, 40, "FAST", 0 });

  navBar();
}

static void screenGraph() {
  topBar("Live graphs", true);

  int gx = 8, gy = BODY_Y + 4, gw = W - 16, gh = BODY_H - 12;
  card(gx, gy, gw, gh, P.panel);
  for (int i = 1; i < 4; i++) {
    int y = gy + gh * i / 4;
    gfx()->drawFastHLine(gx + 4, y, gw - 8, P.grid);
  }

  int px0 = gx + 4, pw = gw - 8;
  int lvx = px0, lvy = 0, lmx = px0, lmy = 0, lpx = px0, lpy = 0;
  for (int i = 0; i < HIST; i++) {
    int idx = (g_hIdx + i) % HIST;
    int x = px0 + pw * i / (HIST - 1);
    int yv = gy + gh - 4 - (g_hVac[idx] * (gh - 10) / 100);
    int ym = gy + gh - 4 - (g_hMotor[idx] * (gh - 10) / 100);
    int yp = gy + gh - 4 - ((int)g_hPpm[idx] * (gh - 10) / Limits::kPpmMax);
    if (i) {
      gfx()->drawLine(lvx, lvy, x, yv, P.accent2);
      gfx()->drawLine(lmx, lmy, x, ym, P.warn);
      gfx()->drawLine(lpx, lpy, x, yp, P.accent);
    }
    lvx = x; lvy = yv; lmx = x; lmy = ym; lpx = x; lpy = yp;
  }

  label("vac", gx + 10, gy + 10, P.accent2, &fonts::Font0, textdatum_t::middle_left);
  label("ppm", gx + 44, gy + 10, P.accent, &fonts::Font0, textdatum_t::middle_left);
  label("motor", gx + 80, gy + 10, P.warn, &fonts::Font0, textdatum_t::middle_left);

  char b[40];
  snprintf(b, sizeof(b), "%lu pulses  %lus", (unsigned long)app.engine.pulseCount(),
           (unsigned long)app.engine.runSeconds(hal::nowUs()));
  label(b, gx + gw - 10, gy + 10, P.muted, &fonts::Font0, textdatum_t::middle_right);

  navBar();
}

static void screenNet() {
  topBar("Network", true);
  char b[80];

  card(8, BODY_Y + 4, W - 16, 84, P.panel);
  label("NETWORK", 18, BODY_Y + 16, P.muted, &fonts::Font0, textdatum_t::middle_left);
  label(g_ssid, 18, BODY_Y + 34, P.accent2, &fonts::Font2, textdatum_t::middle_left);
  label(g_ip,   18, BODY_Y + 56, P.accent,  &fonts::Font2, textdatum_t::middle_left);
  label(g_mode, 18, BODY_Y + 76, P.muted,   &fonts::Font0, textdatum_t::middle_left);

  // The setup AP password and web PIN are shown here and nowhere else.
  card(8, BODY_Y + 92, W - 16, 32, P.panel2);
  snprintf(b, sizeof(b), "AP %s  key %s  PIN %s", app.apSsid, app.apPass, app.pin);
  label(b, W / 2, BODY_Y + 108, P.text, &fonts::Font0);

  emit(Btn{ B_NET_AP,  8, (int16_t)(BODY_Y + 128), 148, 28, "SETUP AP", 0 });
  emit(Btn{ B_NET_WEB, 164, (int16_t)(BODY_Y + 128), 148, 28,
            app.settings.webEnabled ? "WEB: ON" : "WEB: OFF",
            (uint8_t)(app.settings.webEnabled ? 2 : 0) });

  navBar();
}

static void screenSettings() {
  topBar("Settings", true);
  char b[40];

  card(8, BODY_Y + 4, W - 16, 34, P.panel);
  label("Theme", 18, BODY_Y + 21, P.text, &fonts::Font2, textdatum_t::middle_left);
  emit(Btn{ B_THEME, (int16_t)(W - 108), (int16_t)(BODY_Y + 8), 96, 26,
            app.settings.darkTheme ? "DARK" : "LIGHT", 0 });

  card(8, BODY_Y + 42, W - 16, 34, P.panel);
  snprintf(b, sizeof(b), "Backlight  %d/10", app.settings.backlight);
  label(b, 18, BODY_Y + 59, P.text, &fonts::Font2, textdatum_t::middle_left);
  emit(Btn{ B_BL_DN, (int16_t)(W - 108), (int16_t)(BODY_Y + 46), 44, 26, "-", 0 });
  emit(Btn{ B_BL_UP, (int16_t)(W - 58),  (int16_t)(BODY_Y + 46), 44, 26, "+", 0 });

  card(8, BODY_Y + 80, W - 16, 34, P.panel);
  if (app.settings.runLimitMin > 0) snprintf(b, sizeof(b), "Run limit  %d min", app.settings.runLimitMin);
  else                              snprintf(b, sizeof(b), "Run limit  OFF");
  label(b, 18, BODY_Y + 97, P.text, &fonts::Font2, textdatum_t::middle_left);
  emit(Btn{ B_LIMIT_DN, (int16_t)(W - 108), (int16_t)(BODY_Y + 84), 44, 26, "-", 0 });
  emit(Btn{ B_LIMIT_UP, (int16_t)(W - 58),  (int16_t)(BODY_Y + 84), 44, 26, "+", 0 });

  card(8, BODY_Y + 118, W - 16, 34, P.panel);
  label("Web PIN required", 18, BODY_Y + 135, P.text, &fonts::Font2, textdatum_t::middle_left);
  emit(Btn{ B_AUTH, (int16_t)(W - 108), (int16_t)(BODY_Y + 122), 96, 26,
            app.settings.authRequired ? "ON" : "OFF",
            (uint8_t)(app.settings.authRequired ? 2 : 3) });

  navBar();
}

static void screenRecord() {
  topBar("Record rhythm", true);
  char b[48];

  card(8, BODY_Y + 4, W - 16, 46, P.panel);
  snprintf(b, sizeof(b), "Slot %d  -  %s", g_recSlot + 1, app.custom[g_recSlot].name);
  label(b, 18, BODY_Y + 20, P.text, &fonts::Font2, textdatum_t::middle_left);
  const char* d = app.custom[g_recSlot].data[0] ? app.custom[g_recSlot].data : "(empty)";
  char shown[38];
  strncpy(shown, d, sizeof(shown) - 1); shown[sizeof(shown) - 1] = '\0';
  label(shown, 18, BODY_Y + 38, P.muted, &fonts::Font0, textdatum_t::middle_left);

  emit(Btn{ B_REC_SLOT, (int16_t)(W - 66), (int16_t)(BODY_Y + 10), 56, 32, "SLOT", 0 });

  emit(Btn{ B_REC_ARM, 8, (int16_t)(BODY_Y + 56), 96, 32,
            app.recording ? "REC ON" : "ARM", (uint8_t)(app.recording ? 4 : 0) });
  emit(Btn{ B_REC_SAVE,  110, (int16_t)(BODY_Y + 56), 96, 32, "SAVE",  2 });
  emit(Btn{ B_REC_CLEAR, 212, (int16_t)(BODY_Y + 56), 100, 32, "CLEAR", 0 });

  // The big pad: press and hold to record a pulse of that length.
  emit(Btn{ B_REC_TAP, 8, (int16_t)(BODY_Y + 94), (int16_t)(W - 16), 60,
            app.recording ? "TAP / HOLD" : "ARM FIRST", (uint8_t)(app.recording ? 1 : 5) });

  navBar();
}

static void screenEstopConfirm() {
  gfx()->fillScreen(P.danger);
  label("OUTPUTS OFF", W / 2, 44, rgb(255, 255, 255), &fonts::Font4);
  label("All three channels are de-energised.", W / 2, 82, rgb(255, 235, 235), &fonts::Font2);
  label("Release residual pressure?", W / 2, 106, rgb(255, 235, 235), &fonts::Font2);
  emit(Btn{ B_ES_RELEASE, 12, 130, 296, 50, "RELEASE PRESSURE", 5 });
  emit(Btn{ B_ES_CANCEL,  12, 188, 296, 42, "BACK (stay latched)", 0 });
}

static void screenEstopActive() {
  gfx()->fillScreen(P.danger);
  label("RELEASED", W / 2, 52, rgb(255, 255, 255), &fonts::Font4);
  label("Positive and negative pressure off.", W / 2, 92, rgb(255, 235, 235), &fonts::Font2);
  label("E-STOP stays latched until you return.", W / 2, 116, rgb(255, 235, 235), &fonts::Font2);
  emit(Btn{ B_ES_RETURN, 12, 150, 296, 60, "CLEAR E-STOP AND RETURN", 5 });
}

// ------------------------------------------------------------------ //
//  Paint
// ------------------------------------------------------------------ //
static void paint() {
  loadPalette();
  clearBtns();
  gfx()->fillScreen(P.bg);

  switch (g_screen) {
    case SCR_VAC:           screenVac(); break;
    case SCR_PULSE:         screenPulse(); break;
    case SCR_MOTOR:         screenMotor(); break;
    case SCR_RHYTHM:        screenRhythm(); break;
    case SCR_GRAPH:         screenGraph(); break;
    case SCR_NET:           screenNet(); break;
    case SCR_SETTINGS:      screenSettings(); break;
    case SCR_RECORD:        screenRecord(); break;
    case SCR_ESTOP_CONFIRM: screenEstopConfirm(); break;
    case SCR_ESTOP_ACTIVE:  screenEstopActive(); break;
    default:                screenHome(); break;
  }

  // A single status line along the bottom of the body, above the nav.
  if (g_screen != SCR_ESTOP_CONFIRM && g_screen != SCR_ESTOP_ACTIVE) {
    if (app.statusMsg[0] && g_screen == SCR_HOME) {
      label(app.statusMsg, W / 2, NAV_Y - 8, P.muted, &fonts::Font0);
    }
  }

  if (g_haveCanvas) cv.pushSprite(0, 0);
}

void invalidate() { g_dirty = true; }

void setScreen(int s) {
  if (s == g_screen) return;
  g_prevScr = g_screen;
  g_screen = s;
  g_hold = B_NONE;
  g_dirty = true;
}

int screen() { return g_screen; }

void setNetLines(const char* ssid, const char* ip, const char* mode, int bars, bool ap) {
  bool changed = strcmp(g_ssid, ssid) || strcmp(g_ip, ip) || strcmp(g_mode, mode) ||
                 bars != g_bars || ap != g_ap;
  strncpy(g_ssid, ssid, sizeof(g_ssid) - 1); g_ssid[sizeof(g_ssid) - 1] = 0;
  strncpy(g_ip, ip, sizeof(g_ip) - 1);       g_ip[sizeof(g_ip) - 1] = 0;
  strncpy(g_mode, mode, sizeof(g_mode) - 1); g_mode[sizeof(g_mode) - 1] = 0;
  g_bars = bars; g_ap = ap;
  if (changed) g_dirty = true;
}

// ------------------------------------------------------------------ //
//  Touch
// ------------------------------------------------------------------ //
static const Btn* findBtn(int x, int y) {
  for (int i = 0; i < g_btnCount; i++) {
    const Btn& b = g_btns[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) return &b;
  }
  return nullptr;
}

static void applyStep(uint8_t id, int step) {
  switch (id) {
    case B_VAC_DN:   cmdNudgeVac(-step, "screen"); break;
    case B_VAC_UP:   cmdNudgeVac(step, "screen"); break;
    case B_PPM_DN:   cmdNudgePpm(-step, "screen"); break;
    case B_PPM_UP:   cmdNudgePpm(step, "screen"); break;
    case B_RATIO_DN: cmdNudgeRatio(-1, "screen"); break;
    case B_RATIO_UP: cmdNudgeRatio(1, "screen"); break;
    case B_MOTOR_DN: cmdNudgeMotor(-1, "screen"); break;
    case B_MOTOR_UP: cmdNudgeMotor(1, "screen"); break;
    case B_RAMP_DN:  cmdSetRamp(app.settings.vacRamp - 1, "screen"); break;
    case B_RAMP_UP:  cmdSetRamp(app.settings.vacRamp + 1, "screen"); break;
    case B_BL_DN:    cmdNudgeBacklight(-1, "screen"); applyBacklight(); break;
    case B_BL_UP:    cmdNudgeBacklight(1, "screen");  applyBacklight(); break;
    case B_RHY_SLOW: cmdNudgeRhythmSpeed(-25, "screen"); break;
    case B_RHY_FAST: cmdNudgeRhythmSpeed(25, "screen"); break;
    case B_LIMIT_DN: {
      int v = app.settings.runLimitMin;
      v = (v <= 0) ? 0 : (v <= 5 ? 0 : v - 5);
      app.settings.runLimitMin = v; app.settings.validate(); applyEngineSettings(); markSettingsDirty();
      break;
    }
    case B_LIMIT_UP: {
      int v = app.settings.runLimitMin + 5;
      app.settings.runLimitMin = clampi(v, 0, Limits::kRunLimitMax);
      app.settings.validate(); applyEngineSettings(); markSettingsDirty();
      break;
    }
    default: break;
  }
  g_dirty = true;
}

static void onPress(uint8_t id) {
  switch (id) {
    // --- chrome ---------------------------------------------------
    case B_ESTOP:
      cmdEstop("screen");
      setScreen(SCR_ESTOP_CONFIRM);
      return;
    case B_BACK:       setScreen(g_screen == SCR_RECORD ? SCR_RHYTHM : SCR_HOME); return;
    case B_NAV_HOME:   setScreen(SCR_HOME); return;
    case B_NAV_VAC:    setScreen(SCR_VAC); return;
    case B_NAV_PULSE:  setScreen(SCR_PULSE); return;
    case B_NAV_MOTOR:  setScreen(SCR_MOTOR); return;
    case B_NAV_RHYTHM: setScreen(SCR_RHYTHM); return;

    // --- home -----------------------------------------------------
    case B_RUN:
      if (app.engine.running()) cmdStop("screen", STOP_OPERATOR);
      else                      cmdStart("screen");
      g_dirty = true; return;
    case B_GRAPH:    setScreen(SCR_GRAPH); return;
    case B_NET:      setScreen(SCR_NET); return;
    case B_SETTINGS: setScreen(SCR_SETTINGS); return;

    // --- rhythm ---------------------------------------------------
    case B_RHY_PREV: {
      int id = app.settings.rhythmId;
      int next = (id <= 1) ? kBuiltinCount : id - 1;
      if (isCustom(id)) next = (customSlot(id) == 0) ? kBuiltinCount : id - 1;
      cmdSetRhythm(next, "screen"); g_dirty = true; return;
    }
    case B_RHY_NEXT: {
      int id = app.settings.rhythmId;
      int next;
      if (id == 0 || id < kBuiltinCount) next = id + 1;
      else if (id == kBuiltinCount)      next = kCustomBase;
      else if (customSlot(id) >= 0 && customSlot(id) < Limits::kRhythmSlots - 1) next = id + 1;
      else next = 1;
      cmdSetRhythm(next, "screen"); g_dirty = true; return;
    }
    case B_RHY_TOGGLE:
      if (app.settings.rhythmId != 0) cmdSetRhythm(0, "screen");
      else {
        int f = app.settings.fav[0] ? app.settings.fav[0] : 3;
        cmdSetRhythm(f, "screen");
        if (!app.engine.running()) cmdStart("screen");
      }
      g_dirty = true; return;
    case B_RHY_REC: setScreen(SCR_RECORD); return;

    // --- recorder --------------------------------------------------
    case B_REC_SLOT:
      g_recSlot = (g_recSlot + 1) % Limits::kRhythmSlots;
      g_dirty = true; return;
    case B_REC_ARM:
      if (app.recording) cmdRecordStop();
      else               cmdRecordStart(g_recSlot, "screen");
      g_dirty = true; return;
    case B_REC_SAVE:  cmdRecordSave(g_recSlot, "screen");  g_dirty = true; return;
    case B_REC_CLEAR: cmdRecordClear(g_recSlot, "screen"); g_dirty = true; return;
    case B_REC_TAP:   cmdRecordDown(millis()); return;    // release handled below

    // --- settings --------------------------------------------------
    case B_THEME:
      cmdSetTheme(!app.settings.darkTheme, "screen");
      g_dirty = true; return;
    case B_AUTH:
      app.settings.authRequired = !app.settings.authRequired;
      markSettingsDirty();
      setStatus(app.settings.authRequired ? "Web PIN required" : "Web PIN disabled");
      g_dirty = true; return;

    // --- network ---------------------------------------------------
    case B_NET_AP:  app.reqWebRestart = true; setStatus("Restarting setup AP"); g_dirty = true; return;
    case B_NET_WEB:
      app.settings.webEnabled = !app.settings.webEnabled;
      markSettingsDirty();
      app.reqWebRestart = true;
      g_dirty = true; return;

    // --- e-stop ----------------------------------------------------
    case B_ES_RELEASE:
      // Outputs are already off; this is the explicit pressure dump.
      hal::allOff();
      log::event("estop-release");
      setScreen(SCR_ESTOP_ACTIVE); return;
    case B_ES_CANCEL:  setScreen(SCR_HOME); return;
    case B_ES_RETURN:
      cmdClearEstop("screen");
      setScreen(SCR_HOME); return;

    default: break;
  }

  if (isHoldable(id)) {
    g_hold = id;
    g_holdStart = millis();
    g_holdLast = g_holdStart;
    applyStep(id, 1);
  }
}

void handleTouch() {
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    const Btn* b = findBtn(t.x, t.y);
    if (b) onPress(b->id);
    return;
  }

  if (t.isPressed() && g_hold != B_NONE) {
    uint32_t now = millis();
    uint32_t held = now - g_holdStart;
    uint32_t gap = (held < FIRST_REPEAT_MS) ? FIRST_REPEAT_MS : REPEAT_MS;
    if (now - g_holdLast >= gap) {
      g_holdLast = now;
      int step = 1;
      if (held >= TURBO_AFTER_MS) step = 10;
      else if (held >= FAST_AFTER_MS) step = 5;
      applyStep(g_hold, step);
    }
  }

  if (t.wasReleased()) {
    if (g_hold != B_NONE) {
      g_hold = B_NONE;
      saveSettingsNow("screen");
      g_dirty = true;
    }
    // The record pad measures press duration, so the release matters.
    if (g_screen == SCR_RECORD && app.recording) {
      const Btn* b = findBtn(t.x, t.y);
      if (b && b->id == B_REC_TAP) { cmdRecordUp(millis()); g_dirty = true; }
    }
  }
}

// ------------------------------------------------------------------ //
//  Frame scheduling
// ------------------------------------------------------------------ //
void tick() {
  uint32_t now = millis();

  // Sample the live history at a fixed rate regardless of the screen so
  // the graph is already populated when the operator opens it.
  if (now - g_lastHist >= 250) {
    g_lastHist = now;
    g_hVac[g_hIdx]   = (uint8_t)clampi((int)app.engine.vacActual(), 0, 100);
    g_hMotor[g_hIdx] = (uint8_t)clampi(app.settings.motor, 0, 100);
    g_hPpm[g_hIdx]   = (uint16_t)clampi(app.settings.ppm, 0, Limits::kPpmMax);
    g_hIdx = (g_hIdx + 1) % HIST;
  }

  // Screens with live content refresh on a cadence; static ones only on
  // demand. Either way the whole frame is composed off-screen and
  // pushed in one operation, so there is never a partial frame visible.
  uint32_t interval = 500;
  switch (g_screen) {
    case SCR_HOME:  case SCR_PULSE: case SCR_RHYTHM: interval = 120; break;
    case SCR_VAC:   case SCR_MOTOR: interval = 160; break;
    case SCR_GRAPH: interval = 250; break;
    case SCR_RECORD: interval = 200; break;
    default: interval = 800; break;
  }

  if (g_dirty || (now - g_lastPaint) >= interval) {
    g_dirty = false;
    g_lastPaint = now;
    paint();
  }
}

void applyBacklight() {
  M5.Display.setBrightness(map(app.settings.backlight, 1, 10, 30, 255));
}

// ------------------------------------------------------------------ //
void begin() {
  loadPalette();
  M5.Display.setRotation(1);
  applyBacklight();

  // Full-frame canvas in PSRAM. If the allocation fails we fall back to
  // drawing straight to the panel rather than refusing to boot.
  cv.setPsram(true);
  cv.setColorDepth(16);
  g_haveCanvas = cv.createSprite(W, H) != nullptr;
  if (!g_haveCanvas) {
    cv.setPsram(false);
    cv.setColorDepth(8);
    g_haveCanvas = cv.createSprite(W, H) != nullptr;
  }

  memset(g_hVac, 0, sizeof(g_hVac));
  memset(g_hMotor, 0, sizeof(g_hMotor));
  memset(g_hPpm, 0, sizeof(g_hPpm));
  g_dirty = true;
}

// ------------------------------------------------------------------ //
//  Boot animation -- the Milky mascot, kept, but tightened up and drawn
//  through the same canvas so it does not flicker either.
// ------------------------------------------------------------------ //
static void milky(int cx, int cy, int eye, int spurt) {
  uint16_t white = rgb(255, 255, 255);
  uint16_t rim   = rgb(180, 205, 230);
  uint16_t blue  = rgb(45, 140, 210);

  cv.fillRoundRect(cx - 28, cy - 42, 56, 82, 13, white);
  cv.drawRoundRect(cx - 28, cy - 42, 56, 82, 13, rim);
  cv.fillRoundRect(cx - 17, cy - 63, 34, 26, 8, rgb(235, 245, 255));
  cv.drawRoundRect(cx - 17, cy - 63, 34, 26, 8, rim);
  cv.fillRect(cx - 14, cy - 70, 28, 9, blue);

  if (cx > 36 && cx < 284) {
    cv.fillRoundRect(cx - 23, cy + 4, 46, 21, 6, blue);
    cv.setFont(&fonts::Font2);
    cv.setTextDatum(textdatum_t::middle_center);
    cv.setTextColor(white);
    cv.drawString("milky", cx, cy + 15);
  }

  cv.fillCircle(cx - 11, cy - 21, 7, rgb(0, 0, 0));
  cv.fillCircle(cx + 11, cy - 21, 7, rgb(0, 0, 0));
  cv.fillCircle(cx - 11 + eye, cy - 21, 3, white);
  cv.fillCircle(cx + 11 + eye, cy - 21, 3, white);

  if (spurt > 0) {
    cv.drawLine(cx + 28, cy, cx + 28 + spurt, cy - 3, white);
    cv.fillCircle(cx + 28 + spurt, cy - 3, 4, white);
  }
}

void bootAnimation() {
  if (!g_haveCanvas) return;
  loadPalette();

  for (int x = -60; x <= 160; x += 22) {
    cv.fillSprite(P.bg);
    milky(x, 120, 0, 0);
    cv.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS(24));
  }
  for (int i = 0; i < 16; i++) {
    cv.fillSprite(P.bg);
    milky(160, 120, (i / 4) % 2 ? 4 : -4, 0);
    cv.setFont(&fonts::Font4);
    cv.setTextDatum(textdatum_t::middle_center);
    cv.setTextColor(P.accent2);
    cv.drawString("PULSEFEED", 160, 200);
    cv.setFont(&fonts::Font2);
    cv.setTextColor(P.muted);
    cv.drawString(kVersion, 160, 224);
    cv.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS(34));
  }
  for (int x = 160; x < 400; x += 26) {
    cv.fillSprite(P.bg);
    milky(x, 120, 0, x < 260 ? 26 : 0);
    cv.pushSprite(0, 0);
    vTaskDelay(pdMS_TO_TICKS(26));
  }
  cv.fillSprite(P.bg);
  cv.pushSprite(0, 0);
}

}  // namespace ui
}  // namespace pf
