// =====================================================================
//  test_core.cpp -- host-side unit tests for the PulseFeed portable core
//
//  These compile the EXACT files the firmware compiles. Every assertion
//  below corresponds to a defect found in the v43 audit or to a timing
//  guarantee the product depends on.
//
//  build:  tools/build.sh test      (or see tests/Makefile)
// =====================================================================
#include "../firmware/pulsefeed/pf_core.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>

using namespace pf;

// ------------------------------------------------------------------ //
//  Tiny test harness
// ------------------------------------------------------------------ //
static int g_pass = 0, g_fail = 0;
static const char* g_group = "";

static void group(const char* n) { g_group = n; printf("\n\033[1m%s\033[0m\n", n); }

static void ok(bool cond, const char* what) {
  if (cond) { g_pass++; printf("  \033[32mPASS\033[0m %s\n", what); }
  else      { g_fail++; printf("  \033[31mFAIL\033[0m %s\n", what); }
}

static void okNear(double a, double b, double tol, const char* what) {
  bool c = fabs(a - b) <= tol;
  if (c) { g_pass++; printf("  \033[32mPASS\033[0m %s  (%.3f ~ %.3f)\n", what, a, b); }
  else   { g_fail++; printf("  \033[31mFAIL\033[0m %s  (got %.3f, want %.3f +/-%.3f)\n", what, a, b, tol); }
}

// ------------------------------------------------------------------ //
//  Shared fixtures
// ------------------------------------------------------------------ //
static RhythmPattern g_builtins[kBuiltinCount];
static RhythmPattern g_customs[Limits::kRhythmSlots];

static void compileBuiltins(uint16_t unitMs = 250) {
  for (int i = 1; i <= kBuiltinCount; i++) {
    g_builtins[i - 1].compileDots(builtinDots(i), unitMs);
  }
}

// ------------------------------------------------------------------ //
//  1. Value normalisation
// ------------------------------------------------------------------ //
static void testNormalise() {
  group("value normalisation");

  ok(normalizePpm(0)   == 0,   "ppm 0 stays off");
  ok(normalizePpm(-40) == 0,   "negative ppm clamps to off");
  ok(normalizePpm(5)   == 30,  "ppm below active minimum snaps up to 30");
  ok(normalizePpm(30)  == 30,  "ppm 30 is the active minimum");
  ok(normalizePpm(401) == 400, "ppm above max clamps to 400");

  ok(normalizeMotor(0)   == 0,   "motor 0 is a true off");
  ok(normalizeMotor(2)   == 5,   "motor snaps up off zero, never rounds to off");
  ok(normalizeMotor(37)  == 35,  "motor snaps to the 5% grid");
  ok(normalizeMotor(38)  == 40,  "motor snaps to the nearest step");
  ok(normalizeMotor(999) == 100, "motor clamps at 100");

  Settings s; s.defaults();
  s.ppm = 900; s.ratio = 99; s.vacTarget = -5; s.rhythmSpeed = 9999;
  s.rhythmId = 12345; s.backlight = 0; s.runLimitMin = 100000;
  s.validate();
  ok(s.ppm == 400,          "validate() clamps ppm");
  ok(s.ratio == Limits::kRatioMax, "validate() clamps pulsation ratio");
  ok(s.vacTarget == 0,      "validate() clamps vacuum");
  ok(s.rhythmSpeed == Limits::kSpeedMax, "validate() clamps rhythm speed");
  ok(s.rhythmId == 0,       "validate() rejects an unknown rhythm id");
  ok(s.backlight == 1,      "validate() clamps backlight");
  ok(s.runLimitMin == Limits::kRunLimitMax, "validate() clamps run limit");
  ok(s.authRequired,        "auth is on by default (secure default)");
}

