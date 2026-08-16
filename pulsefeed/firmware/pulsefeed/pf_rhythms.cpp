// =====================================================================
//  pf_rhythms.cpp  --  built-in pattern table (portable)
//
//  Dot/dash notation, compiled at boot into RhythmPattern objects.
//  Keeping the table here rather than inside the UI or the web page
//  means the device, the API and the browser all name the same pattern
//  the same way. v43 duplicated this list in the JavaScript, so adding
//  a rhythm to the firmware silently mislabelled every later entry in
//  the web dropdown.
// =====================================================================
#include "pf_core.h"

namespace pf {

namespace {

struct BuiltinDef {
  const char* name;
  const char* dots;
};

// Index 0 == rhythm id 1.
const BuiltinDef kBuiltins[kBuiltinCount] = {
  { "Steady",         ". . . . . . . ."     },
  { "Slow Pulse",     "- - - -"             },
  { "Heartbeat",      ". . - . . -"         },
  { "Double Tap",     ". .   . ."           },
  { "Triple Tap",     ". . .   . . ."       },
  { "Wave",           ". - . - . -"         },
  { "Rolling",        ". . - - . . - -"     },
  { "Breathing",      "- . . -"             },
  { "Massage Soft",   ". .   -"             },
  { "Massage Deep",   "- - . ."             },
  { "Stagger",        ". - . . - ."         },
  { "Alt Pairs",      ". . - -"             },
  { "SOS",            ". . . - - - . . ."   },
  { "Mary",           ". - . . - . ."       },
  { "Twinkle",        ". . - . . - -"       },
  { "Charge",         ". . . -"             },
  { "Shave",          ". . - . -"           },
  { "Slow Release",   "- .   - ."           },
  { "Quick Flutter",  ". . . . -"           },
  { "Deep Wave",      "- . - . -"           },
  // --- added in 2.0 -------------------------------------------------
  { "Letdown",        ". . . . . . . .   -" },
  { "Ramp Up",        ". . . - . . - - -"   },
  { "Long Draw",      "- - .   - - ."       },
  { "Gentle",         ".   .   .   ."       },
};

}  // namespace

bool isBuiltin(int id) { return id >= 1 && id <= kBuiltinCount; }

bool isCustom(int id) {
  return id >= kCustomBase && id < kCustomBase + Limits::kRhythmSlots;
}

int customSlot(int id) { return isCustom(id) ? (id - kCustomBase) : -1; }

const char* builtinName(int id) {
  if (!isBuiltin(id)) return "Off";
  return kBuiltins[id - 1].name;
}

const char* builtinDots(int id) {
  if (!isBuiltin(id)) return "";
  return kBuiltins[id - 1].dots;
}

}  // namespace pf
