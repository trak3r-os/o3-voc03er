// SPDX-License-Identifier: GPL-3.0-or-later
// Talk: LPC talkbox, streaming, at fs/4. DSP steps after mda Talkbox, Copyright (c) 1999-2000 Paul Kellett (maxim digital audio), used under
// the MIT option; see THIRD_PARTY.md. The code here is original.
#include <o3_voc03er/units/vocoder/talk.h>

#include <o3_voc03er/units/vocoder/bands.h>

#include <cmath>

namespace o3_voc03er {

static_assert(Talk::kWin + Talk::kHop <= Talk::kRing, "the window must stay stable through a hop");
static_assert((Talk::kRing & (Talk::kRing - 1u)) == 0u, "the ring wraps with a mask");
static_assert(Talk::kHop * 4u == 128u, "a hop is 128 frames (2.9 ms)");
static_assert(Talk::stages_for(Talk::kMaxOrder) == Talk::kMaxStages, "the arrays hold the highest Quality's lattice");
static_assert(Talk::stages_for(Talk::kMinOrder) >= 1u, "at least one stage");
static_assert(alignof(Talk) >= 8u, "the buffers are alignas(8)");

namespace {
constexpr float kPreEmph = 0.9f;
constexpr float kCarEmph = 0.4f;        // first difference of the carrier: c - 0.4 c[-1]
constexpr float kR0Boost = 1.0001f;
constexpr float kLagHz = 60.0f;
// Mean power of the pre-emphasised, decimated modulator below which the analysis calls it silence. The old gate was 1e-9 (-90 dB) at full
// rate; white noise keeps a quarter of its power through the two decimators, so the same noise floor reads a quarter of it here.
constexpr float kGate = 2.5e-10f;
constexpr float kTiny = 1.0e-15f;
constexpr float kErrFloor = 1.0e-7f;  // of r0: about float's resolution, below it Levinson's error is rounding
constexpr float kPi2 = 6.2831853f;
constexpr float kWinRise = 0.8f;        // the window rises over this part (sin^2) and falls over the rest (cos^2)
constexpr float kGainK = 2.307f;          // level constant on the gain: sets TALK's overall level (calibrated on the bench, see the report)
constexpr float kBreath = 0.36f;         // the breath's level against the modulator's high band
constexpr float kBreathHz = 5000.0f;
constexpr float kOpenPeak = 1.0e-3f;    // the carrier's peak over a call above which the breath is open
constexpr float kOpenMs = 5.0f;

inline float flush(float x) noexcept { return (x > kTiny || x < -kTiny) ? x : 0.0f; }
}  // namespace

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
  odd1_ = odd2_ = false;
  wpos_ = filled_ = hop_pos_ = end_ = 0;
  pa_ = run_ = tgt_order_ = stages_for(order_);
  analysing_ = false;
  stage_ = cpos_ = lag_ = total_ = done_ = ramp_left_ = 0;
  acc_ = emph_prev_ = car_prev_ = gain_ = gain_t_ = dgain_ = 0.0f;
  hp_s1_ = hp_s2_ = 0.0f;
  open_ = 0.0f;
  macs_last_ = 0;
}

void Talk::set_order(std::uint32_t order) noexcept {
  order_ = order < kMinOrder ? kMinOrder : (order > kMaxOrder ? kMaxOrder : order);
}

