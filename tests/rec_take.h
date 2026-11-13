// SPDX-License-Identifier: GPL-3.0-or-later
// SAMPLE REC in the tests. The app is one static object for the whole test process, so the screen keeps its LENGTH, SLOT and AUTOPLAY from the test before (and the
// executable holds every test file). These helpers state what a take needs without reading the screen: UP / DOWN always drive SLOT, LEFT / RIGHT step the lit F key
// (the focus outlives a test too, so LENGTH is lit again with a tap on F3 first); AUTOPLAY is set by the arrows after a tap on F4 (the state is tracked here too, one value for the process, the screen's own default is ON).
#pragma once

#include <o3sdk/app/app.h>
#include <o3sdk/sim/harness.h>

namespace o3_voc03er::test {

inline void rec_tap(o3sdk::sim::Harness& h, o3sdk::app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}

inline bool& rec_autoplay_on() {
  static bool on = true;
  return on;
}

// F4 AUTOPLAY set to `on`, with SAMPLE REC open, whatever it was: a tap lights the key (or switches it, when it was lit), then LEFT sets it off or RIGHT on.
inline void rec_autoplay(o3sdk::sim::Harness& h, bool on) {
  rec_tap(h, o3sdk::app::Button::F4);
  rec_tap(h, on ? o3sdk::app::Button::Right : o3sdk::app::Button::Left);
  rec_autoplay_on() = on;
}

// LENGTH at FREE from whatever it was and whatever key was lit: a tap on F3 lights LENGTH (or steps it on, when it was lit already), then LEFT stops at FREE.
inline void rec_length_free(o3sdk::sim::Harness& h) {
  rec_tap(h, o3sdk::app::Button::F3);
  for (int i = 0; i < 5; ++i) {
    rec_tap(h, o3sdk::app::Button::Left);
  }
}

// LENGTH at 1 BAR: from FREE, one RIGHT.
inline void rec_length_one_bar(o3sdk::sim::Harness& h) {
  rec_length_free(h);
  rec_tap(h, o3sdk::app::Button::Right);
}

// SLOT at `n` (0..3) from whatever it was: UP stops at the first slot, DOWN steps on.
inline void rec_slot(o3sdk::sim::Harness& h, unsigned n) {
  for (int i = 0; i < 3; ++i) {
    rec_tap(h, o3sdk::app::Button::Up);
  }
  for (unsigned i = 0; i < n; ++i) {
    rec_tap(h, o3sdk::app::Button::Down);
  }
}

}  // namespace o3_voc03er::test
