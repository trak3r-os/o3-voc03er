// SPDX-License-Identifier: GPL-3.0-or-later
// NO1, note owners: the unit holds a note until its last holder lets go. Pads, the step sequencer, the console and the user's MIDI are four
// owners (bits 4-5 of Event.flags); a NoteOn sets its owner's bit, a NoteOff clears it, and the note ends only when no owner is left. A real
// Vocoder in the engine (the way midi_slices_test.cpp builds one); the observables are the unit's held count and owner masks, the carrier's
// voices and TUNE's NOTES IN targets. Synthetic signals only.
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/engine.h>

#include <cstdint>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
using Owner = Vocoder::Owner;

namespace {

constexpr unsigned kBlock = 64;
constexpr std::uint8_t kM = 60;

struct Rig {
  dsp::Engine e;
  Vocoder v;
  dsp::UnitId id = dsp::kNoUnit;
  bool ok = false;
  Rig() {
    id = e.attach(v);
    ok = e.set_source(0, id);
    e.start();
    (void)e.set_param(id, Vocoder::CarSrc, 1.0f);  // SYNTH
    run(1);
  }
  void run(unsigned blocks) {
    std::vector<float> l(kBlock, 0.0f), r(kBlock, 0.0f);
    for (unsigned b = 0; b < blocks; ++b) {
      e.render(nullptr, nullptr, l.data(), r.data(), kBlock);
    }
  }
  void on(Owner o, std::uint8_t note = kM) {
    REQUIRE(e.inject(id, Vocoder::note_on_owned(note, 100, o)));
    run(1);
  }
  void off(Owner o, std::uint8_t note = kM) {
    REQUIRE(e.inject(id, Vocoder::note_off_owned(note, o)));
    run(1);
  }
  void midi_on(std::uint8_t note = kM) {
    REQUIRE(e.inject(id, dsp::note_on(note, 100)));
    run(1);
  }
  void midi_off(std::uint8_t note = kM) {
    REQUIRE(e.inject(id, dsp::note_off(note)));
    run(1);
  }
  void all_off() {
    REQUIRE(e.inject(id, dsp::all_notes_off()));
    run(1);
  }
  bool held(std::uint8_t note = kM) const { return v.carrier().holds(note); }
  // A released voice has gone idle once its release tail is over.
  bool idle_after_release() {
    run(2000);  // ~2.9 s at 44.1 kHz: past any release
    return v.carrier().sounding_voices() == 0u;
  }
};

}  // namespace

TEST_CASE("owners: the flags are bits 4-5, clear of the kit's bits and kNoteLocal / kNoteSlice") {
  static_assert((Vocoder::kOwnerBits & (dsp::kNoteAccent | dsp::kNoteSlide | Vocoder::kNoteLocal | Vocoder::kNoteSlice)) == 0u);
  CHECK(Vocoder::note_on_owned(kM, 100, Owner::Seq).flags == (Vocoder::kNoteLocal | (2u << 4)));
  CHECK(Vocoder::note_off_owned(kM, Owner::Seq).flags == Vocoder::note_on_owned(kM, 100, Owner::Seq).flags);
  CHECK(Vocoder::note_off_local(kM).flags == Vocoder::kNoteLocal);  // owner Midi: what it was
}

TEST_CASE("owners: a pad and the sequencer on one note, the sequencer lets go first: the pad's note sounds on") {
  Rig r;
  REQUIRE(r.ok);
  r.on(Owner::Pads);
  r.on(Owner::Seq);  // shared: no second voice, no retrigger
  CHECK(r.v.carrier().sounding_voices() == 1u);
  CHECK(r.v.held_count() == 1u);
  CHECK(r.v.owners_of(kM) == 0b0110u);  // bit 1 Pads, bit 2 Seq
  r.off(Owner::Seq);
  r.run(100);
  CHECK(r.held());
  CHECK(r.v.held_count() == 1u);
  CHECK(r.v.owners_of(kM) == 0b0010u);
  r.off(Owner::Pads);
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
  CHECK(r.v.owners_of(kM) == 0u);
  CHECK(r.idle_after_release());
}

TEST_CASE("owners: the reverse order, the pad lets go first: the sequencer's note sounds on") {
  Rig r;
  r.on(Owner::Pads);
  r.on(Owner::Seq);
  r.off(Owner::Pads);
  r.run(100);
  CHECK(r.held());
  CHECK(r.v.held_count() == 1u);
  r.off(Owner::Seq);
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
  CHECK(r.idle_after_release());
}

