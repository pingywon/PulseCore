// =====================================================================
//  pulsefeed-sim.cpp -- host simulator
//
//  Runs the REAL pf_core engine (same source files the firmware
//  compiles) at 1 kHz, exposes the REAL REST API v1, and serves the
//  REAL web UI. No hardware required.
//
//  Why this exists:
//    * the web UI and the API contract can be exercised and reviewed
//      without a device on the bench
//    * the reverse proxy in bridge/ can be developed and tested against
//      something that behaves exactly like the controller
//    * the pulsation engine can be watched running in real time, which
//      is a far better check on "does it feel right" than a unit test
//
//  build: tools/build.sh sim
//  run:   ./dist/pulsefeed-sim --port 8080
// =====================================================================
#include "../firmware/pulsefeed/pf_core.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace pf;

// ------------------------------------------------------------------ //
//  Simulated device state
// ------------------------------------------------------------------ //
namespace {

struct Slot {
  std::string name;
  std::string data;
};

std::mutex        g_mx;
Engine            g_engine;
Settings          g_set;
RhythmPattern     g_builtins[kBuiltinCount];
RhythmPattern     g_customs[Limits::kRhythmSlots];
Slot              g_slots[Limits::kRhythmSlots];
std::atomic<bool> g_quit{false};

std::string g_status = "Simulator ready";
std::string g_pin    = "246813";
std::string g_webRoot;
bool        g_auth   = true;
int         g_port   = 8080;

bool        g_recording = false;
int         g_recSlot   = 0;
std::string g_recBuf;
uint64_t    g_recDownMs = 0;
uint64_t    g_recLastMs = 0;

std::vector<std::string> g_tokens;
uint64_t g_startWall = 0;
uint64_t g_reqs = 0;

uint64_t nowUs() {
  using namespace std::chrono;
  return (uint64_t)duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}
uint64_t nowMs() { return nowUs() / 1000; }

std::string randomToken() {
  static const char cs[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  static std::mt19937_64 rng{std::random_device{}()};
  std::string t;
  for (int i = 0; i < 24; i++) t += cs[rng() % 62];
  return t;
}

const char* rhythmLabel(int id) {
  static std::string buf;
  if (id == 0) return "Off";
  if (isBuiltin(id)) return builtinName(id);
  int s = customSlot(id);
  if (s >= 0) { buf = g_slots[s].name; return buf.c_str(); }
  return "Off";
}

const char* rhythmNotation(int id) {
  static std::string buf;
  if (isBuiltin(id)) return builtinDots(id);
  int s = customSlot(id);
  if (s >= 0) { buf = g_slots[s].data.empty() ? "(empty)" : g_slots[s].data; return buf.c_str(); }
  return "";
}

// ------------------------------------------------------------------ //
//  Engine thread -- same 1 kHz cadence as the firmware task
// ------------------------------------------------------------------ //
void engineThread() {
  auto next = std::chrono::steady_clock::now();
  while (!g_quit) {
    {
      std::lock_guard<std::mutex> lk(g_mx);
      uint64_t t = nowUs();
      g_engine.feed(t);
      g_engine.tick(t);
    }
    next += std::chrono::milliseconds(1);
    std::this_thread::sleep_until(next);
  }
}

// ------------------------------------------------------------------ //
//  Minimal HTTP
// ------------------------------------------------------------------ //
struct Req {
  std::string method, path, body;
  std::map<std::string, std::string> headers;
  std::map<std::string, std::string> args;
};

std::string urlDecode(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '+') o += ' ';
    else if (s[i] == '%' && i + 2 < s.size()) {
      o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    } else o += s[i];
  }
  return o;
}

void parseArgs(const std::string& q, std::map<std::string, std::string>& out) {
  std::stringstream ss(q);
  std::string pair;
  while (std::getline(ss, pair, '&')) {
    auto eq = pair.find('=');
    if (eq == std::string::npos) out[urlDecode(pair)] = "";
    else out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
  }
}

