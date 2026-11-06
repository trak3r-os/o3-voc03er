// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <o3_voc03er/route.h>

#include <string>

using namespace o3_voc03er::route;

TEST_CASE("route: every pair of picker values is allowed except one slot on both sides") {
  unsigned ok = 0;
  for (std::uint8_t sp = 0; sp < 10; ++sp) {
    for (std::uint8_t so = 0; so < 10; ++so) {
      const bool want = speaks_valid(sp) && sounds_valid(so) && !(is_slot(sp) && sp == so);
      CHECK(allowed(Route{sp, so}) == want);
      ok += allowed(Route{sp, so}) ? 1u : 0u;
    }
  }
  CHECK(ok == 5u * 7u - 4u);  // 5 SPEAKS x 7 SOUNDS minus the 4 same-slot pairs
  CHECK_FALSE(speaks_valid(1));  // NONE is the unit's, not a choice
  CHECK_FALSE(speaks_valid(2));
  CHECK_FALSE(sounds_valid(0));  // FOLLOW is the pattern's
}

TEST_CASE("route: roles follow the pair") {
  Roles o = roles(Route{kSpeaksLive, kSoundsSynth});  // REALTIME
  CHECK(o.live_speaks);
  CHECK_FALSE(o.live_sounds);
  CHECK(o.adc);
  CHECK(o.synth);
  for (auto s : o.slot) CHECK(s == SlotRole::Free);

  o = roles(Route{kFirstSlot + 1, kSoundsLive});  // SLOT 2 > LIVE
  CHECK_FALSE(o.live_speaks);
  CHECK(o.live_sounds);
  CHECK(o.adc);
  CHECK_FALSE(o.synth);
  CHECK(o.slot[1] == SlotRole::Speaks);
  CHECK(o.slot[0] == SlotRole::Free);

  o = roles(Route{kFirstSlot, kFirstSlot + 3});  // SLOT 1 > SLOT 4: no ADC
  CHECK_FALSE(o.adc);
  CHECK(o.slot[0] == SlotRole::Speaks);
  CHECK(o.slot[3] == SlotRole::Sounds);

  o = roles(Route{kFirstSlot, kSoundsNoise});
  CHECK(o.synth);
  CHECK(o.noise_only);
  CHECK(roles(Route{kSpeaksLive, kSoundsLive}).adc);
  CHECK(usb_channel(Side::Speaks) == 0u);
  CHECK(usb_channel(Side::Sounds) == 1u);
}

TEST_CASE("route: the one rule dims a second live source on the other side") {
  const Row line{Kind::Source, static_cast<std::uint8_t>(LiveSrc::Line)};
  const Row live{Kind::Live, 0};
  // The mic speaks: SOUNDS lists LINE dimmed (CAPTURE FIRST, a pre-arm); its LIVE row stays open; SPEAKS' own sources are free.
  Route r{kSpeaksLive, kSoundsSynth};
  RowState s = row_state(Side::Sounds, r, line);
  CHECK(s.dimmed);
  CHECK(s.reason == Reason::CaptureFirst);
  CHECK(s.pre_arm());
  CHECK_FALSE(row_state(Side::Sounds, r, live).dimmed);
  CHECK_FALSE(row_state(Side::Speaks, r, line).dimmed);
  CHECK_FALSE(row_state(Side::Sounds, r, Row{Kind::Synth, 0}).dimmed);
  CHECK_FALSE(row_state(Side::Sounds, r, Row{Kind::Noise, 0}).dimmed);
  // LIVE on SOUNDS: SPEAKS may not take another source; with a synth there it may.
  CHECK(row_state(Side::Speaks, Route{kFirstSlot, kSoundsLive}, line).dimmed);
  CHECK(row_state(Side::Speaks, Route{kSpeaksLive, kSoundsLive}, line).dimmed);
  CHECK_FALSE(row_state(Side::Speaks, Route{kFirstSlot, kSoundsSynth}, line).dimmed);
  // No live source on either side: nothing dims.
  r = Route{kFirstSlot, kFirstSlot + 1};
  CHECK_FALSE(row_state(Side::Speaks, r, line).dimmed);
  CHECK_FALSE(row_state(Side::Sounds, r, line).dimmed);
}

TEST_CASE("route: a slot on the other side is dimmed IN USE") {
  const Route r{kFirstSlot + 2, kSoundsSynth};
  const RowState s = row_state(Side::Sounds, r, Row{Kind::Slot, 2});
  CHECK(s.dimmed);
  CHECK(s.reason == Reason::SlotInUse);
  CHECK_FALSE(s.pre_arm());
  CHECK_FALSE(row_state(Side::Sounds, r, Row{Kind::Slot, 1}).dimmed);
  CHECK_FALSE(row_state(Side::Speaks, r, Row{Kind::Slot, 2}).dimmed);  // its own slot
  CHECK(std::string(reason_text(Reason::CaptureFirst)) == "CAPTURE FIRST");
}

TEST_CASE("route: the picker rows and picking") {
  Row rs[kMaxRows];
  CHECK(rows(Side::Speaks, LiveSrc::MicLow, rs, kMaxRows) == 1u + 6u + 4u);
  CHECK(rows(Side::Sounds, LiveSrc::MicLow, rs, kMaxRows) == 2u + 1u + 6u + 4u);
  CHECK(rows(Side::Sounds, LiveSrc::MicLow, rs, 0) == kMaxRows);  // counts without writing
  CHECK(rs[0].kind == Kind::Synth);
  CHECK(rs[2].kind == Kind::Live);
  for (unsigned i = 3; i < 9; ++i) CHECK((rs[i].kind == Kind::Source && rs[i].arg != static_cast<unsigned>(LiveSrc::MicLow)));
  CHECK(rs[9].kind == Kind::Slot);

  Route r{kSpeaksLive, kSoundsSynth};
  CHECK(pick(Side::Sounds, r, Row{Kind::Slot, 0}));  // MIC > SLOT 1
  CHECK(r.sounds == kFirstSlot);
  CHECK_FALSE(pick(Side::Speaks, r, Row{Kind::Slot, 0}));  // in use
  CHECK(r.speaks == kSpeaksLive);
  CHECK(pick(Side::Speaks, r, Row{Kind::Slot, 1}));
  CHECK(r.speaks == kFirstSlot + 1);
  CHECK(pick(Side::Sounds, r, Row{Kind::Live, 0}));  // SLOT 2 > LIVE
  CHECK(r.sounds == kSoundsLive);
  CHECK_FALSE(pick(Side::Speaks, r, Row{Kind::Source, 0}));  // CAPTURE FIRST: refused here, the screen arms a capture
  CHECK(pick(Side::Speaks, r, Row{Kind::Live, 0}));  // LIVE both sides, the same source
  CHECK(r.speaks == kSpeaksLive);
  CHECK(roles(r).live_speaks);
  CHECK(roles(r).live_sounds);
}
