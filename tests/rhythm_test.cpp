// SPDX-License-Identifier: GPL-3.0-or-later
// GLIDE, GATE and ARP of the vocoder's SYNTH carrier (ids 44, 45, 46): all three at 0 are the exact path of before, the glide reaches its
// target in about the set time, the gate's open and closed periods follow the tempo at each rate without a hard edge, the arp's note order
// for each pattern, its release to silence, no hung note when it goes off with notes held, and the step clock restarting with the transport.
// Synthetic signals only. With O3_VOC03ER_WAV_DIR set, a listening render of each is written there (glide.wav, gate_*.wav, arp_*.wav).
#include <doctest/doctest.h>

#include <o3_voc03er/units/vocoder/carrier.h>
#include <o3_voc03er/units/vocoder/vocoder.h>

#include <o3sdk/dsp/event.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace o3_voc03er;
namespace dsp = o3sdk::dsp;

namespace {

constexpr float kFs = 44100.0f;
constexpr std::size_t kBlock = 64;

using Events = std::vector<std::vector<dsp::Event>>;

struct Run {
  std::uint32_t tempo_mbpm = 120'000;
  std::vector<std::size_t> started;  // blocks where the transport has just started
};

std::vector<float> render(const Events& ev, std::size_t blocks, const Run& run = Run{}) {
  Vocoder v;
  v.prepare(kFs);
  std::vector<float> mod(blocks * kBlock);
  for (std::size_t i = 0; i < mod.size(); ++i) {  // four steady partials: the bands stay open on the carrier's harmonics there
    const float t = static_cast<float>(i) / kFs;
    mod[i] = 0.12f * (std::sin(6.2831853f * 500.0f * t) + std::sin(6.2831853f * 1000.0f * t) + std::sin(6.2831853f * 2000.0f * t) +
                      std::sin(6.2831853f * 3000.0f * t));
  }
  std::vector<float> out(mod.size());
  dsp::Transport tr;
  tr.tempo_mbpm = run.tempo_mbpm;
  for (std::size_t b = 0; b < blocks; ++b) {
    float r[kBlock];
    dsp::ProcessArgs a{};
    a.events = b < ev.size() ? ev[b].data() : nullptr;
    a.event_count = b < ev.size() ? static_cast<std::uint32_t>(ev[b].size()) : 0u;
    a.in_l = mod.data() + b * kBlock;
    a.in_r = mod.data() + b * kBlock;
    a.out_l = out.data() + b * kBlock;
    a.out_r = r;
    a.frames = static_cast<std::uint32_t>(kBlock);
    tr.started = std::find(run.started.begin(), run.started.end(), b) != run.started.end();
    tr.playing = tr.playing || tr.started;
    a.transport = &tr;
    v.process(a);
  }
  return out;
}

Events with_first(std::vector<dsp::Event> first, std::size_t blocks) {
  Events ev(blocks);
  ev[0] = std::move(first);
  return ev;
}

void add(Events& ev, std::size_t block, dsp::Event e) { ev[block].push_back(e); }

// Pitch in Hz of x[from, from + len) by the autocorrelation peak between lo and hi Hz, parabolic refinement.
float pitch(const std::vector<float>& x, std::size_t from, std::size_t len, float lo_hz, float hi_hz) {
  const std::size_t lo = static_cast<std::size_t>(kFs / hi_hz);
  const std::size_t hi = static_cast<std::size_t>(kFs / lo_hz);
  std::vector<double> ac(hi + 2, 0.0);
  for (std::size_t lag = lo - 1; lag <= hi + 1; ++lag) {
    double a = 0.0;
    for (std::size_t i = 0; i + lag < len; ++i) {
      a += static_cast<double>(x[from + i]) * static_cast<double>(x[from + i + lag]);
    }
    ac[lag] = a;
  }
  std::size_t best = lo;
  for (std::size_t lag = lo; lag <= hi; ++lag) {
    if (ac[lag] > ac[best]) {
      best = lag;
    }
  }
  const double d = ac[best - 1] - 2.0 * ac[best] + ac[best + 1];
  const double off = d != 0.0 ? 0.5 * (ac[best - 1] - ac[best + 1]) / d : 0.0;
  return static_cast<float>(static_cast<double>(kFs) / (static_cast<double>(best) + off));
}

// RMS per block of 64 over a centred window of 12 blocks (768 frames, two periods of the lowest note): a block alone is a fraction of a cycle.
std::vector<float> envelope(const std::vector<float>& x) {
  const std::size_t nb = x.size() / kBlock;
  std::vector<double> sq(nb, 0.0);
  for (std::size_t b = 0; b < nb; ++b) {
    for (std::size_t i = 0; i < kBlock; ++i) {
      sq[b] += static_cast<double>(x[b * kBlock + i]) * static_cast<double>(x[b * kBlock + i]);
    }
  }
  std::vector<float> e(nb);
  for (std::size_t b = 0; b < nb; ++b) {
    const std::size_t lo = b >= 6 ? b - 6 : 0;
    const std::size_t hi = std::min(nb, b + 6);
    double s = 0.0;
    for (std::size_t k = lo; k < hi; ++k) {
      s += sq[k];
    }
    e[b] = static_cast<float>(std::sqrt(s / static_cast<double>((hi - lo) * kBlock)));
  }
  return e;
}

float max_step(const std::vector<float>& x, std::size_t from, std::size_t to) {
  float m = 0.0f;
  for (std::size_t i = from + 1; i < to; ++i) {
    m = std::max(m, std::fabs(x[i] - x[i - 1]));
  }
  return m;
}

void write_wav16(const std::string& path, const std::vector<float>& v) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) {
    return;
  }
  const std::uint32_t bytes = static_cast<std::uint32_t>(v.size() * 2);
  const auto u32 = [&](std::uint32_t x) {
    const unsigned char b[4] = {static_cast<unsigned char>(x), static_cast<unsigned char>(x >> 8), static_cast<unsigned char>(x >> 16),
                                static_cast<unsigned char>(x >> 24)};
    std::fwrite(b, 1, 4, f);
  };
  const auto u16 = [&](std::uint16_t x) {
    const unsigned char b[2] = {static_cast<unsigned char>(x), static_cast<unsigned char>(x >> 8)};
    std::fwrite(b, 1, 2, f);
  };
  std::fwrite("RIFF", 1, 4, f);
  u32(36 + bytes);
  std::fwrite("WAVEfmt ", 1, 8, f);
  u32(16);
  u16(1);
  u16(1);
  u32(44100);
  u32(88200);
  u16(2);
  u16(16);
  std::fwrite("data", 1, 4, f);
  u32(bytes);
  for (const float s : v) {
    const float c = std::max(-1.0f, std::min(1.0f, s * 0.8f));
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(c * 32767.0f)));
  }
  std::fclose(f);
}

