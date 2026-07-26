// =====================================================================
//  pf_ui.h -- touchscreen interface
//
//  Two structural changes from v43:
//
//  1. Every button is a row in a layout table. v43 drew the theme toggle
//     with drawThemeModeButton(182,39,124,40) and hit-tested it with
//     insideRect(x,y,182,39,124,40) written out again by hand a thousand
//     lines away. Four literals duplicated per control, with nothing
//     keeping them in step. Here draw and hit-test read the same row.
//
//  2. Everything renders into an off-screen canvas and is pushed once.
//     v43 called fillScreen() then redrew, and repainted live regions
//     directly every 90-180 ms, so the display visibly tore and flashed.
// =====================================================================
#pragma once

#include "pf_app.h"

namespace pf {
namespace ui {

enum Screen {
  SCR_HOME = 0,
  SCR_VAC,
  SCR_PULSE,
  SCR_MOTOR,
  SCR_RHYTHM,
  SCR_GRAPH,
  SCR_NET,
  SCR_SETTINGS,
  SCR_RECORD,
  SCR_SERVICE,
  SCR_ESTOP_CONFIRM,
  SCR_ESTOP_ACTIVE,
  SCR_COUNT
};

void begin();
void bootAnimation();
void applyBacklight();
void tick();          // repaint if due
void handleTouch();
void invalidate();

void   setScreen(int s);
int    screen();

// Network strings are owned by pf_net; the UI just renders them.
void setNetLines(const char* ssid, const char* ip, const char* mode, int bars, bool ap);

}  // namespace ui
}  // namespace pf