// Runs `units` of the sliced work: windowing, then the lag sums.
void Talk::slice(std::uint32_t units) noexcept {
  macs_last_ += units;
  while (units != 0u && stage_ < 2u) {
    if (stage_ == 0u) {
      std::uint32_t c = kWin - cpos_;
      c = c < units ? c : units;
      for (std::uint32_t i = cpos_; i < cpos_ + c; ++i) {
        win_[i] = ring_[(end_ - kWin + i) & (kRing - 1u)] * wtab_[i];
      }
      cpos_ += c;
      units -= c;
      if (cpos_ == kWin) {
        stage_ = 1;
        cpos_ = 0;
        lag_ = 0;
        acc_ = 0.0f;
      }
    } else {
      const std::uint32_t len = kWin - lag_;
      std::uint32_t c = len - cpos_;
      c = c < units ? c : units;
      const float* a = win_ + cpos_;
      const float* b = win_ + cpos_ + lag_;
      float s = 0.0f;
      for (std::uint32_t i = 0; i < c; ++i) s += a[i] * b[i];
      acc_ += s;
      cpos_ += c;
      units -= c;
      if (cpos_ == len) {
        r_[lag_] = acc_;
        acc_ = 0.0f;
        cpos_ = 0;
        if (++lag_ > pa_) stage_ = 2;
      }
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
  if (!silent) {
    gain_t_ = kGainK * std::sqrt((err > 0.0f ? err : 0.0f) / sumw2_);
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
      acc_ = 0.0f;
      std::uint32_t t = kWin;
      for (std::uint32_t l = 0; l <= pa_; ++l) t += kWin - l;
      total_ = t;
      done_ = 0;
    }
  }
  ring_[wpos_ & (kRing - 1u)] = m - kPreEmph * emph_prev_;
  emph_prev_ = m;
  ++wpos_;
  if (filled_ < kRing) ++filled_;
  if (analysing_) {
    // an even share of the work a sample: the cumulative target is total x (pos + 1) / kHop
    const std::uint32_t upto = total_ * (hop_pos_ + 1u) / kHop;
    slice(upto - done_);
    done_ = upto;
    if (hop_pos_ == kHop - 1u) publish();
  }
  hop_pos_ = hop_pos_ == kHop - 1u ? 0u : hop_pos_ + 1u;

  const std::uint32_t run = run_;
  if (ramp_left_ != 0u) {
    for (std::uint32_t i = 1; i <= run; ++i) k_[i] += dk_[i];
    gain_ += dgain_;
    if (--ramp_left_ == 0u) {  // the last step lands on the target
      for (std::uint32_t i = 1; i <= run; ++i) k_[i] = kt_[i];
      gain_ = gain_t_;
    }
  }
  float f = (c - kCarEmph * car_prev_) * gain_;
  car_prev_ = c;
  for (std::uint32_t i = run; i >= 1u; --i) {
    f -= k_[i] * b_[i - 1u];
    b_[i] = b_[i - 1u] + k_[i] * f;
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
  const bool odd1 = odd1_, odd2 = odd2_;
  const std::uint32_t n1 = decimate(dec_mod_[0], m0, n, odd1, m1);
  decimate(dec_car_[0], c0, n, odd1, c1);
  const std::uint32_t n2 = decimate(dec_mod_[1], m1, n1, odd2, m2);
  decimate(dec_car_[1], c1, n1, odd2, c2);
  odd1_ = ((static_cast<unsigned>(odd1) + n) & 1u) != 0u;
  odd2_ = ((static_cast<unsigned>(odd2) + n1) & 1u) != 0u;

  for (std::uint32_t i = 0; i < n2; ++i) tick(m2[i], c2[i], y2[i]);

  for (std::uint32_t i = 0; i < n1; ++i) y1[i] = 0.0f;
  interpolate(interp_[1], y2, n1, odd2, y1);
  for (std::uint32_t i = 0; i < n; ++i) out[i] = 0.0f;
  interpolate(interp_[0], y1, n, odd1, out);

  // Breath: the modulator above 5 kHz, as loud as the carrier is open.
  const float target = peak > kOpenPeak ? 1.0f : 0.0f;
  const float level = kBreath * (0.5f + 0.5f * thru_);
  float s1 = hp_s1_, s2 = hp_s2_, open = open_;
  for (std::uint32_t i = 0; i < n; ++i) {
    const float x = m0[i];
    const float h = hp_b0_ * x + s1;
    s1 = hp_b1_ * x - hp_a1_ * h + s2;
    s2 = hp_b2_ * x - hp_a2_ * h;
    open = open < target ? (open + open_step_ < target ? open + open_step_ : target)
                         : (open - open_step_ > target ? open - open_step_ : target);
    out[i] += level * open * h;
  }
  hp_s1_ = flush(s1);
  hp_s2_ = flush(s2);
  open_ = open;
  macs_last_ += n * 12u;

  // Denormals: states decay to zero under silence; flush them once a block.
  for (std::uint32_t i = 0; i <= run_; ++i) b_[i] = flush(b_[i]);
  for (auto& h : dec_mod_) settle(h);
  for (auto& h : dec_car_) settle(h);
  for (auto& h : interp_) settle(h);
  emph_prev_ = flush(emph_prev_);
  car_prev_ = flush(car_prev_);
  gain_ = flush(gain_);
}

}  // namespace o3_voc03er