void wav(const char* name, const std::vector<float>& v) {
  const char* dir = std::getenv("O3_VOC03ER_WAV_DIR");
  if (dir != nullptr) {
    write_wav16(std::string(dir) + "/" + name, v);
  }
}

// The steady chord 45, 48, 52 (110, 130.8, 164.8 Hz) held from block 0, after the given parameter writes.
Events chord(std::vector<dsp::Event> params, std::size_t blocks) {
  params.push_back(dsp::param_change(Vocoder::Release, 30.0f, 0));
  params.push_back(dsp::note_on(45, 100, 0, 0));
  params.push_back(dsp::note_on(48, 100, 0, 0));
  params.push_back(dsp::note_on(52, 100, 0, 0));
  return with_first(params, blocks);
}

}  // namespace

TEST_CASE("rhythm: GLIDE is parameter 44, GATE 45 and ARP 46, all default 0") {
  CHECK(Vocoder::Glide == 44);
  CHECK(Vocoder::Gate == 45);
  CHECK(Vocoder::Arp == 46);
  CHECK(Vocoder::kParamCount == 64);
  Vocoder v;
  CHECK(v.info().params[Vocoder::Glide].def == 0.0f);
  CHECK(v.info().params[Vocoder::Glide].max == 500.0f);
  CHECK(v.info().params[Vocoder::Gate].def == 0.0f);
  CHECK(v.info().params[Vocoder::Gate].max == 4.0f);
  CHECK(v.info().params[Vocoder::Arp].def == 0.0f);
  CHECK(v.info().params[Vocoder::Arp].max == 4.0f);
}

