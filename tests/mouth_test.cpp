// SPDX-License-Identifier: GPL-3.0-or-later
// LT1: the live voice drives TRACT's tube (SOURCE LIVE). The voice under test is synthetic: the kit's own tube excited by a tilted impulse train, so the
// vowel it says is known to the hertz (the tube's impulse response), and the live path is judged against that truth. Host only (tract_probe.h uses double).
#include <doctest/doctest.h>

#include <o3_voc03er/modes.h>
#include <o3_voc03er/units/mouth/link.h>
#include <o3_voc03er/units/tract/tract.h>
#include <o3_voc03er/units/vocoder/mouth_live.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>
#include <o3sdk/extras/tract/processor.h>
#include <tract_probe.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;
namespace probe = o3sdk::extras::tract::probe;
using o3sdk::extras::tract::Processor;

namespace {

constexpr float kFs = 44100.0f;

// BEGIN default digest
// The chain in TRACT mode, the default MOUTH (PADS): the Vocoder then the TractUnit in place, a saw plus noise at the modulator input, two held notes,
// the tube's every parameter moved. FNV-1a of the bit patterns of both planes.
std::uint32_t chain_digest(MouthLink* bound, bool live) {
  static Vocoder vo;
  static TractUnit tu;
  vo.prepare(kFs);
  tu.prepare(kFs);
  vo.bind_mouth(bound);
  tu.bind_mouth(bound);
  std::uint32_t d = 2166136261u;
  std::uint32_t seed = 4321u;
  float ph = 0.0f;
  dsp::Transport tr;
  for (std::uint32_t b = 0; b < 700; ++b) {
    float m[64], l[64], r[64];
    for (float& x : m) {
      seed = seed * 1664525u + 1013904223u;
      ph += 120.0f / kFs;
      ph -= std::floor(ph);
      x = 0.25f * (2.0f * ph - 1.0f) + 0.02f * (static_cast<float>(seed >> 8) * (1.0f / 8388608.0f) - 1.0f);
    }
    dsp::Event ve[4], te[4];
    std::uint32_t nv = 0, nt = 0;
    if (b == 0) {
      ve[nv++] = dsp::note_on(48, 100);
      ve[nv++] = dsp::note_on(55, 100);
      ve[nv++] = dsp::param_change(Vocoder::Mode, 4.0f);
      te[nt++] = dsp::param_change(TractUnit::Mix, 1.0f);
      if (live) te[nt++] = dsp::param_change(TractUnit::Source, 1.0f);
    }
    if (b == 100) te[nt++] = dsp::param_change(TractUnit::Vowel, 0.5f);
    if (b == 160) te[nt++] = dsp::param_change(TractUnit::Tongue, 0.6f);
    if (b == 200) te[nt++] = dsp::param_change(TractUnit::Jaw, -0.5f);
    if (b == 240) te[nt++] = dsp::param_change(TractUnit::Lips, 0.8f);
    if (b == 300) te[nt++] = dsp::param_change(TractUnit::Close, 1.0f);
    if (b == 330) te[nt++] = dsp::param_change(TractUnit::Close, 0.0f);
    if (b == 360) te[nt++] = dsp::param_change(TractUnit::Size, 0.0f);
    if (b == 400) te[nt++] = dsp::param_change(TractUnit::Accent, 0.7f);
    if (b == 440) te[nt++] = dsp::param_change(TractUnit::Slide, 120.0f);
    if (b == 480) te[nt++] = dsp::param_change(TractUnit::Res, 0.9f);
    if (b == 500) te[nt++] = dsp::param_change(TractUnit::Hiss, 1.0f);
    if (b == 520) te[nt++] = dsp::param_change(TractUnit::Vowel, 1.0f);
    if (b == 600) te[nt++] = dsp::param_change(TractUnit::Mix, 0.5f);
    dsp::ProcessArgs a{};
    a.events = ve;
    a.event_count = nv;
    a.in_l = m;
    a.in_r = m;
    a.out_l = l;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    vo.process(a);
    dsp::ProcessArgs c{};
    c.events = te;
    c.event_count = nt;
    c.in_l = l;
    c.in_r = r;
    c.out_l = l;
    c.out_r = r;
    c.frames = 64;
    c.transport = &tr;
    tu.process(c);
    for (std::size_t i = 0; i < 64; ++i) {
      std::uint32_t u, w;
      std::memcpy(&u, &l[i], sizeof u);
      std::memcpy(&w, &r[i], sizeof w);
      d = (d ^ u) * 16777619u;
      d = (d ^ w) * 16777619u;
    }
  }
  return d;
}
// END default digest

// The synthetic voice: a 120 Hz impulse train, tilted by a one-pole low-pass (a glottal source falls about 6 dB per octave; Lpc's pre-emphasis assumes
// that), through the kit's tube at `vowel`. tilt 0 is the flat train (no source model), the limit the docs name.
std::vector<float> make_voice(float vowel, float tilt, unsigned n) {
  Processor p(Processor::Quality::Full);
  p.prepare(kFs);
  p.set_hiss(0.0f);
  p.set_vowel(vowel);
  p.set_mix(1.0f);
  p.reset();
  std::vector<float> v(n, 0.0f);
  float lp = 0.0f;
  for (unsigned i = 0; i < n; ++i) {
    const float e = (i % (44100u / 120u) == 0u) ? 1.0f : 0.0f;
    lp = tilt * lp + (1.0f - tilt) * e;
    v[i] = tilt > 0.0f ? 4.0f * lp : e;
  }
  p.process(v.data(), v.data(), n);
  return v;
}

std::vector<double> impulse_of(Processor& p, unsigned len) {
  std::vector<float> x(len, 0.0f);
  x[0] = 1.0f;
  p.process(x.data(), x.data(), len);
  return std::vector<double>(x.begin(), x.end());
}

void setup(Processor& p) {
  p.prepare(kFs);
  p.set_hiss(0.0f);
  p.set_mix(1.0f);
}

// Peaks (Hz) of the response of a Processor: the truth at a vowel, or a tube driven by link areas.
std::vector<double> truth_peaks(float vowel) {
  Processor p(Processor::Quality::Full);
  setup(p);
  p.set_vowel(vowel);
  p.reset();
  return probe::peaks(probe::magnitude(impulse_of(p, 4096), 2048), 22050.0, 0.02);
}

std::vector<double> live_response(const MouthLink& l) {
  Processor p(Processor::Quality::Full);
  setup(p);
  p.reset();
  p.set_areas(l.area, l.n);
  std::vector<float> w(2048, 0.0f);
  p.process(w.data(), w.data(), 2048);  // settle, as the unit would have
  p.reset();
  p.set_areas(l.area, l.n);
  return impulse_of(p, 4096);
}

// The chain: the Vocoder then the TractUnit, in place, 64 frames a block, the link bound or not.
struct Rig {
  Vocoder& vo;
  TractUnit& tu;
  MouthLink link{};
  dsp::Transport tr;
  Rig(bool bind, std::uint32_t mode) : vo(vocoder()), tu(tract()) {
    vo.prepare(kFs);
    tu.prepare(kFs);
    vo.bind_mouth(bind ? &link : nullptr);
    tu.bind_mouth(bind ? &link : nullptr);
    dsp::Event ve = dsp::param_change(Vocoder::Mode, static_cast<float>(mode));
    dsp::Event te = dsp::param_change(TractUnit::Mix, 1.0f);
    float z[64] = {}, l[64], r[64];
    block(z, &ve, 1, &te, 1, l, r);
  }
  static Vocoder& vocoder() { static Vocoder v; return v; }
  static TractUnit& tract() { static TractUnit t; return t; }
  void block(const float* mod, const dsp::Event* ve, std::uint32_t nv, const dsp::Event* te, std::uint32_t nt, float* l, float* r) {
    dsp::ProcessArgs a{};
    a.events = ve;
    a.event_count = nv;
    a.in_l = mod;
    a.in_r = mod;
    a.out_l = l;
    a.out_r = r;
    a.frames = 64;
    a.transport = &tr;
    vo.process(a);
    dsp::ProcessArgs c{};
    c.events = te;
    c.event_count = nt;
    c.in_l = l;
    c.in_r = r;
    c.out_l = l;
    c.out_r = r;
    c.frames = 64;
    c.transport = &tr;
    tu.process(c);
  }
  void tick(const float* mod, float* l, float* r) { block(mod, nullptr, 0, nullptr, 0, l, r); }
};

bool finite_shape(const MouthLink& k) {
  if (k.n < 2u || k.n > MouthLink::kMaxAreas) return false;
  for (std::uint32_t i = 0; i < k.n; ++i)
    if (!std::isfinite(k.area[i]) || k.area[i] < 0.0f || k.area[i] > 1.0001f) return false;
  return true;
}

struct Formants {
  double err[3] = {1e9, 1e9, 1e9};  // percent, live against truth, F1 to F3
  double truth[3] = {0, 0, 0}, live[3] = {0, 0, 0};
};

Formants compare(const std::vector<double>& truth, const std::vector<double>& live_h) {
  Formants f;
  const std::vector<double> got = probe::peaks(probe::magnitude(live_h, 2048), 22050.0, 0.02);
  for (std::size_t i = 0; i < 3 && i < truth.size(); ++i) {
    double best = 1e9;
    for (double g : got)
      if (std::fabs(g - truth[i]) < std::fabs(best - truth[i])) best = g;
    f.truth[i] = truth[i];
    f.live[i] = best;
    f.err[i] = 100.0 * (best - truth[i]) / truth[i];
  }
  return f;
}

// Runs the voice through the chain in TRACT mode with SOURCE LIVE and returns the shape snapshots of the last `snaps` blocks stepped by 8.
std::vector<MouthLink> run_live(const std::vector<float>& voice, unsigned snaps) {
  Rig g(true, kModeTract);
  dsp::Event ve = dsp::note_on(48, 100);
  dsp::Event te = dsp::param_change(TractUnit::Source, 1.0f);
  std::vector<MouthLink> out;
  float l[64], r[64];
  const unsigned nb = static_cast<unsigned>(voice.size()) / 64u;
  for (unsigned b = 0; b < nb; ++b) {
    if (b == 0) g.block(&voice[0], &ve, 1, &te, 1, l, r);
    else g.tick(&voice[b * 64u], l, r);
    if (b + snaps * 8u >= nb && (nb - b) % 8u == 0u) out.push_back(g.link);
  }
  return out;
}

}  // namespace

