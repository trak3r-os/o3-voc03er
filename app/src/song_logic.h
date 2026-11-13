// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SONG's pure logic (docs/UI-DESIGN.md, "SONG (U9)"): editing the chain (project.h's Chain), and the playback rule, which is a state machine over
// "laps" (one pass of a pattern from its first step to its last). The screen drives it from the main loop: the step sequencer counts the laps
// it takes (StepSeq::laps()), the screen asks `next_lap` for the pattern of the lap after the one in progress and hands it to
// StepSeq::request_pattern, so a chain change lands on the pattern's end exactly as a manual PATTERN change does. No timing here, no state
// besides the Player, no heap.

#include <o3_voc03er/project.h>
#include <o3_voc03er/stepseq.h>

#include <cstdint>

namespace o3_voc03er::song {

// ---- editing -------------------------------------------------------------------------------------------------------------------------

// The row under the cursor stays valid whatever the chain holds: 0 on an empty chain, else clamped to the last entry.
inline unsigned clamp_cursor(const Chain& c, unsigned cursor) noexcept {
  return c.count == 0u ? 0u : (cursor < c.count ? cursor : c.count - 1u);
}

// UP / DOWN: `delta` rows, stopping at both ends.
inline unsigned move_cursor(const Chain& c, unsigned cursor, int delta) noexcept {
  if (c.count == 0u) {
    return 0;
  }
  const int last = static_cast<int>(c.count) - 1;
  int at = static_cast<int>(clamp_cursor(c, cursor)) + delta;
  at = at < 0 ? 0 : (at > last ? last : at);
  return static_cast<unsigned>(at);
}

// ADD / INSERT: a new entry right after the cursor's, with the cursor's pattern and one repeat (`fallback` is the pattern of an empty chain's first
// entry). `at` is the new row. False when the chain is full.
inline bool add_after(Chain& c, unsigned cursor, unsigned fallback, unsigned& at) noexcept {
  if (c.count >= kChainMax) {
    return false;
  }
  ChainEntry e;
  e.repeats = 1;
  unsigned pos = 0;
  if (c.count == 0u) {
    e.pattern = static_cast<std::uint8_t>(fallback < kBankPatterns ? fallback : 0u);
  } else {
    const unsigned cur = clamp_cursor(c, cursor);
    e.pattern = c.entry[cur].pattern;
    pos = cur + 1u;
  }
  for (unsigned i = c.count; i > pos; --i) {
    c.entry[i] = c.entry[i - 1u];
  }
  c.entry[pos] = e;
  ++c.count;
  at = pos;
  return true;
}

// DELETE: the row `at` goes and the ones below move up. `cursor` ends on the entry that took its place (the last one, if it was the last).
inline bool remove_at(Chain& c, unsigned at, unsigned& cursor) noexcept {
  if (at >= c.count) {
    return false;
  }
  for (unsigned i = at; i + 1u < c.count; ++i) {
    c.entry[i] = c.entry[i + 1u];
  }
  --c.count;
  c.entry[c.count] = ChainEntry{};  // an unused entry reads all-zero, as the file holds it
  cursor = clamp_cursor(c, at);
  return true;
}

// The encoder on a row's pattern letter A..H: stops at both ends.
inline void step_pattern(Chain& c, unsigned at, int delta) noexcept {
  if (at >= c.count) {
    return;
  }
  int v = static_cast<int>(c.entry[at].pattern) + delta;
  v = v < 0 ? 0 : (v > static_cast<int>(kBankPatterns) - 1 ? static_cast<int>(kBankPatterns) - 1 : v);
  c.entry[at].pattern = static_cast<std::uint8_t>(v);
}

// The encoder on a row's repeats 1..kChainRepeatMax: stops at both ends.
inline void step_repeats(Chain& c, unsigned at, int delta) noexcept {
  if (at >= c.count) {
    return;
  }
  int v = static_cast<int>(c.entry[at].repeats) + delta;
  v = v < 1 ? 1 : (v > static_cast<int>(kChainRepeatMax) ? static_cast<int>(kChainRepeatMax) : v);
  c.entry[at].repeats = static_cast<std::uint8_t>(v);
}

// DELETE takes two taps on the same row inside `window` ms (no undo for a chain entry): the first arms and the screen says so, the second removes.
// Any other key, a move of the cursor or the time running out disarms it.
struct Confirm {
  bool armed = false;
  std::uint8_t row = 0;
  std::uint64_t until = 0;
  // A tap on `at`: true when it is the second one (and disarms), false when it armed.
  bool tap(unsigned at, std::uint64_t now, std::uint64_t window) noexcept {
    if (armed && row == at && now < until) {
      armed = false;
      return true;
    }
    armed = true;
    row = static_cast<std::uint8_t>(at);
    until = now + window;
    return false;
  }
  bool live(std::uint64_t now) const noexcept { return armed && now < until; }
  void clear() noexcept { armed = false; }
};

// What SONG's PLAY says when the transport runs (the press stops it, it does not start the chain). Following a MIDI master, the stop is only a request to the
// device (Engine::stop, kit engine.cpp: `request_`), so the chain keeps playing until the master stops: the line says "asked", never "stopped".
inline constexpr const char* kStopAskedLine = "STOP ASKED OF THE MIDI MASTER";
constexpr const char* play_note(bool chain_active, bool following) noexcept {
  return following ? kStopAskedLine : (chain_active ? "CHAIN STOPPED. PLAY RUNS IT FROM THE CURSOR" : "STOPPED. PLAY AGAIN RUNS THE CHAIN FROM THE CURSOR");
}

// ---- playback ------------------------------------------------------------------------------------------------------------------------

// The lap in progress: entry `entry`, its `lap`th pass (0-based). `gone`: the entry was deleted while it played, so the lap in progress finishes
// and the next lap is the first of the entry now at index `entry` (the one that moved up into its place).
struct Player {
  bool active = false;
  bool gone = false;
  std::uint8_t entry = 0;
  std::uint8_t lap = 0;
};

// The pattern of `entry`'s row (an entry past the chain reads as A, never out of range).
inline unsigned pattern_of(const Chain& c, unsigned entry) noexcept {
  return entry < c.count && c.entry[entry].pattern < kBankPatterns ? c.entry[entry].pattern : 0u;
}

// PLAY on SONG from the cursor: the first lap of the cursor's entry. False on an empty chain (PLAY is then the transport's, as everywhere).
inline bool start(const Chain& c, unsigned cursor, Player& p) noexcept {
  if (c.count == 0u) {
    p = Player{};
    return false;
  }
  p.active = true;
  p.gone = false;
  p.entry = static_cast<std::uint8_t>(clamp_cursor(c, cursor));
  p.lap = 0;
  return true;
}

// The lap after the one in progress: the same entry while it has repeats left (read live: lowering REPEATS takes effect at once), else the next
// entry, else (LOOP CHAIN on) the first, else none. `entry` / `lap` receive it. False: the chain ends with the lap in progress.
inline bool next_lap(const Chain& c, const Player& p, unsigned& entry, unsigned& lap) noexcept {
  if (!p.gone && p.entry < c.count && p.lap + 1u < c.entry[p.entry].repeats) {
    entry = p.entry;
    lap = p.lap + 1u;
    return true;
  }
  const unsigned n = p.gone ? p.entry : p.entry + 1u;
  if (n < c.count) {
    entry = n;
    lap = 0;
    return true;
  }
  if (c.loop != 0u && c.count != 0u) {
    entry = 0;
    lap = 0;
    return true;
  }
  return false;
}

// A new lap has begun: move the player on to it. False when there was none (the chain was over): the player is then idle and the screen stops
// the transport once the lap in progress has played out.
inline bool advance(const Chain& c, Player& p) noexcept {
  unsigned e = 0;
  unsigned l = 0;
  if (!p.active || !next_lap(c, p, e, l)) {
    p.active = false;
    return false;
  }
  p.entry = static_cast<std::uint8_t>(e);
  p.lap = static_cast<std::uint8_t>(l);
  p.gone = false;
  return true;
}

// The chain was edited under a running player: an entry was inserted at `at` or removed at `at`; the player keeps pointing at what plays.
inline void inserted(Player& p, unsigned at) noexcept {
  if (p.active && (at < p.entry || (at == p.entry && !p.gone))) {
    ++p.entry;
  }
}
inline void removed(Player& p, unsigned at) noexcept {
  if (!p.active) {
    return;
  }
  if (at < p.entry) {
    --p.entry;
  } else if (at == p.entry) {
    p.gone = true;
  }
}

// Laps the sequencer took between two reads of its wrapping byte counter.
inline unsigned laps_between(unsigned before, unsigned now) noexcept { return (now - before) & 0xFFu; }

// A sixteenth in milliseconds at `tempo_mbpm` (the engine's milli-BPM), 125 when the tempo reads 0.
inline std::uint32_t step_ms(std::uint32_t tempo_mbpm) noexcept { return tempo_mbpm == 0u ? 125u : 15000000u / tempo_mbpm; }

}  // namespace o3_voc03er::song