void send(int fd, int code, const std::string& mime, const std::string& body,
          const std::string& extra = "") {
  const char* txt = code == 200 ? "OK" : code == 401 ? "Unauthorized"
                  : code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed"
                  : code == 429 ? "Too Many Requests" : "Error";
  std::ostringstream h;
  h << "HTTP/1.1 " << code << " " << txt << "\r\n"
    << "Content-Type: " << mime << "\r\n"
    << "Content-Length: " << body.size() << "\r\n"
    << "Cache-Control: no-store\r\n"
    << "Connection: close\r\n" << extra << "\r\n";
  std::string out = h.str() + body;
  ssize_t n = ::send(fd, out.data(), out.size(), MSG_NOSIGNAL);
  (void)n;
}

bool readFile(const std::string& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss; ss << f.rdbuf(); out = ss.str();
  return true;
}

int argInt(const Req& r, const char* k, int fb) {
  auto it = r.args.find(k);
  if (it == r.args.end()) return fb;
  return atoi(it->second.c_str());
}
std::string argStr(const Req& r, const char* k) {
  auto it = r.args.find(k);
  return it == r.args.end() ? std::string() : it->second;
}

bool authed(const Req& r) {
  if (!g_auth) return true;
  auto it = r.headers.find("x-pf-token");
  std::string tok = (it != r.headers.end()) ? it->second : argStr(r, "t");
  if (tok.empty()) return false;
  for (auto& t : g_tokens) if (t == tok) return true;
  return false;
}

// ------------------------------------------------------------------ //
//  API
// ------------------------------------------------------------------ //
void applySettings() { g_engine.applySettings(g_set, nowUs()); }

bool recompile(int slot) {
  bool active = (g_set.rhythmId == kCustomBase + slot);
  if (active) { g_set.rhythmId = 0; applySettings(); }
  bool ok = g_customs[slot].compileCsv(g_slots[slot].data.c_str());
  if (active && ok) { g_set.rhythmId = kCustomBase + slot; applySettings(); }
  return ok;
}

std::string metaJson() {
  static char buf[8192];
  JsonOut j(buf, sizeof(buf));
  j.beginObj();
  j.kvStr("product", kProduct);
  j.kvStr("model", kModel);
  j.kvStr("version", kVersion);
  j.kvNum("api", 1);
  j.kvBool("authRequired", g_auth);
  j.key("limits"); j.beginObj();
  j.kvNum("ppmMin", Limits::kPpmMin);     j.kvNum("ppmMax", Limits::kPpmMax);
  j.kvNum("ratioMin", Limits::kRatioMin); j.kvNum("ratioMax", Limits::kRatioMax);
  j.kvNum("motorStep", Limits::kMotorStep);
  j.kvNum("speedMin", Limits::kSpeedMin); j.kvNum("speedMax", Limits::kSpeedMax);
  j.kvNum("slots", Limits::kRhythmSlots);
  j.kvNum("customBase", kCustomBase);
  j.endObj();
  j.key("rhythms"); j.beginArr();
  for (int i = 1; i <= kBuiltinCount; i++) {
    j.beginObj();
    j.kvNum("id", i);
    j.kvStr("name", builtinName(i));
    j.kvStr("dots", builtinDots(i));
    j.endObj();
  }
  j.endArr();
  j.endObj();
  return std::string(j.c_str());
}