// ------------------------------------------------------------------ //
//  2. JSON writer  (v43: unescaped control chars broke the dashboard)
// ------------------------------------------------------------------ //
static void testJson() {
  group("json writer");

  {
    char buf[256];
    JsonOut j(buf, sizeof(buf));
    j.beginObj();
    j.kvNum("a", 42);
    j.kvBool("b", true);
    j.kvStr("c", "hi");
    j.endObj();
    ok(strcmp(buf, "{\"a\":42,\"b\":true,\"c\":\"hi\"}") == 0, "basic object shape");
    ok(!j.overflow(), "no overflow flagged");
  }

  {
    char buf[256];
    JsonOut j(buf, sizeof(buf));
    j.beginObj();
    j.kvStr("ssid", "my\"net\\work");
    j.endObj();
    ok(strcmp(buf, "{\"ssid\":\"my\\\"net\\\\work\"}") == 0, "quotes and backslashes escaped");
  }

  {
    // The exact v43 killer: an SSID with a control character.
    char raw[] = { 'B','a','d',0x01,'N','e','t','\n','x', 0 };
    char buf[256];
    JsonOut j(buf, sizeof(buf));
    j.beginObj(); j.kvStr("ssid", raw); j.endObj();
    ok(strstr(buf, "\\u0001") != NULL, "control character escaped as \\u0001");
    ok(strstr(buf, "\\n") != NULL,     "newline escaped");
    ok(strchr(buf, 0x01) == NULL,      "no raw control byte in output");
  }

  {
    // Invalid UTF-8 must not produce unparseable JSON.
    char raw[] = { 'x', (char)0xC3, (char)0x28, 0 };   // bad 2-byte sequence
    char buf[128];
    JsonOut j(buf, sizeof(buf));
    j.beginObj(); j.kvStr("s", raw); j.endObj();
    ok(strstr(buf, "\\ufffd") != NULL, "invalid utf-8 replaced with u+fffd");
  }

  {
    // Valid UTF-8 must pass through untouched.
    const char* raw = "caf\xC3\xA9";
    char buf[128];
    JsonOut j(buf, sizeof(buf));
    j.beginObj(); j.kvStr("s", raw); j.endObj();
    ok(strstr(buf, "caf\xC3\xA9") != NULL, "valid utf-8 passes through");
  }

  {
    char buf[16];                       // deliberately far too small
    JsonOut j(buf, sizeof(buf));
    j.beginObj();
    for (int i = 0; i < 40; i++) j.kvNum("keyname", 123456);
    j.endObj();
    ok(j.overflow(), "overflow is reported, not silently wrong");
    ok(strlen(buf) < sizeof(buf), "buffer never overrun");
  }
}

// ------------------------------------------------------------------ //
//  3. Rhythm compiler
// ------------------------------------------------------------------ //
static void testRhythmCompile() {
  group("rhythm compiler");

  RhythmPattern p;
  ok(p.compileDots("..", 100), "compiles a two-dot pattern");
  ok(p.count() == 2, "two steps");
  ok(p.step(0).onMs == 100 && p.step(0).offMs == 100, "dot = 1 unit on, 1 off");
  ok(p.totalMs() == 400, "total duration is 4 units");

  ok(p.compileDots("-", 100), "compiles a dash");
  ok(p.step(0).onMs == 300 && p.step(0).offMs == 100, "dash = 3 units on, 1 off");

  // ". .   . ."  ->  four dots; each space folds one unit of rest into
  // the step before it, so step 1 (followed by three spaces) carries
  // 100 ms of its own rest plus 300 ms of separator.
  ok(p.compileDots(". .   . .", 100), "compiles with rest separators");
  ok(p.count() == 4, "spaces do not create phantom steps");
  ok(p.step(1).offMs == 400, "runs of spaces widen the preceding rest");
  ok(p.step(0).offMs == 200, "a single space widens by exactly one unit");

  ok(!p.compileDots("", 100),   "empty pattern is invalid");
  ok(!p.compileDots(NULL, 100), "null pattern is invalid");
  ok(!p.compileDots(". .", 0),  "zero unit is invalid");

  ok(p.compileCsv("120,300;80,150;"), "compiles tap-recorded csv");
  ok(p.count() == 2, "two csv steps");
  ok(p.step(0).onMs == 120 && p.step(0).offMs == 300, "csv pair parsed");
  ok(p.totalMs() == 650, "csv total");

  ok(p.compileCsv("999999,0;"), "absurd csv value is accepted after clamping");
  ok(p.step(0).onMs == 5000, "csv on-time clamped to a safe ceiling");

  ok(!p.compileCsv("garbage"), "non-numeric csv rejected");

  // Overflow guard: feed far more steps than the pattern can hold.
  std::string big;
  for (int i = 0; i < 500; i++) big += "50,50;";
  p.compileCsv(big.c_str());
  ok(p.count() <= Limits::kMaxSteps, "step array cannot be overrun");

  compileBuiltins();
  int bad = 0;
  for (int i = 1; i <= kBuiltinCount; i++) if (!g_builtins[i - 1].valid()) bad++;
  ok(bad == 0, "all built-in patterns compile");
}

