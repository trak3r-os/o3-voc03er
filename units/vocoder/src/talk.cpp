// SPDX-License-Identifier: GPL-3.0-or-later
// Talk: LPC talkbox, streaming, at fs/4. DSP steps after mda Talkbox, Copyright (c) 1999-2000 Paul Kellett (maxim digital audio), used under
// the MIT option; see THIRD_PARTY.md. The code here is original.
#include <o3_voc03er/units/vocoder/talk.h>

#include <o3_voc03er/units/vocoder/agc.h>
#include <o3_voc03er/units/vocoder/bands.h>
#include <o3_voc03er/units/vocoder/nofma.h>

#include <cmath>

namespace o3_voc03er {

static_assert(Talk::kWin + Talk::kHop <= Talk::kRing, "the window must stay stable through a hop");
static_assert((Talk::kRing & (Talk::kRing - 1u)) == 0u, "the ring wraps with a mask");
static_assert(Talk::kHop * 4u == 128u, "a hop is 128 frames (2.9 ms), a power of 2: the breath's counter wraps with a mask");
static_assert(Talk::stages_for(Talk::kMaxOrder) == Talk::kMaxStages, "the arrays hold the highest Quality's lattice");
static_assert(Talk::stages_for(Talk::kMinOrder) >= 1u, "at least one stage");
static_assert(alignof(Talk) >= 8u, "the buffers are alignas(8)");
static_assert(Talk::kWin % 4u == 0u && Talk::kHop % 4u == 0u, "the analysis goes four samples at a time, and its share every four low-rate samples");

namespace {
constexpr float kPreEmph = 0.9f;
constexpr float kCarEmph = 0.1f;        // first difference of the carrier: c - 0.1 c[-1] (was 0.2; S2's low-pass takes the top, a starting value)
constexpr float kCarLpHz = 3500.0f;     // S2: one-pole low-pass on the low-rate carrier, the tube's top (a starting value)
constexpr float kR0Boost = 1.0001f;
constexpr float kLagHz = 60.0f;
// Mean power of the pre-emphasised, decimated modulator below which the analysis calls it silence. The old gate was 1e-9 (-90 dB) at full
// rate; white noise keeps a quarter of its power through the two decimators, so the same noise floor reads a quarter of it here.
constexpr float kGate = 2.5e-10f;
constexpr float kTiny = 1.0e-15f;
constexpr float kErrFloor = 1.0e-7f;  // of r0: about float's resolution, below it Levinson's error is rounding
constexpr float kPi2 = 6.2831853f;
constexpr float kWinRise = 0.8f;        // the window rises over this part (sin^2) and falls over the rest (cos^2)
constexpr float kGainK = 2.307f;          // the lattice's level against the breath: fixes their balance (docs/ENGINE-REALISM.md)
constexpr float kBreath = 0.5f;         // the breath's level against the modulator's high band (was 0.3; a starting value, S1)
// S1/S1b, the voiced/unvoiced duck. ratio = (mean h^2 of the 5 kHz high-pass) / (mean x^2 of the un-emphasised modulator), both per full-rate
// frame over the same 128-frame hop: the same scale, so a vowel reads a small ratio and breath or sss a large one (S1 divided by the
// pre-emphasised window's power, which shrinks the lows and read high on vowels). Every constant below is a starting value, set between the
// distributions on 8 s of real speech (docs/ENGINE-REALISM.md), for the maintainer's ear to move.
constexpr float kDuckT0 = 0.28f;        // at or below this ratio the carrier is not ducked
constexpr float kDuckT1 = 0.45f;        // at or above it the carrier is ducked to the floor
constexpr float kDuckFloor = 0.05f;     // -26 dB: the gain a hop of fricative-like content keeps (a starting value: ear and unit read)
constexpr float kDuckSlope = 1.0f / (kDuckT1 - kDuckT0);
constexpr float kBreathVoiced = 0.25f;  // the breath's share while voiced; it rises to 1 as the duck closes
constexpr float kUnvGate = 10.0f * kGate;  // lf + hf (both as mean powers) at or below this: a gap, not a consonant: duck to the floor, breath 0
constexpr float kRatioEps = kGate;      // keeps the divide finite when lf is 0
constexpr float kHopFrames = 128.0f;    // the hf accumulator's hop, as Talk::kHop * 4
constexpr float kBreathHz = 5000.0f;
constexpr float kBreathTopHz = 4000.0f;  // two cascaded poles on the breath: the player's air at the mic, not a full-band hiss (a starting value)
constexpr float kOpenPeak = 1.0e-3f;    // the carrier's peak over a call above which the breath is open
constexpr float kOpenMs = 5.0f;

inline float flush(float x) noexcept { return (x > kTiny || x < -kTiny) ? x : 0.0f; }
}  // namespace

void CarDrive::prepare(float sample_rate) noexcept {
  a_ = std::exp(-kPi2 * kDriveHz / sample_rate);
  reset();
}

void CarDrive::process(float* x, std::uint32_t n) noexcept {
  const float a = a_;
  float x1 = x1_, y1 = y1_;
  for (std::uint32_t i = 0; i < n; ++i) {
    const float in = x[i];
    y1 = mulb(a, y1 + (in - x1));  // y[n] = a (y[n-1] + x[n] - x[n-1])
    x1 = in;
    float t = mulb(y1, 1.0f / kClipAt);
    t = t > 1.0f ? 1.0f : (t < -1.0f ? -1.0f : t);
    const float t3 = mulb(mulb(t, t), t);
    x[i] = mulb(kDriveGain * kClipAt, t - mulb(t3, 1.0f / 3.0f));  // t - t^3/3, flat at +-1 where it is +-2/3
  }
  x1_ = flush(x1);
  y1_ = flush(y1);
}

void Talk::prepare(float sample_rate) noexcept {
  const std::uint32_t rise = static_cast<std::uint32_t>(static_cast<float>(kWin) * kWinRise);
  const std::uint32_t fall = kWin - rise;
  float s2 = 0.0f;
  for (std::uint32_t i = 0; i < kWin; ++i) {
    float h;
    if (i < rise) {
      h = std::sin(0.25f * kPi2 * (static_cast<float>(i) + 0.5f) / static_cast<float>(rise));
    } else {
      h = std::cos(0.25f * kPi2 * (static_cast<float>(i - rise) + 0.5f) / static_cast<float>(fall));
    }
    wtab_[i] = h * h;
    s2 += wtab_[i] * wtab_[i];
  }
  sumw2_ = s2;
  const float fl = sample_rate * 0.25f;
  for (std::uint32_t l = 0; l <= kMaxStages; ++l) {
    const float x = kPi2 * kLagHz * static_cast<float>(l) / fl;
    lagw_[l] = std::exp(-0.5f * x * x);
  }
  // 2nd-order Butterworth high-pass (RBJ, Q = 1/sqrt 2), full rate.
  const float w0 = kPi2 * kBreathHz / sample_rate;
  const float cw = std::cos(w0);
  const float al = std::sin(w0) * 0.70710678f;
  const float a0 = 1.0f + al;
  hp_b0_ = 0.5f * (1.0f + cw) / a0;
  hp_b1_ = -(1.0f + cw) / a0;
  hp_b2_ = hp_b0_;
  hp_a1_ = -2.0f * cw / a0;
  hp_a2_ = (1.0f - al) / a0;
  open_step_ = 1.0f / (kOpenMs * 0.001f * sample_rate);
  br_lp_a_ = 1.0f - std::exp(-kPi2 * kBreathTopHz / sample_rate);  // both poles of the cascade take this one coefficient
  car_lp_a_ = 1.0f - std::exp(-kPi2 * kCarLpHz / fl);
  lvl_att_ = 1.0f - std::exp(-1000.0f / (kAgcAttackMs * fl));
  lvl_rel_ = 1.0f - std::exp(-1000.0f / (kAgcReleaseMs * fl));
  level_ = kAgcTarget;  // starts at the target: unity gain, no blast at the first loud sample
  drive_.prepare(sample_rate);
  reset();
}

void Talk::reset() noexcept {
  for (auto& v : ring_) v = 0.0f;
  for (auto& v : win_) v = 0.0f;
  for (auto& v : r_) v = 0.0f;
  for (std::uint32_t m = 0; m < kMaxStages + 2; ++m) {
    k_[m] = 0.0f;
    kt_[m] = 0.0f;
    dk_[m] = 0.0f;
    b_[m] = 0.0f;
  }
  for (auto& h : dec_mod_) h = Halfband{};
  for (auto& h : dec_car_) h = Halfband{};
  for (auto& h : interp_) h = Halfband{};
  drive_.reset();
  odd1_ = odd2_ = false;
  wpos_ = filled_ = hop_pos_ = end_ = 0;
  pa_ = run_ = tgt_order_ = stages_for(order_);
  analysing_ = false;
  stage_ = cpos_ = lag_ = total_ = done_ = ramp_left_ = 0;
  for (auto& v : acc_) v = 0.0f;
  emph_prev_ = car_prev_ = gain_ = gain_t_ = dgain_ = 0.0f;
  hp_s1_ = hp_s2_ = br_lp_y_ = br_lp2_y_ = 0.0f;
  hf_acc_ = hf_done_ = ms_acc_ = ms_done_ = unv_ = car_lp_y_ = 0.0f;
  duck_ = 1.0f;
  open_ = breath_ = breath_step_ = 0.0f;
  breath_pos_ = 0;
  agc_ = agc_gain(level_);  // level_ stays: its estimate of the input beats unity gain after a pause, as in Bands::restart
  macs_last_ = 0;
}

void Talk::adopt_level(float level) noexcept {
  level_ = level;
  agc_ = agc_gain(level);
}

void Talk::set_order(std::uint32_t order) noexcept {
  order_ = order < kMinOrder ? kMinOrder : (order > kMaxOrder ? kMaxOrder : order);
}

// Runs `units` of the sliced work, a unit being four samples: first the window and r0, then the lags, four in one pass. Every sample loaded serves
// four lags and the four sums do not wait on each other; one lag at a time cost two loads a multiply-add and stalled on its own sum. The
// window's zeros past kWin let every lag run the same length, and the lags past pa_ of the last pass are never read.
void Talk::slice(std::uint32_t units) noexcept {
  macs_last_ += units * 16u;
  while (units != 0u && stage_ < 2u) {
    std::uint32_t c = kQuads - cpos_;
    c = c < units ? c : units;
    units -= c;
    if (stage_ == 0u) {
      float r0 = acc_[0];
      const std::uint32_t last = (cpos_ + c) * 4u;
      for (std::uint32_t i = cpos_ * 4u; i < last; ++i) {
        const float x = ring_[(end_ - kWin + i) & (kRing - 1u)] * wtab_[i];
        win_[i] = x;
        r0 += mulb(x, x);
      }
      acc_[0] = r0;
    } else {
      const float* a = win_ + cpos_ * 4u;
      const float* b = a + lag_;
      float s0 = acc_[0], s1 = acc_[1], s2 = acc_[2], s3 = acc_[3];
      float b0 = b[0], b1 = b[1], b2 = b[2];
      for (std::uint32_t q = 0; q < c; ++q) {
        const float a0 = a[0], a1 = a[1], a2 = a[2], a3 = a[3];
        const float b3 = b[3], b4 = b[4], b5 = b[5], b6 = b[6];
        s0 += mulb(a0, b0);
        s1 += mulb(a0, b1);
        s2 += mulb(a0, b2);
        s3 += mulb(a0, b3);
        s0 += mulb(a1, b1);
        s1 += mulb(a1, b2);
        s2 += mulb(a1, b3);
        s3 += mulb(a1, b4);
        s0 += mulb(a2, b2);
        s1 += mulb(a2, b3);
        s2 += mulb(a2, b4);
        s3 += mulb(a2, b5);
        s0 += mulb(a3, b3);
        s1 += mulb(a3, b4);
        s2 += mulb(a3, b5);
        s3 += mulb(a3, b6);
        b0 = b4;
        b1 = b5;
        b2 = b6;
        a += 4;
        b += 4;
      }
      acc_[0] = s0;
      acc_[1] = s1;
      acc_[2] = s2;
      acc_[3] = s3;
    }
    cpos_ += c;
    if (cpos_ == kQuads) {
      cpos_ = 0;
      if (stage_ == 0u) {
        r_[0] = acc_[0];
        stage_ = 1;
        lag_ = 1;
      } else {
        for (std::uint32_t i = 0; i < 4u; ++i) r_[lag_ + i] = acc_[i];
        lag_ += 4u;
        if (lag_ > pa_) stage_ = 2;
      }
      for (auto& v : acc_) v = 0.0f;
    }
  }
}

// Levinson-Durbin on r_[0..pa_]; sets the targets and the gain target, and starts the ramp.
void Talk::publish() noexcept {
  const std::uint32_t p = pa_;
  const std::uint32_t ord = stages_for(order_);
  const float power = r_[0] / sumw2_;
  const bool silent = !(power > kGate);
  float err = 0.0f;
  float kk[kMaxStages + 2] = {};
  if (!silent && !freeze_) {
    r_[0] *= kR0Boost;
    for (std::uint32_t l = 1; l <= p; ++l) r_[l] *= lagw_[l];
    float a[kMaxStages + 2];
    float t[kMaxStages + 2];
    for (auto& v : a) v = 0.0f;
    err = r_[0];
    std::uint32_t macs = 0;
    const float err_floor = r_[0] * kErrFloor;
    for (std::uint32_t m = 1; m <= p; ++m) {
      if (!(err > err_floor)) {
        // A safety net, reached by no signal tried: the boost on r0 and the lag window keep err far above it. If err ever got to 0,
        // -s/err would be a NaN no clamp catches and the lattice would stay poisoned. The stages left stay at 0.
        break;
      }
      float s = r_[m];
      for (std::uint32_t j = 1; j < m; ++j) s += a[j] * r_[m - j];
      float k = -s / err;
      k = k > kMaxK ? kMaxK : (k < -kMaxK ? -kMaxK : k);
      for (std::uint32_t j = 1; j < m; ++j) t[j] = a[j] + k * a[m - j];
      for (std::uint32_t j = 1; j < m; ++j) a[j] = t[j];
      a[m] = k;
      kk[m] = k;
      err *= 1.0f - k * k;
      macs += 2u * m;
    }
    macs_last_ += macs;
  } else if (!silent) {
    err = r_[0] * kR0Boost;  // frozen: k holds, the gain follows the level through the frozen filter's own error
    const std::uint32_t q = p < ord ? p : ord;
    for (std::uint32_t m = 1; m <= q; ++m) err *= 1.0f - kt_[m] * kt_[m];
  }
  agc_ = agc_gain(level_);
  // S1: the carrier ducks when the modulator is breath-like (much high band against the window's power) and above the level gate.
  float gd = kDuckFloor;
  unv_ = 0.0f;
  const float hf = hf_done_ * (1.0f / kHopFrames);
  if (!silent && power + hf > kUnvGate) {
    const float ratio = hf / (ms_done_ * (1.0f / kHopFrames) + kRatioEps);  // the hop's one divide
    gd = (kDuckT1 - ratio) * kDuckSlope;
    gd = gd > 1.0f ? 1.0f : (gd < kDuckFloor ? kDuckFloor : gd);
    unv_ = kBreathVoiced + (1.0f - kBreathVoiced) * (1.0f - gd);
  }
  duck_ = gd;
  if (!silent) {
    gain_t_ = gd * kGainK * agc_ * std::sqrt((err > 0.0f ? err : 0.0f) / sumw2_);
  } else {
    gain_t_ = 0.0f;
  }
  const std::uint32_t use = p < ord ? p : ord;
  const std::uint32_t prev_run = run_;
  if (!silent && !freeze_) {
    for (std::uint32_t m = 1; m <= kMaxStages; ++m) kt_[m] = m <= use ? kk[m] : 0.0f;
  } else {
    for (std::uint32_t m = use + 1; m <= kMaxStages; ++m) kt_[m] = 0.0f;
  }
  tgt_order_ = use;
  run_ = prev_run > use ? prev_run : use;
  if (gain_ == 0.0f) {  // from silence nothing sounds yet, so the filter lands at once: a phrase starts on its formants, with no glide from flat
    for (std::uint32_t m = 1; m <= kMaxStages; ++m) k_[m] = kt_[m];
  }
  ramp_left_ = kHop;
  const float inv = 1.0f / static_cast<float>(kHop);
  for (std::uint32_t m = 1; m <= kMaxStages; ++m) dk_[m] = (kt_[m] - k_[m]) * inv;
  dgain_ = (gain_t_ - gain_) * inv;
}

// One low-rate sample: the modulator into the ring, this sample's share of the analysis, the ramp one step, the carrier through the lattice.
void Talk::tick(float m, float c, float& y) noexcept {
  if (hop_pos_ == 0u) {
    // the window is the kWin samples before this one; a ring of 512 keeps it stable through the hop
    analysing_ = filled_ >= kWin;
    if (analysing_) {
      end_ = wpos_ & (kRing - 1u);
      pa_ = stages_for(order_);
      stage_ = cpos_ = lag_ = 0;
      for (auto& v : acc_) v = 0.0f;
      total_ = kQuads * (1u + (pa_ + 3u) / 4u);  // the window, and a pass for every four lags
      done_ = 0;
    }
  }
  ring_[wpos_ & (kRing - 1u)] = m - kPreEmph * emph_prev_;
  emph_prev_ = m;
  ++wpos_;
  const float am = m < 0.0f ? -m : m;
  level_ += (am - level_) * (am > level_ ? lvl_att_ : lvl_rel_);
  if (filled_ < kRing) ++filled_;
  if (analysing_ && (hop_pos_ & 3u) == 3u) {
    // an even share of the work every four samples: the cumulative target is total x (pos + 1) / kHop
    const std::uint32_t upto = total_ * (hop_pos_ + 1u) / kHop;
    slice(upto - done_);
    done_ = upto;
    if (hop_pos_ == kHop - 1u) publish();
  }
  hop_pos_ = hop_pos_ == kHop - 1u ? 0u : hop_pos_ + 1u;

  const std::uint32_t run = run_;
  const bool ramping = ramp_left_ != 0u;
  if (ramping) gain_ += dgain_;
  float f = (c - kCarEmph * car_prev_) * gain_;
  car_prev_ = c;
  if (ramping) {  // the ramp's step and the lattice in one pass: a hop is ramping in all but one of its samples
    for (std::uint32_t i = run; i >= 1u; --i) {
      const float k = k_[i] + dk_[i];
      const float bp = b_[i - 1u];
      k_[i] = k;
      f -= mulb(k, bp);
      b_[i] = bp + mulb(k, f);
    }
    if (--ramp_left_ == 0u) {  // the last step lands on the target
      for (std::uint32_t i = 1; i <= run; ++i) k_[i] = kt_[i];
      gain_ = gain_t_;
    }
  } else {
    for (std::uint32_t i = run; i >= 1u; --i) {
      const float k = k_[i];
      const float bp = b_[i - 1u];
      f -= mulb(k, bp);
      b_[i] = bp + mulb(k, f);
    }
  }
  b_[0] = f;
  y = f;
  macs_last_ += 6u * run + 12u;
  // The ramp has landed and the targets above the new order are 0: stop running those stages (a publish restarts the ramp the
  // very next hop, so this is checked here and not once a block).
  if (ramp_left_ == 0u && run_ != tgt_order_) {
    run_ = tgt_order_;
    for (std::uint32_t i = run_ + 1; i <= kMaxStages; ++i) {
      b_[i] = 0.0f;
      k_[i] = 0.0f;
    }
  }
}

void Talk::process(const float* mod, const float* car, float* out, std::uint32_t n) noexcept {
  macs_last_ = 0;
  if (n > kMaxFrames) n = kMaxFrames;
  float m0[kMaxFrames], c0[kMaxFrames];
  float m1[kMaxFrames / 2], c1[kMaxFrames / 2], m2[kMaxFrames / 4], c2[kMaxFrames / 4];
  float y2[kMaxFrames / 4], y1[kMaxFrames / 2];
  float peak = 0.0f;
  for (std::uint32_t i = 0; i < n; ++i) {
    m0[i] = Bands::clean(mod[i]);
    c0[i] = Bands::clean(car[i]);
    const float a = c0[i] < 0.0f ? -c0[i] : c0[i];
    peak = a > peak ? a : peak;
  }
  // The driver, at the full rate and before the halfbands: the carrier's peak above was read before it, so the breath's gate is unchanged.
  drive_.process(c0, n);
  const bool odd1 = odd1_, odd2 = odd2_;
  const std::uint32_t n1 = decimate(dec_mod_[0], m0, n, odd1, m1);
  decimate(dec_car_[0], c0, n, odd1, c1);
  const std::uint32_t n2 = decimate(dec_mod_[1], m1, n1, odd2, m2);
  decimate(dec_car_[1], c1, n1, odd2, c2);
  odd1_ = ((static_cast<unsigned>(odd1) + n) & 1u) != 0u;
  odd2_ = ((static_cast<unsigned>(odd2) + n1) & 1u) != 0u;

  {  // S2: the carrier's top, one pole at the low rate
    float y = car_lp_y_;
    const float g = car_lp_a_;
    for (std::uint32_t i = 0; i < n2; ++i) {
      y += mulb(g, c2[i] - y);
      c2[i] = y;
    }
    car_lp_y_ = y;
  }
  for (std::uint32_t i = 0; i < n2; ++i) tick(m2[i], c2[i], y2[i]);

  for (std::uint32_t i = 0; i < n1; ++i) y1[i] = 0.0f;
  interpolate(interp_[1], y2, n1, odd2, y1);
  for (std::uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  interpolate(interp_[0], y1, n, odd1, out);

  // Breath: the modulator above 5 kHz, as loud as the carrier is open.
  const float target = peak > kOpenPeak ? 1.0f : 0.0f;
  // Its level moves once a hop, at frames counted from reset: a publish always lands before the frame that reads it (4 frames, a call is at
  // most 64 of the hop's 128), so the output does not depend on how the frames are split into calls.
  float breath = breath_, breath_step = breath_step_;
  std::uint32_t breath_pos = breath_pos_;
  float s1 = hp_s1_, s2 = hp_s2_, open = open_, hf = hf_acc_, ms = ms_acc_, bl = br_lp_y_, bl2 = br_lp2_y_;
  const float bla = br_lp_a_;
  const float b0 = hp_b0_, b1 = hp_b1_, b2 = hp_b2_, a1 = hp_a1_, a2 = hp_a2_;  // in locals: a store to out must not reload them
  const float ostep = open_step_;
  for (std::uint32_t i = 0; i < n;) {
    constexpr std::uint32_t kGrid = kHop * 4u;
    if (breath_pos == 0u) {
      hf_done_ = hf;  // the hop that just ended; the next publish, at this hop's end, reads it (and ms_done_, over the same frames)
      ms_done_ = ms;
      hf = ms = 0.0f;
      breath_step = (kBreath * unv_ * (0.5f + 0.5f * thru_) * agc_ - breath) * (1.0f / static_cast<float>(kGrid));
    }
    const std::uint32_t left = kGrid - breath_pos;
    const std::uint32_t run = n - i < left ? n - i : left;
    breath_pos = (breath_pos + run) & (kGrid - 1u);
    const std::uint32_t last = i + run;
    if (open == target) {  // nearly always
      for (; i < last; ++i) {
        const float x = m0[i];
        const float h = mulb(b0, x) + s1;
        s1 = mulb(b1, x) - mulb(a1, h) + s2;
        s2 = mulb(b2, x) - mulb(a2, h);
        hf += mulb(h, h);
        ms += mulb(x, x);
        breath += breath_step;
        bl += mulb(bla, h - bl);
        bl2 += mulb(bla, bl - bl2);
        out[i] += mulb(breath * open, bl2);
      }
    } else {
      for (; i < last; ++i) {
        const float x = m0[i];
        const float h = mulb(b0, x) + s1;
        s1 = mulb(b1, x) - mulb(a1, h) + s2;
        s2 = mulb(b2, x) - mulb(a2, h);
        open = open < target ? (open + ostep < target ? open + ostep : target) : (open - ostep > target ? open - ostep : target);
        hf += mulb(h, h);
        ms += mulb(x, x);
        breath += breath_step;
        bl += mulb(bla, h - bl);
        bl2 += mulb(bla, bl - bl2);
        out[i] += mulb(breath * open, bl2);
      }
    }
  }
  breath_ = breath;
  breath_step_ = breath_step;
  breath_pos_ = breath_pos;
  hp_s1_ = flush(s1);
  hp_s2_ = flush(s2);
  br_lp_y_ = flush(bl);
  br_lp2_y_ = flush(bl2);
  open_ = open;
  hf_acc_ = hf;
  ms_acc_ = ms;
  macs_last_ += n * 15u;

  // Denormals: states decay to zero under silence; flush them once a block.
  for (std::uint32_t i = 0; i <= run_; ++i) b_[i] = flush(b_[i]);
  for (auto& h : dec_mod_) settle(h);
  for (auto& h : dec_car_) settle(h);
  for (auto& h : interp_) settle(h);
  emph_prev_ = flush(emph_prev_);
  car_prev_ = flush(car_prev_);
  car_lp_y_ = flush(car_lp_y_);
  gain_ = flush(gain_);
  level_ = flush(level_);
}

}  // namespace o3_voc03er