TEST_CASE("rhythm: GLIDE 0, GATE 0 and ARP 0 written are bit-exact with never written") {
  const auto base = render(chord({}, 1), 200);
  const auto zero =
      render(chord({dsp::param_change(Vocoder::Glide, 0.0f, 0), dsp::param_change(Vocoder::Gate, 0.0f, 0), dsp::param_change(Vocoder::Arp, 0.0f, 0)}, 1), 200);
  CHECK(base == zero);
  // switched on and back off: the exact path again (the held notes sound together)
  Events ev = chord({}, 200);
  add(ev, 50, dsp::param_change(Vocoder::Gate, 2.0f, 0));
  add(ev, 60, dsp::param_change(Vocoder::Gate, 0.0f, 0));
  const auto back = render(ev, 200);
  const auto env = envelope(back);
  CHECK(env[199] > 0.01f);
  float lo = 1.0f;
  float hi = 0.0f;
  for (std::size_t b = 150; b < 200; ++b) {
    lo = std::min(lo, env[b]);
    hi = std::max(hi, env[b]);
  }
  CHECK(lo > 0.5f * hi);  // no gating left behind
}

TEST_CASE("rhythm: the carrier's glide reaches the target pitch in about the set time") {
  Carrier c;
  c.prepare(kFs);
  c.set_detune_cents(0.0f);
  c.set_release_ms(5.0f);
  const float ms = 100.0f;
  const float tau = ms * 0.001f * kFs * 0.25f;  // the unit's four time constants over the set time
  const float coef = 1.0f - std::exp(-static_cast<float>(kBlock) / tau);
  std::vector<float> out;
  const auto run = [&](std::size_t blocks) {
    for (std::size_t b = 0; b < blocks; ++b) {
      float buf[kBlock];
      c.step_glide();
      c.render(buf, static_cast<std::uint32_t>(kBlock));
      out.insert(out.end(), buf, buf + kBlock);
    }
  };
  c.set_glide(coef);
  c.note_on(45);
  run(150);
  c.note_off(45);
  c.note_on(57);  // an octave up, 220 Hz: the new voice starts from the old one's pitch
  const std::size_t start = out.size();
  run(300);
  // the frequency between positive-going zero crossings
  std::vector<float> t;
  for (std::size_t i = start + 1; i < out.size(); ++i) {
    if (out[i - 1] < 0.0f && out[i] >= 0.0f) {
      t.push_back(static_cast<float>(i - start) - out[i] / (out[i] - out[i - 1]));
    }
  }
  const auto freq_at = [&](float ms_at) {
    const float s = ms_at * 0.001f * kFs;
    for (std::size_t k = 0; k + 1 < t.size(); ++k) {
      if (t[k] >= s) {
        return kFs / (t[k + 1] - t[k]);
      }
    }
    return 0.0f;
  };
  const float f_start = freq_at(0.0f);
  const float f_mid = freq_at(25.0f);   // one time constant: 220 - 110 / e = 179.5 Hz
  const float f_end = freq_at(120.0f);  // past the set time
  MESSAGE("glide 100 ms: " << f_start << " Hz at 0, " << f_mid << " at 25 ms, " << f_end << " at 120 ms");
  CHECK((f_start > 100.0f && f_start < 150.0f));  // the old note's release shares the first cycles
  CHECK(f_mid == doctest::Approx(179.5f).epsilon(0.1));
  CHECK(f_end == doctest::Approx(220.0f).epsilon(0.03));
  wav("glide.wav", out);
}

TEST_CASE("rhythm: GLIDE through the unit changes the sound and settles") {
  Events ev = chord({}, 1);
  ev.resize(300);
  const auto dry = render(ev, 300);
  ev[0].insert(ev[0].begin(), dsp::param_change(Vocoder::Glide, 200.0f, 0));
  add(ev, 100, dsp::note_off(52, 0));
  add(ev, 100, dsp::note_on(64, 100, 0, 0));
  const auto g = render(ev, 300);
  for (const float s : g) {
    REQUIRE(std::isfinite(s));
  }
  Events ev0 = chord({}, 1);
  ev0.resize(300);
  add(ev0, 100, dsp::note_off(52, 0));
  add(ev0, 100, dsp::note_on(64, 100, 0, 0));
  const auto n = render(ev0, 300);
  double d = 0.0;
  for (std::size_t i = 100 * kBlock; i < 130 * kBlock; ++i) {
    d += std::fabs(static_cast<double>(g[i]) - static_cast<double>(n[i]));
  }
  CHECK(d > 1.0);
  CHECK(dry.size() == g.size());
  wav("glide_unit.wav", g);
}