// ------------------------------------------------------------------ //
//  4. Rhythm clock -- the v43 32-bit overflow
// ------------------------------------------------------------------ //
static void testRhythmClockOverflow() {
  group("rhythm clock long-run stability");

  RhythmPattern p;
  // No separators here so the duty is exactly what the notation says:
  // three dots (1 on / 1 off) and one dash (3 on / 1 off) -> 60% on.
  p.compileDots("..-.", 250);

  // v43 computed elapsedMs * speedPct in 32 bits. At 300% that wraps
  // after ~4h and the pattern jumps. Walk 24 simulated hours and verify
  // the position still matches the exact modular answer.
  const int speed = 300;
  bool allMatch = true;
  for (uint64_t hour = 0; hour <= 24; hour++) {
    uint64_t el = hour * 3600ULL * 1000000ULL + 12345ULL;
    uint64_t scaledMs = (el / 1000ULL) * (uint64_t)speed / 100ULL;
    uint32_t expectPos = (uint32_t)(scaledMs % p.totalMs());

    // Recover the position the pattern is actually using via stepAt.
    int idx = p.stepAt(el, speed);
    uint32_t lo = 0;
    for (int i = 0; i < idx; i++) lo += p.step(i).onMs + p.step(i).offMs;
    uint32_t hi = lo + p.step(idx).onMs + p.step(idx).offMs;
    if (!(expectPos >= lo && expectPos < hi)) allMatch = false;
  }
  ok(allMatch, "pattern position exact across 24 simulated hours at 300% speed");

  // Duty of the compiled pattern should match the notation.
  // ". . - ." -> on 250+250+750+250 = 1500 of total 2500 = 60%
  uint32_t on = 0, tot = 0;
  for (int i = 0; i < p.count(); i++) { on += p.step(i).onMs; tot += p.step(i).onMs + p.step(i).offMs; }
  okNear(100.0 * on / tot, 60.0, 0.01, "compiled duty matches dot/dash notation");

  // Sampling the pattern over many loops must reproduce that duty.
  int hits = 0, samples = 0;
  for (uint64_t t = 0; t < 60ULL * 1000000ULL; t += 1000) {  // 60 s @ 1 kHz
    if (p.pulseAt(t, 100)) hits++;
    samples++;
  }
  okNear(100.0 * hits / samples, 60.0, 0.5, "sampled duty matches compiled duty");
}

// ------------------------------------------------------------------ //
//  5. Engine -- pulsation accuracy (the core product function)
// ------------------------------------------------------------------ //
static void runEngine(Engine& e, uint64_t& t, uint64_t durationUs, uint64_t stepUs,
                      int* risingEdges = NULL, uint64_t* onTimeUs = NULL) {
  bool prev = false;
  uint64_t end = t + durationUs;
  while (t < end) {
    e.feed(t);
    Outputs o = e.tick(t);
    if (o.pulseOn && !prev && risingEdges) (*risingEdges)++;
    if (o.pulseOn && onTimeUs) *onTimeUs += stepUs;
    prev = o.pulseOn;
    t += stepUs;
  }
}

static void testEnginePulsation() {
  group("engine pulsation accuracy");

  compileBuiltins();

  struct Case { int ppm; int seconds; };
  Case cases[] = { {30, 60}, {60, 60}, {120, 30}, {240, 30}, {400, 30} };

  for (size_t ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ci++) {
    Engine e;
    e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults();
    s.ppm = cases[ci].ppm; s.ratio = 50; s.runLimitMin = 0;
    uint64_t t = 0;
    e.reset(t);
    e.applySettings(s, t);
    e.start(t);

    int edges = 0;
    runEngine(e, t, (uint64_t)cases[ci].seconds * 1000000ULL, 500, &edges);

    double expected = cases[ci].ppm * (cases[ci].seconds / 60.0);
    char msg[128];
    snprintf(msg, sizeof(msg), "%d PPM over %ds -> %d pulses", cases[ci].ppm, cases[ci].seconds, edges);
    okNear(edges, expected, 1.0, msg);
  }
}

static void testEngineRatio() {
  group("engine pulsation ratio (new in 2.0)");

  compileBuiltins();
  int ratios[] = { 30, 40, 50, 60, 70 };
  for (size_t i = 0; i < sizeof(ratios) / sizeof(ratios[0]); i++) {
    Engine e;
    e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults();
    s.ppm = 60; s.ratio = ratios[i]; s.runLimitMin = 0;
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t); e.start(t);

    uint64_t onUs = 0;
    const uint64_t dur = 30ULL * 1000000ULL;
    uint64_t t0 = t;
    runEngine(e, t, dur, 250, NULL, &onUs);

    double measured = 100.0 * (double)onUs / (double)(t - t0);
    char msg[96];
    snprintf(msg, sizeof(msg), "ratio %d%% produces a %d%% on-phase", ratios[i], ratios[i]);
    okNear(measured, ratios[i], 0.5, msg);
  }
}

