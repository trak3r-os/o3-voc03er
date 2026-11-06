// SPDX-License-Identifier: GPL-3.0-or-later
// PERFORM on the engine: a chord pad sounds a chord, SHIFT latches it, DELETE lets it go; a frame fits the display list.
#include <doctest/doctest.h>

#include <o3sdk/app/app.h>
#include <o3sdk/app/runtime.h>
#include <o3sdk/sim/harness.h>

#include <memory>

namespace app = o3sdk::app;
namespace sim = o3sdk::sim;

namespace {
void tap(sim::Harness& h, app::Button b) {
  h.press(b);
  h.run_ms(5);
  h.release(b);
  h.run_ms(5);
}
}  // namespace

TEST_CASE("perform: a held chord pad lights, SHIFT latches it, DELETE releases it") {
  auto h = std::make_unique<sim::Harness>(::o3sdk_app());
  REQUIRE(h->start());
  h->run_ms(100);
  h->pad_down(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 255);
  tap(*h, app::Button::Shift);
  h->pad_up(26);
  h->run_ms(100);
  CHECK(h->leds().level(26) == 140);  // latched: Mid
  tap(*h, app::Button::Delete);
  h->run_ms(100);
  CHECK(h->leds().level(26) != 140);
  CHECK(h->runtime().frame_failures() == 0u);
  CHECK(h->display().peak_frame_words() < 2048u);
}