TEST_CASE("owners: a sequencer repeat under a held pad neither restarts nor ends the note") {
  Rig r;
  r.on(Owner::Pads);
  for (int i = 0; i < 4; ++i) {  // a ratchet: on, off, on, off ...
    r.on(Owner::Seq);
    r.off(Owner::Seq);
    CHECK(r.held());
    CHECK(r.v.carrier().sounding_voices() == 1u);
  }
  r.off(Owner::Pads);
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
}

TEST_CASE("owners: one owner's double NoteOn ends with one NoteOff") {
  Rig r;
  r.on(Owner::Pads);
  r.on(Owner::Pads);
  CHECK(r.v.carrier().sounding_voices() == 1u);
  CHECK(r.v.held_count() == 1u);
  r.off(Owner::Pads);
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
  CHECK(r.v.owners_of(kM) == 0u);
  CHECK(r.idle_after_release());
}

TEST_CASE("owners: AllNotesOff with two owners is silent, and the next pair of one owner is normal") {
  Rig r;
  r.on(Owner::Pads);
  r.on(Owner::Seq);
  r.all_off();
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
  CHECK(r.v.owners_of(kM) == 0u);
  CHECK(r.idle_after_release());
  r.on(Owner::Seq);
  CHECK(r.held());
  CHECK(r.v.held_count() == 1u);
  r.off(Owner::Seq);  // no stale Pads bit keeps it
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
}

TEST_CASE("owners: a ninth note with owners, everything released: nothing stuck, the masks are clean") {
  Rig r;
  for (std::uint8_t k = 0; k < 9; ++k) {
    r.on(Owner::Pads, static_cast<std::uint8_t>(60 + k));
    r.on(Owner::Seq, static_cast<std::uint8_t>(60 + k));
  }
  CHECK(r.v.held_count() == 8u);  // the ninth never entered held_
  for (std::uint8_t k = 0; k < 9; ++k) {
    r.off(Owner::Seq, static_cast<std::uint8_t>(60 + k));
    r.off(Owner::Pads, static_cast<std::uint8_t>(60 + k));
  }
  CHECK(r.v.held_count() == 0u);
  for (std::uint8_t k = 0; k < 9; ++k) {
    CHECK(r.v.owners_of(static_cast<std::uint8_t>(60 + k)) == 0u);
  }
  CHECK(r.idle_after_release());
  // A ninth note that is stolen and then wiped by AllNotesOff leaves no stale bit that blocks a later hold.
  for (std::uint8_t k = 0; k < 9; ++k) {
    r.on(Owner::Pads, static_cast<std::uint8_t>(40 + k));
  }
  r.all_off();
  r.on(Owner::Pads, 48);
  r.off(Owner::Pads, 48);
  CHECK(r.v.owners_of(48) == 0u);
  CHECK(r.v.held_count() == 0u);
  CHECK(r.idle_after_release());
}

TEST_CASE("owners: unmarked MIDI and a pad on one note") {
  Rig r;
  r.midi_on();
  r.on(Owner::Pads);
  CHECK(r.v.carrier().sounding_voices() == 1u);
  CHECK(r.v.owners_of(kM) == 0b0011u);
  r.midi_off();  // the key lets go, the pad still holds
  r.run(100);
  CHECK(r.held());
  r.off(Owner::Pads);
  CHECK_FALSE(r.held());
  r.on(Owner::Pads);  // and the other way round
  r.midi_on();
  r.off(Owner::Pads);
  r.run(100);
  CHECK(r.held());
  r.midi_off();
  CHECK_FALSE(r.held());
  CHECK(r.v.held_count() == 0u);
}

TEST_CASE("owners: a stray NoteOff of an owner that does not hold the note does not cut the holder") {
  Rig r;
  r.on(Owner::Pads);
  r.off(Owner::Seq);
  r.midi_off();
  r.run(100);
  CHECK(r.held());
  r.off(Owner::Pads);
  CHECK_FALSE(r.held());
}

TEST_CASE("owners: TUNE's NOTES IN keeps a note a target until its last holder lets go") {
  Rig r;
  r.on(Owner::Pads);
  r.on(Owner::Seq);
  CHECK(r.v.tune().held_count() == 1u);
  r.off(Owner::Seq);
  CHECK(r.v.tune().held_count() == 1u);
  r.off(Owner::Pads);
  CHECK(r.v.tune().held_count() == 0u);
  r.on(Owner::Seq, 64);
  r.on(Owner::Pads, 64);
  r.off(Owner::Pads, 64);
  CHECK(r.v.tune().held_count() == 1u);
  r.all_off();
  CHECK(r.v.tune().held_count() == 0u);
}