std::string stateJson() {
  static char buf[8192];
  JsonOut j(buf, sizeof(buf));
  Outputs o = g_engine.outputs();
  j.beginObj();
  j.kvBool("running", g_engine.running());
  j.kvBool("estop", g_engine.estop());
  j.kvNum("stopReason", (long)g_engine.lastStop());
  j.kvNum("runSec", (long)g_engine.runSeconds(nowUs()));
  j.kvNum("pulses", (long)g_engine.pulseCount());
  j.kvReal("phase", g_engine.pulsePhase(), 3);

  j.kvNum("vac", g_set.vacTarget);
  j.kvReal("vacActual", g_engine.vacActual(), 1);
  j.kvNum("ramp", g_set.vacRamp);
  j.kvNum("ppm", g_set.ppm);
  j.kvNum("ratio", g_set.ratio);
  j.kvNum("motor", g_set.motor);
  j.kvNum("motorSoftMs", g_set.motorSoftMs);
  j.kvNum("runLimitMin", g_set.runLimitMin);
  j.kvBool("autoStopOnDisconnect", g_set.autoStopOnDisconnect);

  j.kvNum("rhythm", g_set.rhythmId);
  j.kvStr("rhythmName", rhythmLabel(g_set.rhythmId));
  j.kvStr("rhythmDots", rhythmNotation(g_set.rhythmId));
  j.kvNum("rhythmSpeed", g_set.rhythmSpeed);

  j.key("out"); j.beginObj();
  j.kvBool("vac", o.vacOn); j.kvBool("pulse", o.pulseOn); j.kvBool("motor", o.motorOn);
  j.kvNum("motorDuty", o.motorDuty);
  j.endObj();

  j.key("slots"); j.beginArr();
  for (int i = 0; i < Limits::kRhythmSlots; i++) {
    j.beginObj();
    j.kvNum("id", kCustomBase + i);
    j.kvStr("name", g_slots[i].name.c_str());
    j.kvStr("data", g_slots[i].data.c_str());
    j.kvBool("valid", g_customs[i].valid());
    j.endObj();
  }
  j.endArr();
  j.kvBool("recording", g_recording);
  j.kvNum("recordSlot", g_recSlot);

  j.key("net"); j.beginObj();
  j.kvStr("ssid", "simulator");
  j.kvStr("ip", "127.0.0.1");
  j.kvStr("mode", "SIMULATED");
  char share[64]; snprintf(share, sizeof(share), "http://localhost:%d", g_port);
  j.kvStr("share", share);
  j.kvNum("bars", 4);
  j.kvBool("ap", false);
  j.kvStr("apSsid", "PulseFeed-SIM");
  j.endObj();

  j.key("sys"); j.beginObj();
  j.kvUNum("heap", 240000);
  j.kvUNum("uptime", (unsigned long)((nowMs() - g_startWall) / 1000));
  j.kvBool("sd", false);
  j.kvUNum("logWritten", 0);
  j.kvUNum("logDropped", 0);
  j.kvUNum("nvsWrites", 0);
  j.kvBool("dark", g_set.darkTheme);
  j.kvNum("backlight", g_set.backlight);
  j.kvStr("version", kVersion);
  j.endObj();

  j.kvStr("status", g_status.c_str());
  j.endObj();
  return std::string(j.c_str());
}

std::string okJson(const char* msg = "ok") {
  static char buf[512];
  JsonOut j(buf, sizeof(buf));
  j.beginObj(); j.kvBool("ok", true); j.kvStr("msg", msg); j.kvStr("status", g_status.c_str()); j.endObj();
  return std::string(j.c_str());
}
std::string errJson(const char* e) {
  static char buf[256];
  JsonOut j(buf, sizeof(buf));
  j.beginObj(); j.kvBool("ok", false); j.kvStr("error", e); j.endObj();
  return std::string(j.c_str());
}