TEST_CASE("mouth: the default (SOURCE PADS) is the chain of HEAD, bound or not: digest 3546222B") {
  const std::uint32_t unbound = chain_digest(nullptr, false);
  MouthLink k{};
  const std::uint32_t pads = chain_digest(&k, false);
  std::printf("mouth: chain digest unbound %08X, bound PADS %08X\n", unbound, pads);
  CHECK(unbound == 0x3546222Bu);  // pinned on the tree before LT1 (Talk's analysis moved to Lpc, the tube given an areas entry)
  CHECK(pads == unbound);          // a bound link on PADS is the same stream bit for bit
  CHECK(!k.valid);                 // nobody listened
  CHECK(k.want == false);
  const std::uint32_t live = chain_digest(&k, true);
  CHECK(live != unbound);          // LIVE does something
  CHECK(k.valid);
}

TEST_CASE("mouth: SOURCE is the last parameter of TRCT, default PADS, not a saved one") {
  static TractUnit tu;
  tu.prepare(kFs);
  CHECK(TractUnit::Source == TractUnit::kParamCount - 1);
  CHECK(tu.info().params[TractUnit::Source].def == 0.0f);
  CHECK(tu.info().params[TractUnit::Source].max == 1.0f);
  CHECK(!tu.mouth_live());
  MouthLink k{};
  tu.bind_mouth(&k);
  dsp::Event e[2] = {dsp::param_change(TractUnit::Mix, 1.0f), dsp::param_change(TractUnit::Source, 1.0f)};
  float l[64] = {}, r[64] = {};
  dsp::Transport tr;
  dsp::ProcessArgs a{};
  a.events = e;
  a.event_count = 2;
  a.in_l = l;
  a.in_r = r;
  a.out_l = l;
  a.out_r = r;
  a.frames = 64;
  a.transport = &tr;
  tu.process(a);
  CHECK(tu.mouth_live());
  a.events = nullptr;
  a.event_count = 0;
  tu.process(a);
  CHECK(k.want);  // asks every block while live, even in silence (the tube sleeps, the question does not)
  dsp::Event off = dsp::param_change(TractUnit::Mix, 0.0f);
  a.events = &off;
  a.event_count = 1;
  tu.process(a);
  CHECK(!tu.mouth_live());  // MIX at 0: the tube sleeps and does not ask
  tu.bind_mouth(nullptr);
}

