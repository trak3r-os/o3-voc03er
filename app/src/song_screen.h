// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SONG (docs/UI-DESIGN.md, "SONG (U9)"), in the OR NOIR skin: the chain of up to 16 entries (a pattern letter and a repeat count, a row each),
// two columns of eight. The chain is the app's one Chain (project.h, in the SDRAM, so FILE can save it); the screen edits it through pointer.
//
//   UP / DOWN   the row (the cursor).        ENCODER   the row's pattern letter A..H; after F2, its repeats 1..64 (F2 again: the letter).
//   LEFT / RIGHT   one encoder step of that (the letter, or the repeats while F2 is lit); a held arrow repeats.
//   INSERT, F1 ADD   a row after the cursor's, with its pattern.          DELETE   the row under the cursor, on a second tap (a line says so).
//   F3 LOOP CHAIN   toggles (the chain goes back to its first entry when it ends).          F8 PERFORM   (labelled so: SONG has no opener to go back to).
//   PLAY   the app's (app.cpp hands it to start() while this screen is up and the transport is stopped): the chain plays from the cursor's row.
//          While the transport already runs PLAY is the transport's, as everywhere: it stops (the chain with it) and the screen says so; the next PLAY
//          starts the chain from the cursor. (UX3-7: one rule, never a silent no-op; no restart from the cursor in mid-lap, that would cut the lap.)
//
// Two micro lines above the F row (as PATTERN's) say what the keys do; a note (DELETE's ask, the chain is full, ...) takes their place for two seconds.
//
// How a chain change lands: the step sequencer counts every pattern it takes (StepSeq::laps()); service() walks song_logic.h's player on that
// count and, each time a lap begins, asks StepSeq::request_pattern for the pattern of the lap after it. That is the PATTERN screen's own request,
// so the change lands on the pattern's last step as a manual one does: no timing path of its own, nothing added to the render but one byte
// store per pattern end. The row playing is drawn lit (it leads the audio by one step: the sequencer counts at a pattern's last step); the
// cursor is free. At the end of a chain that does not loop the transport stops a step's seven eighths after the last lap's end was seen.
// Leaving the screen does not end the walking: App::update calls service() every pass, so the chain plays on under any screen; PLAY stops it.
//
// Main loop only. The state is about 64 bytes of SRAM_U; the chain and the player's counters are not shared with the render context.

#include "song_logic.h"

#include <o3_voc03er/stepseq.h>

#include <o3sdk/app/help.h>
#include <o3sdk/app/screen.h>
#include <o3sdk/dsp/engine.h>

#include <cstdint>

namespace o3_voc03er {

class SongScreen final : public o3sdk::app::Screen {
 public:
  // `chain` is the app's (O3SDK_BIG, all-zero at boot); `seq` the step sequencer hook; `back` where F8 goes (PERFORM); `voice` for the header.
  void init(o3sdk::dsp::UnitId voice, Chain* chain, StepSeq* seq, o3sdk::app::Screen* back) noexcept;

  const char* title() const noexcept override { return "SONG"; }
  o3sdk::app::Chrome chrome(const o3sdk::app::Context&) const noexcept override { return o3sdk::app::Chrome::None; }
  const o3sdk::ui::Theme* theme(const o3sdk::app::Context&) const noexcept override;
  void enter(o3sdk::app::Context& ctx) noexcept override;
  void input(o3sdk::app::Context& ctx, const o3sdk::app::Input& in) noexcept override;
  std::uint64_t repeat_buttons(const o3sdk::app::Context&) const noexcept override;  // LEFT / RIGHT: a held arrow repeats
  void draw(o3sdk::app::Context& ctx, o3sdk::ui::Renderer& r, const o3sdk::ui::Rect& area) noexcept override;
  void soft_keys(o3sdk::app::Context& ctx, o3sdk::app::SoftKeys& out) noexcept override;
  void help(const o3sdk::app::Context& ctx, o3sdk::app::Help& help) const noexcept override;
  void update(o3sdk::app::Context& ctx) noexcept override;
  // Every pass of the main loop, from App::update, whichever screen is up: the chain's walking (laps, advance, the stop at its end).
  void service(o3sdk::app::Context& ctx) noexcept;

  // PLAY while this screen is up (app.cpp): with the transport stopped and the chain not empty, queue the cursor's entry and start the
  // transport; true when it did. Otherwise false and the app's PLAY does what it does everywhere.
  bool start(o3sdk::app::Context& ctx) noexcept;

  unsigned cursor() const noexcept { return cursor_; }
  bool editing_repeats() const noexcept { return repeats_focus_; }
  bool playing_chain() const noexcept { return player_.active; }
  // The row lit (the lap in progress), or kChainMax when the chain is not playing.
  unsigned playing_row() const noexcept { return player_.active ? player_.entry : kChainMax; }
  unsigned playing_lap() const noexcept { return player_.lap; }

  // LE4: FILE's load put another chain in place. A player that walks it starts over at the new chain's first row: the lap in progress plays out and
  // the next lap is that row's first (a lap that has not begun yet starts it), the cursor is clamped, and the next request is made again.
  void chain_replaced() noexcept;

  // A refusal App::input makes for a key it handled itself (SAMPLE LOAD while FILE runs a job): said on this screen's own line.
  void notice(o3sdk::app::Context& ctx, const char* text) noexcept { say(ctx, text); }

 private:
  void say(o3sdk::app::Context& ctx, const char* text) noexcept;
  void sync_ahead() noexcept;  // the request for the lap after the one in progress (after every edit and every lap)
  void halt() noexcept;        // the walking ends; the pending request goes
  void edit(o3sdk::app::Context& ctx, int delta) noexcept;
  void add(o3sdk::app::Context& ctx) noexcept;
  void remove(o3sdk::app::Context& ctx) noexcept;
  void disarm() noexcept;  // the DELETE ask goes (another key, the cursor moved) with its line

  o3sdk::dsp::UnitId voice_ = o3sdk::dsp::kNoUnit;
  Chain* chain_ = nullptr;
  StepSeq* seq_ = nullptr;
  o3sdk::app::Screen* back_ = nullptr;
  song::Player player_;
  std::uint8_t cursor_ = 0;
  std::uint8_t last_laps_ = 0;  // StepSeq::laps() as the last pass read it
  bool first_ = false;          // the lap begin of the Play itself is still to come (it moves nothing)
  bool repeats_focus_ = false;  // the encoder drives the repeats (F2) instead of the letter
  song::Confirm confirm_;       // DELETE's ask
  bool stop_pending_ = false;   // the chain ended: the transport stops at stop_at_
  const char* note_ = nullptr;
  std::uint64_t stop_at_ = 0;
  std::uint64_t note_until_ = 0;
};

}  // namespace o3_voc03er