void handle(int fd, Req& r) {
  g_reqs++;
  const std::string& p = r.path;

  // ---- static -----------------------------------------------------
  if (r.method == "GET" && (p == "/" || p == "/index.html")) {
    std::string body;
    if (!readFile(g_webRoot + "/index.html", body)) { send(fd, 404, "text/plain", "index.html missing"); return; }
    send(fd, 200, "text/html; charset=utf-8", body);
    return;
  }
  if (r.method == "GET" && (p == "/stats" || p == "/stats.html")) {
    std::string body;
    if (!readFile(g_webRoot + "/stats.html", body)) { send(fd, 404, "text/plain", "stats.html missing"); return; }
    send(fd, 200, "text/html; charset=utf-8", body);
    return;
  }

  // ---- open endpoints ---------------------------------------------
  if (p == "/api/v1/meta")   { send(fd, 200, "application/json", metaJson()); return; }
  if (p == "/api/v1/health") { send(fd, 200, "application/json", "{\"ok\":true,\"api\":1}"); return; }

  if (p == "/api/v1/auth") {
    if (r.method != "POST") { send(fd, 405, "application/json", errJson("post_required")); return; }
    if (argStr(r, "pin") == g_pin) {
      std::string t = randomToken();
      { std::lock_guard<std::mutex> lk(g_mx); g_tokens.push_back(t); }
      static char buf[256];
      JsonOut j(buf, sizeof(buf));
      j.beginObj(); j.kvBool("ok", true); j.kvStr("token", t.c_str()); j.endObj();
      send(fd, 200, "application/json", std::string(j.c_str()));
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    send(fd, 401, "application/json", errJson("bad_pin"));
    return;
  }

  // ---- authenticated ----------------------------------------------
  if (!authed(r)) { send(fd, 401, "application/json", errJson("auth_required")); return; }

  std::lock_guard<std::mutex> lk(g_mx);

  if (p == "/api/v1/state") { send(fd, 200, "application/json", stateJson()); return; }

  if (p == "/api/v1/control") {
    if (r.method != "POST") { send(fd, 405, "application/json", errJson("post_required")); return; }
    std::string a = argStr(r, "action");
    if (a == "start") {
      if (g_engine.estop()) { send(fd, 423, "application/json", errJson("estop_latched")); return; }
      g_engine.feed(nowUs()); g_engine.start(nowUs()); g_status = "Running";
    } else if (a == "stop")    { g_engine.stop(nowUs(), STOP_OPERATOR); g_status = "Stopped"; }
    else if (a == "estop")     { g_engine.triggerEstop(nowUs()); g_status = "E-STOP - outputs de-energised"; }
    else if (a == "release")   { g_status = "Pressure released"; }
    else if (a == "clear")     { g_engine.clearEstop(nowUs()); g_status = "E-STOP cleared"; }
    else { send(fd, 400, "application/json", errJson("unknown_action")); return; }
    send(fd, 200, "application/json", okJson());
    return;
  }

  if (p == "/api/v1/settings") {
    if (r.method != "POST") { send(fd, 405, "application/json", errJson("post_required")); return; }
    if (r.args.count("vac"))   g_set.vacTarget   = argInt(r, "vac", g_set.vacTarget);
    if (r.args.count("ppm"))   g_set.ppm         = argInt(r, "ppm", g_set.ppm);
    if (r.args.count("ratio")) g_set.ratio       = argInt(r, "ratio", g_set.ratio);
    if (r.args.count("motor")) g_set.motor       = argInt(r, "motor", g_set.motor);
    if (r.args.count("ramp"))  g_set.vacRamp     = argInt(r, "ramp", g_set.vacRamp);
    if (r.args.count("motorSoftMs")) g_set.motorSoftMs = argInt(r, "motorSoftMs", g_set.motorSoftMs);
    if (r.args.count("runLimitMin")) g_set.runLimitMin = argInt(r, "runLimitMin", g_set.runLimitMin);
    if (r.args.count("backlight"))   g_set.backlight   = argInt(r, "backlight", g_set.backlight);
    if (r.args.count("dark"))        g_set.darkTheme   = argInt(r, "dark", 1) == 1;
    g_set.validate();
    applySettings();
    send(fd, 200, "application/json", okJson());
    return;
  }

  if (p == "/api/v1/rhythm") {
    if (r.method != "POST") { send(fd, 405, "application/json", errJson("post_required")); return; }
    if (r.args.count("speed")) { g_set.rhythmSpeed = argInt(r, "speed", g_set.rhythmSpeed); }
    if (r.args.count("id")) {
      int id = argInt(r, "id", 0);
      int s = customSlot(id);
      if (s >= 0 && !g_customs[s].valid()) { g_status = "That custom slot is empty"; }
      else g_set.rhythmId = id;
    }
    if (r.args.count("fav")) {
      int s = clampi(argInt(r, "fav", 1), 1, 3) - 1;
      g_set.fav[s] = g_set.rhythmId;
    }
    g_set.validate();
    applySettings();
    send(fd, 200, "application/json", okJson());
    return;
  }

  if (p == "/api/v1/custom") {
    if (r.method != "POST") { send(fd, 405, "application/json", errJson("post_required")); return; }
    int slot = clampi(argInt(r, "slot", 1), 1, Limits::kRhythmSlots) - 1;
    std::string a = argStr(r, "action");
    std::string nm = argStr(r, "name");
    if (!nm.empty()) g_slots[slot].name = nm.substr(0, Limits::kRhythmNameLen - 1);

    if (a == "arm")   { g_recording = true; g_recSlot = slot; g_recBuf.clear();
                        g_recDownMs = 0; g_recLastMs = nowMs(); g_status = "Recording"; }
    else if (a == "down") { if (g_recording) g_recDownMs = nowMs(); }
    else if (a == "up") {
      if (!g_recording) { send(fd, 409, "application/json", errJson("not_recording")); return; }
      uint64_t now = nowMs();
      uint64_t down = g_recDownMs ? g_recDownMs : (now > 120 ? now - 120 : 0);
      int on  = clampi((int)(now - down), 20, 5000);
      int gap = clampi((int)(down - g_recLastMs), 0, 10000);
      char frag[32]; snprintf(frag, sizeof(frag), "%d,%d;", on, gap);
      if (g_recBuf.size() + strlen(frag) < Limits::kRhythmDataLen) g_recBuf += frag;
      g_recLastMs = now; g_recDownMs = 0;
    }
    else if (a == "stop")  { g_recording = false; g_status = "Recording stopped"; }
    else if (a == "save")  {
      if (g_recSlot == slot && !g_recBuf.empty()) g_slots[slot].data = g_recBuf;
      g_recording = false;
      if (!recompile(slot)) { send(fd, 400, "application/json", errJson("nothing_recorded")); return; }
      g_status = "Custom rhythm saved";
    }
    else if (a == "clear") {
      if (g_set.rhythmId == kCustomBase + slot) { g_set.rhythmId = 0; applySettings(); }
      g_slots[slot].data.clear(); g_customs[slot].clear(); g_recBuf.clear();
      g_recording = false; g_status = "Custom slot cleared";
    }
    else if (a == "play") {
      if (!g_customs[slot].valid()) g_status = "That custom slot is empty";
      else { g_set.rhythmId = kCustomBase + slot; applySettings(); }
    }
    else if (a == "seed") {
      int from = argInt(r, "from", 0);
      RhythmPattern tmp;
      if (!isBuiltin(from) || !tmp.compileDots(builtinDots(from), 250)) {
        send(fd, 400, "application/json", errJson("bad_source")); return;
      }
      char csv[Limits::kRhythmDataLen];
      tmp.toCsv(csv, sizeof(csv));
      g_slots[slot].data = csv;
      char nm[Limits::kRhythmNameLen];
      snprintf(nm, sizeof(nm), "%s+", builtinName(from));
      g_slots[slot].name = nm;
      if (!recompile(slot)) { send(fd, 400, "application/json", errJson("bad_source")); return; }
      g_status = "Preset copied to slot - now adjustable";
    }
    send(fd, 200, "application/json", okJson());
    return;
  }

  if (p == "/api/v1/wifi/scan") {
    send(fd, 200, "application/json",
         "{\"scanning\":false,\"networks\":["
         "{\"ssid\":\"Simulated-Barn\",\"rssi\":-48,\"open\":false},"
         "{\"ssid\":\"Simulated-House\",\"rssi\":-71,\"open\":false}]}");
    return;
  }
  if (p == "/api/v1/wifi/join" || p == "/api/v1/wifi/forget") {
    send(fd, 200, "application/json", okJson("simulated"));
    return;
  }
  if (p == "/api/v1/logs") {
    send(fd, 200, "application/json",
         "{\"sd\":false,\"current\":\"\",\"written\":0,\"dropped\":0,\"files\":[]}");
    return;
  }
  if (p == "/api/v1/system") {
    std::string a = argStr(r, "action");
    if (a == "reboot") { g_engine.stop(nowUs(), STOP_OPERATOR); g_engine.reset(nowUs()); applySettings(); g_status = "Simulated reboot"; }
    send(fd, 200, "application/json", okJson());
    return;
  }

  send(fd, 404, "application/json", errJson("no_such_endpoint"));
}

void serveConn(int fd) {
  std::string raw;
  char buf[4096];
  ssize_t n;
  // Read headers.
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
    raw.append(buf, n);
    if (raw.find("\r\n\r\n") != std::string::npos) break;
    if (raw.size() > 65536) break;
  }
  if (raw.empty()) { close(fd); return; }

  Req r;
  std::istringstream ss(raw);
  std::string line;
  std::getline(ss, line);
  {
    std::istringstream ls(line);
    std::string ver;
    ls >> r.method >> r.path >> ver;
  }
  size_t contentLen = 0;
  while (std::getline(ss, line) && line != "\r" && !line.empty()) {
    auto c = line.find(':');
    if (c == std::string::npos) continue;
    std::string k = line.substr(0, c), v = line.substr(c + 1);
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
    while (!v.empty() && (v.back() == '\r' || v.back() == '\n')) v.pop_back();
    for (auto& ch : k) ch = tolower(ch);
    r.headers[k] = v;
    if (k == "content-length") contentLen = strtoul(v.c_str(), nullptr, 10);
  }

  // Query string
  auto q = r.path.find('?');
  if (q != std::string::npos) {
    parseArgs(r.path.substr(q + 1), r.args);
    r.path = r.path.substr(0, q);
  }

  // Body
  size_t hdrEnd = raw.find("\r\n\r\n");
  if (hdrEnd != std::string::npos) {
    r.body = raw.substr(hdrEnd + 4);
    while (r.body.size() < contentLen) {
      n = recv(fd, buf, sizeof(buf), 0);
      if (n <= 0) break;
      r.body.append(buf, n);
    }
  }
  if (!r.body.empty()) parseArgs(r.body, r.args);

  handle(fd, r);
  close(fd);
}