TEST_CASE("rhythm: GATE open and closed periods follow the tempo at each rate, with soft edges") {
  const float beats[4] = {0.5f, 0.25f, 0.125f, 0.0625f};  // half a step in beats for 1/4, 1/8, 1/16, 1/32
  const char* names[4] = {"gate_4.wav", "gate_8.wav", "gate_16.wav", "gate_32.wav"};
  const std::size_t blocks = 4000;
  const auto plain = render(chord({}, 1), 1200);
  const float plain_step = max_step(plain, 300 * kBlock, 1000 * kBlock);
  for (unsigned r = 1; r <= 4; ++r) {
    for (const std::uint32_t tempo : {120'000u, 90'000u}) {
      Run run;
      run.tempo_mbpm = tempo;
      run.started = {0};
      Events ev = chord({dsp::param_change(Vocoder::Gate, static_cast<float>(r), 0)}, blocks);
      const auto out = render(ev, blocks, run);
      const auto env = envelope(out);
      const float peak = *std::max_element(env.begin() + 100, env.end());
      REQUIRE(peak > 0.01f);
      const float half = kFs * 60000.0f / static_cast<float>(tempo) * beats[r - 1u];
      // block-resolution edges: rising and falling at half the peak
      std::vector<float> rise;
      std::vector<float> fall;
      bool on = env[100] > 0.5f * peak;
      for (std::size_t b = 101; b < blocks; ++b) {
        const bool now = env[b] > 0.5f * peak;
        if (now && !on) {
          rise.push_back(static_cast<float>(b * kBlock));
        } else if (!now && on) {
          fall.push_back(static_cast<float>(b * kBlock));
        }
        on = now;
      }
      REQUIRE(rise.size() >= 4);
      REQUIRE(fall.size() >= 4);
      const float period = (rise.back() - rise.front()) / static_cast<float>(rise.size() - 1u);
      CHECK(period == doctest::Approx(2.0f * half).epsilon(0.02));
      float on_len = 0.0f;
      std::size_t cnt = 0;
      for (const float fl : fall) {
        for (const float ri : rise) {
          if (ri < fl && fl - ri < 1.5f * half) {
            on_len += fl - ri;
            ++cnt;
            break;
          }
        }
      }
      REQUIRE(cnt >= 3);
      CHECK(on_len / static_cast<float>(cnt) == doctest::Approx(half).epsilon(0.15));
      // the 2 ms edges: no jump beyond what the ungated sound makes (a hard cut would jump by the signal's level)
      const float step = max_step(out, 300 * kBlock, 1000 * kBlock);
      CHECK(step < 1.5f * plain_step);
      if (tempo == 120'000u) {
        wav(names[r - 1u], out);
      }
    }
  }
}

TEST_CASE("rhythm: the step clock restarts on the bar when the transport starts") {
  const std::size_t blocks = 400;
  Run run;
  run.started = {0, 150};  // 150 * 64 = 9600 frames: 4087 into a 5512-frame step, in its closed half
  Events ev = chord({dsp::param_change(Vocoder::Gate, 3.0f, 0)}, blocks);
  const auto env = envelope(render(ev, blocks, run));
  const float peak = *std::max_element(env.begin() + 100, env.end());
  CHECK(env[149] < 0.5f * peak);  // closed before the restart
  CHECK(env[153] > 0.5f * peak);  // open again right after it, a step begins on the start
  // and without the restart the same block is still closed (the grid kept its own)
  Run none;
  none.started = {0};
  const auto env2 = envelope(render(ev, blocks, none));
  CHECK(env2[153] < 0.5f * peak);
}

namespace {

// The arp's note at step s of a 1/16 grid at 120 BPM: 0 for 45, 1 for 48, 2 for 52, -1 for none of them.
int arp_note(const std::vector<float>& out, unsigned s) {
  const float step = kFs * 60.0f / 120.0f * 0.25f;
  const std::size_t from = static_cast<std::size_t>(step * static_cast<float>(s)) + 2200;
  const float p = pitch(out, from, 3000, 85.0f, 190.0f);
  const float notes[3] = {110.0f, 130.81f, 164.81f};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(p - notes[i]) < 0.04f * notes[i]) {
      return i;
    }
  }
  return -1;
}

}  // namespace