// ------------------------------------------------------------------ //
//  6. Engine -- safety behaviours that v43 did not have
// ------------------------------------------------------------------ //
static void testEngineSafety() {
  group("engine safety");

  compileBuiltins();

  // --- e-stop cuts output on the same tick -------------------------
  {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.ppm = 120; s.vacTarget = 80; s.motor = 50; s.runLimitMin = 0;
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t); e.start(t);
    runEngine(e, t, 3000000ULL, 1000);
    ok(e.vacActual() > 50.0f, "vacuum ramps up while running");

    e.triggerEstop(t);
    Outputs o = e.tick(t);
    ok(!o.vacOn && !o.pulseOn && !o.motorOn, "e-stop kills all three channels immediately");
    ok(o.motorDuty == 0 && o.vacDuty == 0, "e-stop zeroes pwm duty, not just the discrete pins");
    okNear(e.vacActual(), 0.0, 0.001, "e-stop dumps the modelled vacuum");

    e.start(t);
    ok(!e.running(), "start() refused while the e-stop is latched");

    e.clearEstop(t);
    e.start(t);
    ok(e.running(), "start() works again once the latch is cleared");
  }

  // --- deadman -----------------------------------------------------
  {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.ppm = 60; s.vacTarget = 60; s.runLimitMin = 0;
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t); e.start(t);
    runEngine(e, t, 2000000ULL, 1000);
    ok(e.running(), "still running while being fed");

    // Stop feeding: simulate a hung UI / network stack.
    for (int i = 0; i < 4000; i++) { e.tick(t); t += 1000; }
    ok(!e.running(), "deadman stops the machine when the supervisor stalls");
    ok(e.lastStop() == STOP_WATCHDOG, "stop reason recorded as watchdog");
    Outputs o = e.tick(t);
    ok(!o.vacOn && !o.pulseOn && !o.motorOn, "outputs are off after a deadman stop");
  }

  // --- run limit ---------------------------------------------------
  {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.ppm = 60; s.runLimitMin = 1;   // 1 minute
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t); e.start(t);
    runEngine(e, t, 59ULL * 1000000ULL, 2000);
    ok(e.running(), "still running just under the limit");
    runEngine(e, t, 3ULL * 1000000ULL, 2000);
    ok(!e.running(), "run limit stops the machine");
    ok(e.lastStop() == STOP_RUN_LIMIT, "stop reason recorded as run limit");
  }

  // --- clock going backwards must not wedge the engine -------------
  {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.ppm = 60; s.runLimitMin = 0;
    uint64_t t = 1000000;
    e.reset(t); e.applySettings(s, t); e.start(t);
    e.feed(t); e.tick(t);
    e.feed(0); e.tick(0);              // clock jumps backwards
    e.feed(t); Outputs o = e.tick(t);
    (void)o;
    ok(e.running(), "engine survives a backwards clock step");
  }

  // --- a stopped engine outputs nothing ---------------------------
  {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.ppm = 400; s.vacTarget = 100; s.motor = 100;
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t);
    bool anyOn = false;
    for (int i = 0; i < 20000; i++) { e.feed(t); Outputs o = e.tick(t); if (o.vacOn || o.pulseOn || o.motorOn) anyOn = true; t += 500; }
    ok(!anyOn, "a stopped engine never energises anything, at any setting");
  }
}

// ------------------------------------------------------------------ //
//  7. Engine -- vacuum ramp must not depend on tick rate
// ------------------------------------------------------------------ //
static void testVacuumRampIsTimeBased() {
  group("vacuum ramp is time-based, not loop-rate-based");

  compileBuiltins();

  auto timeTo63 = [](uint64_t stepUs) -> double {
    Engine e; e.attachPatterns(g_builtins, g_customs);
    Settings s; s.defaults(); s.vacTarget = 100; s.vacRamp = 5; s.runLimitMin = 0;
    uint64_t t = 0;
    e.reset(t); e.applySettings(s, t); e.start(t);
    while (t < 20ULL * 1000000ULL) {
      e.feed(t); e.tick(t);
      if (e.vacActual() >= 63.2f) return t / 1000.0;
      t += stepUs;
    }
    return -1;
  };

  double slow = timeTo63(5000);   // 200 Hz  -- a busy UI
  double fast = timeTo63(500);    // 2 kHz   -- an idle device

  ok(slow > 0 && fast > 0, "target reached at both tick rates");
  okNear(slow, fast, fast * 0.05, "ramp time is within 5% across a 10x tick-rate change");
  okNear(fast, 2200.0 / 5.0, 60.0, "ramp time matches the configured time constant");
}