TEST_CASE("mouth: the voice's formants come through: F1 to F3 error per vowel (tilted source), and the flat-source limit") {
  const float vowels[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
  const std::vector<float> tilts = {0.96f, 0.0f};
  for (float tilt : tilts) {
    double worst[3] = {0, 0, 0};
    for (float v : vowels) {
      const std::vector<double> truth = truth_peaks(v);
      REQUIRE(truth.size() >= 3);
      const std::vector<float> voice = make_voice(v, tilt, 44100);
      const std::vector<MouthLink> snaps = run_live(voice, 6);
      REQUIRE(snaps.size() >= 4);
      double mx[3] = {0, 0, 0}, mean[3] = {0, 0, 0};
      Formants last;
      for (const MouthLink& k : snaps) {
        REQUIRE(k.valid);
        REQUIRE(finite_shape(k));
        const Formants f = compare(truth, live_response(k));
        last = f;
        for (int i = 0; i < 3; ++i) {
          mx[i] = std::max(mx[i], std::fabs(f.err[i]));
          mean[i] += f.err[i] / static_cast<double>(snaps.size());
        }
      }
      std::printf("mouth: tilt %.2f vowel %.2f truth %4.0f %4.0f %4.0f Hz, live %4.0f %4.0f %4.0f Hz, err of the last shape %+5.1f %+5.1f %+5.1f %%, worst of %zu shapes %4.1f %4.1f %4.1f %%\n",
                  double(tilt), double(v), last.truth[0], last.truth[1], last.truth[2], last.live[0], last.live[1], last.live[2], last.err[0], last.err[1],
                  last.err[2], snaps.size(), mx[0], mx[1], mx[2]);
      for (int i = 0; i < 3; ++i) worst[i] = std::max(worst[i], mx[i]);
    }
    std::printf("mouth: tilt %.2f worst over the five vowels: F1 %.1f %%, F2 %.1f %%, F3 %.1f %%\n", double(tilt), worst[0], worst[1], worst[2]);
    // Pins at about 1.6x what the first run measured (F1 12.6, F2 3.6, F3 1.8 %): a regression guard, not a spec. The flat source (tilt 0) has a loose ceiling only:
    // Lpc's pre-emphasis assumes a falling source, a flat train reads high (F1 up to 104 %, F2 57 %, F3 34 % off), and the docs say so.
    if (tilt > 0.0f) {
      CHECK(worst[0] < 20.0);
      CHECK(worst[1] < 6.0);
      CHECK(worst[2] < 3.0);
    } else {
      // The flat source is the documented limit, not a spec: a loose ceiling at about 1.5x the first run (F1 103.7, F2 57.2, F3 33.7 %), so a change
      // that makes it worse shows, and one that makes it better does not fail.
      CHECK(worst[0] < 160.0);
      CHECK(worst[1] < 90.0);
      CHECK(worst[2] < 55.0);
    }
  }
}

namespace {

double rms_of(const std::vector<float>& x, std::size_t from, std::size_t to) {
  double s = 0;
  for (std::size_t i = from; i < to; ++i) s += double(x[i]) * double(x[i]);
  return std::sqrt(s / double(to - from));
}

bool bits_equal(const MouthLink& a, const MouthLink& b) {
  return a.n == b.n && std::memcmp(a.area, b.area, sizeof(float) * a.n) == 0;
}

// Blocks of `mod` (a repeating source, whatever the lambda says) through a LIVE chain, a note held, collecting the left plane.
template <class Gen>
std::vector<float> run_chain(Rig& g, unsigned blocks, Gen gen, bool live, bool note = true) {
  std::vector<float> out;
  out.reserve(blocks * 64u);
  dsp::Event ve = dsp::note_on(48, 100);
  dsp::Event te = dsp::param_change(TractUnit::Source, live ? 1.0f : 0.0f);
  float m[64], l[64], r[64];
  for (unsigned b = 0; b < blocks; ++b) {
    for (unsigned i = 0; i < 64u; ++i) m[i] = gen(b * 64u + i);
    if (b == 0) g.block(m, note ? &ve : nullptr, note ? 1u : 0u, &te, 1, l, r);
    else g.tick(m, l, r);
    out.insert(out.end(), l, l + 64);
    out.insert(out.end(), r, r + 0);
  }
  return out;
}

}  // namespace

TEST_CASE("mouth: silence holds the last shape (a sane one, not a closed tube) and the output goes on") {
  const std::vector<float> voice = make_voice(0.5f, 0.96f, 44100);
  Rig g(true, kModeTract);
  std::vector<float> speech = run_chain(g, 340, [&](unsigned i) { return voice[i]; }, true);
  REQUIRE(g.link.valid);
  // The analysis window (1024 frames, 16 blocks) still holds the voice's tail for a while: the shape holds from the hop it empties.
  std::vector<float> quiet = run_chain(g, 40, [](unsigned) { return 0.0f; }, true);
  const MouthLink spoken = g.link;
  const std::uint32_t shapes_spoken = g.vo.mouth().shapes();
  const std::vector<float> quiet2 = run_chain(g, 300, [](unsigned) { return 0.0f; }, true);
  quiet.insert(quiet.end(), quiet2.begin(), quiet2.end());
  CHECK(bits_equal(g.link, spoken));               // held bit for bit: nothing was written
  CHECK(g.vo.mouth().shapes() == shapes_spoken);
  CHECK(g.vo.mouth().held_hops() > 100u);          // 300 blocks are 150 hops
  CHECK(finite_shape(g.link));
  float widest = 0, narrowest = 2;
  for (std::uint32_t i = 0; i < g.link.n; ++i) {
    widest = std::max(widest, g.link.area[i]);
    narrowest = std::min(narrowest, g.link.area[i]);
  }
  CHECK(widest == doctest::Approx(1.0f));
  CHECK(narrowest >= MouthLive::kFloor * MouthLive::kFloor * 0.999f);  // never a closed tube
  bool finite = true;
  for (float x : quiet) finite = finite && std::isfinite(x);
  CHECK(finite);
  const double a = rms_of(speech, speech.size() - 8192, speech.size()), b = rms_of(quiet, quiet.size() - 8192, quiet.size());
  std::printf("mouth: output rms while the voice speaks %.4f, after %.2f s of silence %.4f (ratio %.2f)\n", a, 340.0 * 64 / 44100, b, b / a);
  CHECK(b > 0.2 * a);
  CHECK(b < 5.0 * a);
}

TEST_CASE("mouth: LIVE from silence (no shape seen yet) plays the tube as it is: the same stream as PADS") {
  auto zero = [](unsigned) { return 0.0f; };
  Rig p(true, kModeTract);
  const std::vector<float> pads = run_chain(p, 200, zero, false);
  Rig l(true, kModeTract);
  const std::vector<float> live = run_chain(l, 200, zero, true);
  CHECK(!l.link.valid);
  CHECK(l.vo.mouth().shapes() == 0u);
  CHECK(l.vo.mouth().held_hops() > 0u);  // it listened and heard nothing
  CHECK(pads.size() == live.size());
  CHECK(std::memcmp(pads.data(), live.data(), pads.size() * sizeof(float)) == 0);
}

TEST_CASE("mouth: DC, a clipped square, a tiny noise and a huge level leave finite areas and a finite, bounded output") {
  struct Case {
    const char* name;
    float (*gen)(unsigned);
  };
  const Case cases[] = {
      {"DC 0.5", [](unsigned) { return 0.5f; }},
      {"DC full scale", [](unsigned) { return 1.0f; }},
      {"clipped square 200 Hz", [](unsigned i) { return (i / 110u) % 2u ? 1.0f : -1.0f; }},
      {"alternating full scale", [](unsigned i) { return (i & 1u) ? 1.0f : -1.0f; }},
      {"1e-20 noise", [](unsigned i) { return 1.0e-20f * static_cast<float>((i * 2654435761u) >> 20); }},
      {"x1000 sine", [](unsigned i) { return 1000.0f * std::sin(0.05f * static_cast<float>(i)); }},
  };
  for (const Case& c : cases) {
    INFO(c.name);
    Rig g(true, kModeTract);
    const std::vector<float> out = run_chain(g, 400, c.gen, true);
    bool finite = true;
    float peak = 0;
    for (float x : out) {
      finite = finite && std::isfinite(x);
      peak = std::max(peak, std::fabs(x));
    }
    CHECK(finite);
    CHECK(peak < 30.0f);
    if (g.link.valid) CHECK(finite_shape(g.link));
    std::printf("mouth: %-24s shapes %4u held %4u peak %.3f\n", c.name, g.vo.mouth().shapes(), g.vo.mouth().held_hops(), double(peak));
  }
}

TEST_CASE("mouth: switching SOURCE mid-note makes no click beyond a note's own step") {
  const std::vector<float> voice = make_voice(0.25f, 0.96f, 44100);
  auto gen = [&](unsigned i) { return voice[i % voice.size()]; };
  auto max_step = [](const std::vector<float>& x, std::size_t from, std::size_t to) {
    float m = 0;
    for (std::size_t i = from + 1; i < to; ++i) m = std::max(m, std::fabs(x[i] - x[i - 1]));
    return m;
  };
  // The reference: the same run with SOURCE never touched after block 0 (PADS), and the largest step it has once settled.
  Rig ref(true, kModeTract);
  const std::vector<float> still = run_chain(ref, 900, gen, false);
  const float natural = max_step(still, 250 * 64, 900 * 64);
  // The switched run: PADS to LIVE at block 400, LIVE to PADS at block 650.
  Rig g(true, kModeTract);
  std::vector<float> out;
  {
    dsp::Event ve = dsp::note_on(48, 100);
    dsp::Event on = dsp::param_change(TractUnit::Source, 1.0f), off = dsp::param_change(TractUnit::Source, 0.0f);
    float m[64], l[64], r[64];
    for (unsigned b = 0; b < 900; ++b) {
      for (unsigned i = 0; i < 64u; ++i) m[i] = gen(b * 64u + i);
      if (b == 0) g.block(m, &ve, 1, nullptr, 0, l, r);
      else if (b == 400) g.block(m, nullptr, 0, &on, 1, l, r);
      else if (b == 650) g.block(m, nullptr, 0, &off, 1, l, r);
      else g.tick(m, l, r);
      out.insert(out.end(), l, l + 64);
    }
  }
  const float to_live = max_step(out, 395 * 64, 500 * 64);
  const float to_pads = max_step(out, 645 * 64, 750 * 64);
  std::printf("mouth: natural step %.5f, around PADS to LIVE %.5f (x%.2f), around LIVE to PADS %.5f (x%.2f)\n", double(natural), double(to_live),
              double(to_live / natural), double(to_pads), double(to_pads / natural));
  bool finite = true;
  for (float x : out) finite = finite && std::isfinite(x);
  CHECK(finite);
  CHECK(natural > 0.0f);
  CHECK(to_live <= 1.25f * natural);
  CHECK(to_pads <= 1.25f * natural);
}

TEST_CASE("mouth: the handshake: it listens only while TRCT asks, in TRACT mode, and never together with TALK") {
  const std::vector<float> voice = make_voice(0.5f, 0.96f, 44100);
  auto gen = [&](unsigned i) { return voice[i % voice.size()]; };
  Rig g(true, kModeTract);
  run_chain(g, 100, gen, false);
  CHECK(g.vo.mouth().shapes() == 0u);  // PADS: nobody asks, the analysis does not run
  CHECK(g.vo.mouth().held_hops() == 0u);
  float m[64], l[64], r[64];
  dsp::Event on = dsp::param_change(TractUnit::Source, 1.0f), off = dsp::param_change(TractUnit::Source, 0.0f);
  auto feed = [&](unsigned from, unsigned blocks, const dsp::Event* te, const dsp::Event* ve) {
    for (unsigned b = 0; b < blocks; ++b) {
      for (unsigned i = 0; i < 64u; ++i) m[i] = gen((from + b) * 64u + i);
      g.block(m, ve, ve ? 1u : 0u, te, te && b == 0 ? 1u : 0u, l, r);
    }
  };
  feed(100, 100, &on, nullptr);
  const std::uint32_t s1 = g.vo.mouth().shapes();
  CHECK(s1 > 20u);  // 100 blocks are 50 hops, minus the window filling
  feed(200, 100, &off, nullptr);
  const std::uint32_t s2 = g.vo.mouth().shapes();
  feed(300, 100, nullptr, nullptr);
  CHECK(g.vo.mouth().shapes() == s2);  // asked no more: no more shapes (one block of grace at most, inside the s2 read)
  CHECK(s2 - s1 <= 2u);
  feed(400, 100, &on, nullptr);
  CHECK(g.vo.mouth().shapes() > s2 + 20u);  // asks again: listens again
  // TALK takes the analysis: the Vocoder's own mode, with SOURCE still on LIVE
  const std::uint32_t s3 = g.vo.mouth().shapes();
  dsp::Event talk = dsp::param_change(Vocoder::Mode, 1.0f);
  feed(500, 400, nullptr, &talk);
  const std::uint32_t s4 = g.vo.mouth().shapes();
  feed(900, 100, nullptr, nullptr);
  CHECK(g.vo.mouth().shapes() == s4);  // settled in TALK: the carrier-mix ramp is closed, TALK's own analysis runs and MouthLive does not
  CHECK(s4 - s3 < 120u);
}

TEST_CASE("mouth: leaving LIVE forgets the shape: LIVE, PADS, LIVE again does not apply the old voice") {
  const std::vector<float> va = make_voice(0.0f, 0.96f, 44100);
  const std::vector<float> vb = make_voice(0.5f, 0.96f, 44100);
  Rig g(true, kModeTract);
  dsp::Event ve = dsp::note_on(48, 100);
  dsp::Event on = dsp::param_change(TractUnit::Source, 1.0f), off = dsp::param_change(TractUnit::Source, 0.0f);
  float m[64], l[64], r[64];
  const auto feed = [&](const std::vector<float>* v, unsigned blocks, const dsp::Event* te, const dsp::Event* note) {
    for (unsigned b = 0; b < blocks; ++b) {
      for (unsigned i = 0; i < 64u; ++i) m[i] = v != nullptr ? (*v)[(b * 64u + i) % v->size()] : 0.0f;
      g.block(m, b == 0 ? note : nullptr, b == 0 && note ? 1u : 0u, b == 0 ? te : nullptr, b == 0 && te ? 1u : 0u, l, r);
    }
  };
  feed(&va, 120, &on, &ve);
  REQUIRE(g.link.valid);
  const MouthLink first = g.link;
  feed(&va, 10, &off, nullptr);
  CHECK(!g.link.valid);  // PADS: the old shape is dropped
  const std::uint32_t shapes = g.vo.mouth().shapes();
  feed(nullptr, 40, &on, nullptr);
  CHECK(!g.link.valid);  // LIVE again on silence: nothing heard, so no shape, and not the first voice's
  CHECK(g.vo.mouth().shapes() == shapes);
  feed(&vb, 120, nullptr, nullptr);
  REQUIRE(g.link.valid);
  CHECK(!bits_equal(first, g.link));  // the other voice's own shape
}