TEST_CASE("rhythm: ARP sounds the held notes one at a time in the order of each pattern") {
  const std::size_t blocks = 800;  // steps 0 to 7 of 1/16 at 120 BPM (5,512 frames each)
  const char* names[5] = {"", "arp_up.wav", "arp_down.wav", "arp_updown.wav", "arp_random.wav"};
  const std::vector<int> want[4] = {
      {0, 1, 2, 0, 1, 2, 0, 1},  // UP
      {0, 2, 1, 0, 2, 1, 0, 2},  // DOWN: the first key alone starts the clock, so the cycle begins at it
      {0, 1, 2, 1, 0, 1, 2, 1},  // UP-DOWN, the ends once
      {},
  };
  for (unsigned m = 1; m <= 4; ++m) {
    const auto out = render(chord({dsp::param_change(Vocoder::Arp, static_cast<float>(m), 0)}, blocks), blocks);
    std::vector<int> got;
    for (unsigned s = 0; s < 8; ++s) {  // the transport is stopped: the first key restarts the clock and step 0 sounds at once
      got.push_back(arp_note(out, s));
    }
    if (m < 4) {
      std::string txt;
      for (const int g : got) txt += std::to_string(g) + " ";
      MESSAGE("arp " << m << ": " << txt);
      CHECK(got == want[m - 1u]);
    } else {  // RANDOM: always one of the three, more than one of them used
      std::vector<int> seen(3, 0);
      for (const int g : got) {
        REQUIRE(g >= 0);
        ++seen[static_cast<std::size_t>(g)];
      }
      CHECK(std::count_if(seen.begin(), seen.end(), [](int c) { return c > 0; }) >= 2);
    }
    wav(names[m], out);
  }
}

namespace {

// The arp's note at step s of the 1/16 grid at 120 BPM as a MIDI note among 45, 48, 50, 52, 0 for none of them.
int arp_midi(const std::vector<float>& out, unsigned s) {
  const float step = kFs * 60.0f / 120.0f * 0.25f;
  const std::size_t from = static_cast<std::size_t>(step * static_cast<float>(s)) + 2200;
  const float p = pitch(out, from, 3000, 85.0f, 190.0f);
  const int notes[4] = {45, 48, 50, 52};
  const float hz[4] = {110.0f, 130.81f, 146.83f, 164.81f};
  for (int i = 0; i < 4; ++i) {
    if (std::fabs(p - hz[i]) < 0.04f * hz[i]) {
      return notes[i];
    }
  }
  return 0;
}

}  // namespace

TEST_CASE("rhythm: ARP keeps its place in the run when a held note enters or leaves below it") {
  const std::size_t blocks = 700;  // steps 0 to 7; a step is about 86 blocks
  const std::vector<dsp::Event> up = {dsp::param_change(Vocoder::Arp, 1.0f, 0)};
  const std::vector<dsp::Event> down = {dsp::param_change(Vocoder::Arp, 2.0f, 0)};
  auto seq = [&](const Events& ev, unsigned n) {
    const auto out = render(ev, blocks);
    std::vector<int> got;
    for (unsigned s = 0; s < n; ++s) {
      got.push_back(arp_midi(out, s));
    }
    return got;
  };
  {  // UP over 45 48 52, D (50) pressed after step 2 (the 52): the run goes on from the top to the bottom, then 45 48 50 52
    Events ev = chord(up, blocks);
    add(ev, 215, dsp::note_on(50, 100, 0, 0));
    CHECK(seq(ev, 7) == std::vector<int>{45, 48, 52, 45, 48, 50, 52});
  }
  {  // UP, 48 let go while it sounds (after step 1): the note that took its place (52) is next, none skipped
    Events ev = chord(up, blocks);
    add(ev, 130, dsp::note_off(48, 0));
    CHECK(seq(ev, 5) == std::vector<int>{45, 48, 52, 45, 52});
  }
  {  // DOWN over 45 48 52 (45 alone starts it): 45 let go after step 1 (52 sounding), the run goes on 48, 52, 48
    Events ev = chord(down, blocks);
    add(ev, 130, dsp::note_off(45, 0));
    CHECK(seq(ev, 5) == std::vector<int>{45, 52, 48, 52, 48});
  }
  {  // DOWN, D (50) pressed below the sounding 52 after step 1: 50 follows, then 48
    Events ev = chord(down, blocks);
    add(ev, 130, dsp::note_on(50, 100, 0, 0));
    CHECK(seq(ev, 5) == std::vector<int>{45, 52, 50, 48, 45});
  }
}