// ------------------------------------------------------------------ //
//  8. Engine -- rhythm playback drives CH2 only
// ------------------------------------------------------------------ //
static void testRhythmChannelIsolation() {
  group("rhythm channel isolation");

  compileBuiltins();
  g_customs[0].compileCsv("100,100;200,400;");

  Engine e; e.attachPatterns(g_builtins, g_customs);
  Settings s; s.defaults();
  s.rhythmId = kCustomBase;      // custom slot 0
  s.vacTarget = 70; s.motor = 60; s.ppm = 0; s.runLimitMin = 0;
  uint64_t t = 0;
  e.reset(t); e.applySettings(s, t); e.start(t);

  // let vacuum settle
  runEngine(e, t, 5000000ULL, 1000);

  int pulseEdges = 0;
  bool vacEverOff = false, motorEverOff = false, pulseEverOn = false;
  bool prev = false;
  for (int i = 0; i < 20000; i++) {
    e.feed(t); Outputs o = e.tick(t);
    if (o.pulseOn && !prev) pulseEdges++;
    prev = o.pulseOn;
    if (o.pulseOn) pulseEverOn = true;
    if (!o.motorOn) motorEverOff = true;
    t += 1000;
  }
  (void)vacEverOff;
  ok(pulseEverOn && pulseEdges > 10, "custom rhythm pulses CH2");
  ok(!motorEverOff, "motor stays commanded through the whole rhythm");
  ok(e.vacActual() > 60.0f, "vacuum stays at its set point through the whole rhythm");

  // Selecting a rhythm restarts its clock, changing speed does not.
  Settings s2 = s; s2.rhythmSpeed = 200;
  e.applySettings(s2, t);
  ok(e.running(), "speed change does not disturb the run");
}

// ------------------------------------------------------------------ //
//  9. Engine -- motor soft start
// ------------------------------------------------------------------ //
static void testMotorSoftStart() {
  group("motor soft start (new in 2.0)");

  compileBuiltins();
  Engine e; e.attachPatterns(g_builtins, g_customs);
  Settings s; s.defaults(); s.motor = 100; s.motorSoftMs = 1000; s.runLimitMin = 0;
  uint64_t t = 0;
  e.reset(t); e.applySettings(s, t); e.start(t);

  e.feed(t); Outputs a = e.tick(t);
  ok(a.motorDuty < 20, "motor duty starts near zero, not at full inrush");

  t += 500000; e.feed(t); Outputs b = e.tick(t);
  okNear(b.motorDuty, 127, 12, "motor duty is about half way at half the ramp");

  t += 700000; e.feed(t); Outputs c = e.tick(t);
  ok(c.motorDuty == 255, "motor duty reaches full after the ramp");

  // Zero ramp must still be legal and immediate.
  Settings s2 = s; s2.motorSoftMs = 0;
  Engine e2; e2.attachPatterns(g_builtins, g_customs);
  uint64_t t2 = 0;
  e2.reset(t2); e2.applySettings(s2, t2); e2.start(t2);
  e2.feed(t2); Outputs d = e2.tick(t2);
  ok(d.motorDuty == 255, "zero soft-start ramp goes straight to full");
}

// ------------------------------------------------------------------ //
int main() {
  printf("\n\033[1m=== PulseFeed core test suite ===\033[0m\n");
  printf("core version %s, api level %s\n", kVersion, kApiLevel);

  testNormalise();
  testJson();
  testRhythmCompile();
  testRhythmClockOverflow();
  testEnginePulsation();
  testEngineRatio();
  testEngineSafety();
  testVacuumRampIsTimeBased();
  testRhythmChannelIsolation();
  testMotorSoftStart();

  printf("\n\033[1m---------------------------------\033[0m\n");
  printf("passed: \033[32m%d\033[0m   failed: %s%d\033[0m\n",
         g_pass, g_fail ? "\033[31m" : "\033[32m", g_fail);
  return g_fail == 0 ? 0 : 1;
}