void onSignal(int) { g_quit = true; }

}  // namespace

// ------------------------------------------------------------------ //
int main(int argc, char** argv) {
  g_webRoot = "web/src";
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc)    g_port = atoi(argv[++i]);
    else if (a == "--web" && i + 1 < argc) g_webRoot = argv[++i];
    else if (a == "--pin" && i + 1 < argc) g_pin = argv[++i];
    else if (a == "--no-auth")             g_auth = false;
    else if (a == "--help") {
      printf("pulsefeed-sim [--port N] [--web DIR] [--pin 123456] [--no-auth]\n");
      return 0;
    }
  }

  signal(SIGINT, onSignal);
  signal(SIGTERM, onSignal);
  signal(SIGPIPE, SIG_IGN);

  g_startWall = nowMs();
  g_set.defaults();
  g_set.runLimitMin = 0;             // no nagging limit in a demo
  for (int i = 1; i <= kBuiltinCount; i++) g_builtins[i - 1].compileDots(builtinDots(i), 250);
  for (int i = 0; i < Limits::kRhythmSlots; i++) {
    g_slots[i].name = "Custom " + std::to_string(i + 1);
  }
  // Two demo slots so the recorder has something to show immediately.
  g_slots[0].name = "Demo triple"; g_slots[0].data = "90,90;90,90;90,420;";
  g_slots[1].name = "Demo long";   g_slots[1].data = "700,300;180,180;";
  for (int i = 0; i < Limits::kRhythmSlots; i++) g_customs[i].compileCsv(g_slots[i].data.c_str());

  g_engine.attachPatterns(g_builtins, g_customs);
  g_engine.reset(nowUs());
  g_engine.applySettings(g_set, nowUs());

  int srv = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;          // 0.0.0.0 so the LAN can reach it
  addr.sin_port = htons(g_port);
  if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
  if (listen(srv, 16) < 0) { perror("listen"); return 1; }

  std::thread eng(engineThread);

  printf("\n  PulseFeed simulator -- core v%s\n", kVersion);
  printf("  listening on 0.0.0.0:%d   web root: %s\n", g_port, g_webRoot.c_str());
  printf("  PIN: %s%s\n\n", g_pin.c_str(), g_auth ? "" : "  (auth disabled)");
  fflush(stdout);

  while (!g_quit) {
    sockaddr_in cli{};
    socklen_t cl = sizeof(cli);
    int fd = accept(srv, (sockaddr*)&cli, &cl);
    if (fd < 0) { if (g_quit) break; continue; }
    std::thread(serveConn, fd).detach();
  }

  g_quit = true;
  eng.join();
  close(srv);
  printf("\n  stopped after %llu requests\n", (unsigned long long)g_reqs);
  return 0;
}