TEST_CASE("rhythm: ARP goes silent with none held, and releases to silence") {
  const std::size_t blocks = 500;
  Events ev = chord({dsp::param_change(Vocoder::Arp, 1.0f, 0)}, blocks);
  for (const int n : {45, 48, 52}) {
    add(ev, 200, dsp::note_off(static_cast<std::uint8_t>(n), 0));
  }
  const auto env = envelope(render(ev, blocks));
  float held_peak = 0.0f;
  for (std::size_t b = 100; b < 200; ++b) {
    held_peak = std::max(held_peak, env[b]);
  }
  CHECK(held_peak > 0.01f);
  for (std::size_t b = 300; b < blocks; ++b) {
    CHECK(env[b] < 0.001f * held_peak);
  }
  // nothing held from the start: silent, and the first note after it sounds
  Events none = with_first({dsp::param_change(Vocoder::Arp, 2.0f, 0)}, blocks);
  add(none, 300, dsp::note_on(45, 100, 0, 0));
  const auto e2 = envelope(render(none, blocks));
  for (std::size_t b = 100; b < 290; ++b) {
    CHECK(e2[b] < 1e-6f);
  }
  CHECK(*std::max_element(e2.begin() + 300, e2.end()) > 0.01f);
}

TEST_CASE("rhythm: ARP off with notes held sounds them all again and leaves no hung note") {
  const std::size_t blocks = 700;
  Events ev = chord({dsp::param_change(Vocoder::Arp, 3.0f, 0)}, blocks);
  add(ev, 200, dsp::param_change(Vocoder::Arp, 0.0f, 0));
  for (const int n : {45, 48, 52}) {
    add(ev, 400, dsp::note_off(static_cast<std::uint8_t>(n), 0));
  }
  const auto out = render(ev, blocks);
  const auto env = envelope(out);
  // after the switch the chord sounds together: three partials of the pitches are present, the level steady
  const float p = pitch(out, 300 * kBlock, 4096, 85.0f, 190.0f);
  CHECK(p > 100.0f);
  float lo = 1.0f;
  float hi = 0.0f;
  for (std::size_t b = 250; b < 390; ++b) {
    lo = std::min(lo, env[b]);
    hi = std::max(hi, env[b]);
  }
  CHECK(lo > 0.5f * hi);
  for (std::size_t b = 600; b < blocks; ++b) {
    CHECK(env[b] < 0.001f * hi);  // released: nothing hangs
  }
  // off while the arp has a note on and the keys are let go in the same block
  Events ev2 = chord({dsp::param_change(Vocoder::Arp, 1.0f, 0)}, blocks);
  add(ev2, 100, dsp::param_change(Vocoder::Arp, 0.0f, 0));
  for (const int n : {45, 48, 52}) {
    add(ev2, 100, dsp::note_off(static_cast<std::uint8_t>(n), 0));
  }
  const auto e2 = envelope(render(ev2, blocks));
  for (std::size_t b = 300; b < blocks; ++b) {
    CHECK(e2[b] < 1e-3f);
  }
}

namespace {
// The first frame at which the output exceeds a threshold well above the bands' noise floor (or the size when it never does).
std::size_t onset(const std::vector<float>& x, std::size_t from) {
  for (std::size_t i = from; i < x.size(); ++i) {
    if (std::fabs(x[i]) > 2.0e-3f) {
      return i;
    }
  }
  return x.size();
}
}  // namespace

TEST_CASE("rhythm: ARP's first note sounds at once with the transport stopped and stays on the grid when it runs") {
  const std::size_t blocks = 700;
  const std::size_t press = 100;  // block where the key goes down: frame 6,400, between the grid edges 5,512 and 11,025
  const auto played = [&](bool running) {
    Events ev(blocks);
    add(ev, 0, dsp::param_change(Vocoder::Arp, 1.0f, 0));
    add(ev, 0, dsp::param_change(Vocoder::Release, 30.0f, 0));
    add(ev, press, dsp::note_on(45, 100, 0, 0));
    add(ev, 400, dsp::note_off(45, 0));
    Run run;
    if (running) {
      run.started = {0};
    }
    return render(ev, blocks, run);
  };
  const std::size_t at = press * kBlock;
  const auto stopped = played(false);
  const std::size_t s_on = onset(stopped, 0);
  CHECK(s_on >= at);
  CHECK(s_on < at + 2 * kBlock + 600);  // at once: the attack of the voice, not a wait for the next step
  const auto running = played(true);
  const std::size_t r_on = onset(running, 0);
  CHECK(r_on >= 11025);  // on the grid: the next edge after the key
  CHECK(r_on < 11025 + 2 * kBlock + 600);
  // no hung note: both are silent a while after the key goes up
  for (const auto* o : {&stopped, &running}) {
    const auto env = envelope(*o);
    for (std::size_t b = 600; b < blocks; ++b) {
      CHECK(env[b] < 1.0e-3f);
    }
  }
}
