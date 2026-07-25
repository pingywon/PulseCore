// =====================================================================
//  pf_log.cpp
// =====================================================================
#include "pf_log.h"
#include "pf_hal.h"

#include <SD.h>
#include <SPI.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace pf {
namespace log {

namespace {

struct Record {
  uint32_t ms;
  time_t   wall;
  Snapshot snap;
  char     name[40];
};

QueueHandle_t g_q        = nullptr;
bool          g_card     = false;
bool          g_ready    = false;
uint32_t      g_written  = 0;
uint32_t      g_dropped  = 0;
uint32_t      g_lastFlush = 0;
uint32_t      g_lastSample = 0;
char          g_path[48] = "";
char          g_day[12]  = "";

Snapshot      g_pub = { 0, 0, 0, 0, 0, 0, 0, 0 };
portMUX_TYPE  g_pubMux = portMUX_INITIALIZER_UNLOCKED;

const char* kHeader =
  "ms,time,event,vacTarget,vacActual,ppm,ratio,motor,rhythmId,"
  "running,solVac,solPulse,solMotor,estop,heap";

// Today's date as YYYY-MM-DD. Uses a plain time() read -- no blocking
// retry loop. Before NTP or RTC has set the clock this returns the
// epoch-based fallback date, which still groups a session correctly.
void today(char* out, size_t len) {
  time_t now = time(nullptr);
  struct tm tmv;
  if (now > 1600000000 && localtime_r(&now, &tmv)) {
    snprintf(out, len, "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
  } else {
    snprintf(out, len, "0000-00-00");
  }
}

void stampTime(time_t wall, uint32_t ms, char* out, size_t len) {
  struct tm tmv;
  if (wall > 1600000000 && localtime_r(&wall, &tmv)) {
    snprintf(out, len, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    // No wall clock yet: fall back to uptime so ordering is still usable.
    uint32_t s = ms / 1000;
    snprintf(out, len, "%02lu:%02lu:%02lu",
             (unsigned long)((s / 3600) % 24), (unsigned long)((s % 3600) / 60), (unsigned long)(s % 60));
  }
}

// Open (creating if needed) today's log and make sure it has a header.
bool ensureDailyFile() {
  if (!g_card) return false;

  char d[12];
  today(d, sizeof(d));
  if (strcmp(d, g_day) == 0 && g_path[0]) return true;   // unchanged

  strncpy(g_day, d, sizeof(g_day) - 1);
  g_day[sizeof(g_day) - 1] = '\0';

  if (!SD.exists(PF_LOG_ROOT)) SD.mkdir(PF_LOG_ROOT);

  char dir[32];
  snprintf(dir, sizeof(dir), PF_LOG_ROOT "/%s", g_day);
  if (!SD.exists(dir)) SD.mkdir(dir);

  snprintf(g_path, sizeof(g_path), "%s/%s.log", dir, g_day);

  bool isNew = !SD.exists(g_path);
  File f = SD.open(g_path, FILE_APPEND);
  if (!f) { g_ready = false; return false; }
  if (isNew) f.println(kHeader);
  f.close();
  g_ready = true;
  return true;
}

bool nameIsSafe(const char* n) {
  if (!n || !*n) return false;
  size_t len = strlen(n);
  if (len < 5 || len > 32) return false;
  if (strstr(n, "..")) return false;
  for (const char* p = n; *p; ++p) {
    bool okc = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') ||
               (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_' || *p == '.';
    if (!okc) return false;
  }
  return strcmp(n + len - 4, ".log") == 0;
}

}  // namespace

bool begin() {
  if (!g_q) g_q = xQueueCreate(PF_LOG_QUEUE_LEN, sizeof(Record));

  SPI.begin(PF_SD_SCK, PF_SD_MISO, PF_SD_MOSI, PF_SD_CS);
  g_card = SD.begin(PF_SD_CS, SPI, PF_SD_HZ);
  if (g_card) ensureDailyFile();
  return g_card;
}

bool cardPresent()  { return g_card; }
bool loggingReady() { return g_card && g_ready; }

void publish(const Snapshot& s) {
  portENTER_CRITICAL(&g_pubMux);
  g_pub = s;
  portEXIT_CRITICAL(&g_pubMux);
}

void event(const char* name, const Snapshot& s) {
  if (!g_q) return;
  Record r;
  r.ms   = hal::nowMs();
  r.wall = time(nullptr);
  r.snap = s;
  strncpy(r.name, name ? name : "", sizeof(r.name) - 1);
  r.name[sizeof(r.name) - 1] = '\0';
  // Commas would corrupt the CSV column layout.
  for (char* p = r.name; *p; ++p) if (*p == ',') *p = ' ';

  // Zero timeout: a control path must never wait on the logger.
  if (xQueueSend(g_q, &r, 0) != pdTRUE) g_dropped++;
}

void event(const char* name) {
  Snapshot s;
  portENTER_CRITICAL(&g_pubMux);
  s = g_pub;
  portEXIT_CRITICAL(&g_pubMux);
  event(name, s);
}

void sample() {
  uint32_t now = hal::nowMs();
  if (now - g_lastSample < PF_LOG_SAMPLE_MS) return;
  g_lastSample = now;
  Snapshot s;
  portENTER_CRITICAL(&g_pubMux);
  s = g_pub;
  portEXIT_CRITICAL(&g_pubMux);
  if (!(s.flags & F_RUNNING)) return;
  event("sample", s);
}

void service() {
  if (!g_q || !g_card) return;
  if (uxQueueMessagesWaiting(g_q) == 0) return;

  uint32_t now = hal::nowMs();
  bool full = uxQueueMessagesWaiting(g_q) >= (PF_LOG_QUEUE_LEN / 2);
  if (!full && (now - g_lastFlush) < PF_LOG_FLUSH_MS) return;
  g_lastFlush = now;

  if (!ensureDailyFile()) return;

  // One open/close per flush rather than per line: on a class-10 card
  // this turns ~15 ms of overhead per event into ~15 ms per second.
  File f = SD.open(g_path, FILE_APPEND);
  if (!f) { g_ready = false; return; }

  Record r;
  int drained = 0;
  char line[PF_LOG_LINE_MAX];
  char ts[16];
  while (drained < PF_LOG_QUEUE_LEN && xQueueReceive(g_q, &r, 0) == pdTRUE) {
    stampTime(r.wall, r.ms, ts, sizeof(ts));
    snprintf(line, sizeof(line),
             "%lu,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lu",
             (unsigned long)r.ms, ts, r.name,
             r.snap.vacTarget, r.snap.vacActual, r.snap.ppm, r.snap.ratio,
             r.snap.motor, r.snap.rhythmId,
             (r.snap.flags & F_RUNNING) ? 1 : 0,
             (r.snap.flags & F_VAC)     ? 1 : 0,
             (r.snap.flags & F_PULSE)   ? 1 : 0,
             (r.snap.flags & F_MOTOR)   ? 1 : 0,
             (r.snap.flags & F_ESTOP)   ? 1 : 0,
             (unsigned long)r.snap.heap);
    f.println(line);
    drained++;
    g_written++;
  }
  f.close();
}

uint32_t queued()  { return g_q ? uxQueueMessagesWaiting(g_q) : 0; }
uint32_t written() { return g_written; }
uint32_t dropped() { return g_dropped; }
const char* currentPath() { return g_path; }

void listJson(JsonOut& j) {
  j.beginArr();
  if (g_card) {
    File root = SD.open(PF_LOG_ROOT);
    if (root) {
      File day = root.openNextFile();
      while (day) {
        if (day.isDirectory()) {
          File f = day.openNextFile();
          while (f) {
            if (!f.isDirectory()) {
              const char* n = f.name();
              const char* base = strrchr(n, '/');
              base = base ? base + 1 : n;
              size_t bl = strlen(base);
              if (bl > 4 && strcmp(base + bl - 4, ".log") == 0) j.arrStr(base);
            }
            f.close();
            f = day.openNextFile();
          }
        }
        day.close();
        day = root.openNextFile();
      }
      root.close();
    }
  }
  j.endArr();
}

bool resolvePath(const char* name, char* out, size_t outLen) {
  if (!g_card) return false;
  if (!nameIsSafe(name)) return false;
  // The day directory is the filename stem, so a name fully determines
  // its path and there is nowhere for a traversal to point.
  char stem[16];
  size_t sl = strlen(name) - 4;
  if (sl >= sizeof(stem)) return false;
  memcpy(stem, name, sl);
  stem[sl] = '\0';
  snprintf(out, outLen, PF_LOG_ROOT "/%s/%s", stem, name);
  return SD.exists(out);
}

}  // namespace log
}  // namespace pf
