// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE LOAD's pure logic (docs/UI-DESIGN.md, "SAMPLE LOAD"): what a WAV on the card may be (PCM of 8, 16, 24 or 32 bits or 32-bit float, mono or stereo, 22.05 to 96 kHz),
// the refusal texts, the sink the card job reads into (it walks the RIFF chunks and converts the data to the slot's format as the bytes arrive: mono, 16-bit, 44.1 kHz,
// at most one loop of 16 s), the text that says what was converted, the fades at both ends and the slice cursor of the write into the slot.
// No kit state, no engine: host tests in tests/sample_load_test.cpp (the samples and the converter's quality in tests/wav_convert_test.cpp).

#include <o3_voc03er/units/vocoder/loop.h>

#include <o3sdk/core/byte_io.h>

#include "sample_edit_logic.h"
#include "wav_convert.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er::sample_load {

inline constexpr std::uint32_t kRateHz = wav::kEngineRate;
inline constexpr std::uint32_t kMaxFrames = Loop::kMaxSamples;  // one loop: 16 s, counted after the conversion
inline constexpr std::uint32_t kFadeFrames = Loop::kFadeSamples;
inline constexpr std::uint32_t kSlice = 8192;  // frames one write pass moves: 16 KiB read and 16 KiB written in the SDRAM, a frame's share of the main loop
// The most multiply-adds of the rate converter one main-loop pass spends (about 5 ms at a dozen cycles each [est]). It is checked before every input frame, so a pass
// overshoots it by one frame's work at most (a frame makes at most two outputs of 32 taps, or one of about 70 taps: under 140), not by a whole 2 KiB chunk.
inline constexpr std::uint32_t kPassTaps = 80000;

// ---- the header ------------------------------------------------------------------------------------------------------------------------

enum class Verdict : std::uint8_t {
  Broken,    // too short, a chunk past the end, no fmt or data chunk, the data cut short, a block align that is not the frame (0: a zeroed Info, as the SDRAM starts, is a refusal)
  Ok,
  NotWav,    // no RIFF / WAVE
  NotPcm,    // a compressed or another format tag (not PCM, not float)
  Bits,      // not 8, 16, 24 or 32 bit PCM, not 32-bit float
  Rate,      // outside 22.05 to 96 kHz
  Channels,  // not mono or stereo
  Empty,     // a data chunk without one whole frame
};

// The strip's line for a refusal (static, upper case, at most 24 characters); "" for Ok.
constexpr const char* refusal_text(Verdict v) noexcept {
  switch (v) {
    case Verdict::Ok: return "";
    case Verdict::NotWav: return "NOT A WAV FILE";
    case Verdict::Broken: return "BROKEN WAV FILE";
    case Verdict::NotPcm: return "NEEDS PCM OR FLOAT";
    case Verdict::Bits: return "NEEDS 8 TO 32 BIT";
    case Verdict::Rate: return "NEEDS 22 TO 96 KHZ";
    case Verdict::Channels: return "NEEDS MONO OR STEREO";
    case Verdict::Empty: return "EMPTY WAV FILE";
  }
  return "BROKEN WAV FILE";
}

struct Info {
  Verdict verdict = Verdict::Broken;
  std::uint32_t frames = 0;       // what is in the stage: converted, at most kMaxFrames
  std::uint32_t file_frames = 0;  // what the file's data holds, at its own rate (0: not known, a data size of 0xFFFFFFFF in a file of unknown size)
  std::uint32_t rate = 0;         // the file's, as read from its fmt chunk (also set for a refusal that got that far)
  std::uint8_t channels = 0;
  std::uint8_t bits = 0;
  bool is_float = false;
  bool clipped = false;           // the file is longer than the slot: the slot gets the first 16 s
  // Anything beyond the stereo mix: the rate or the sample format is not the slot's.
  constexpr bool converted() const noexcept { return rate != kRateHz || bits != 16u || is_float; }
  // The file's length as the slot would hold it, in frames of 44.1 kHz and not cut (0 when not known).
  constexpr std::uint32_t total_frames() const noexcept {
    if (rate == 0u || file_frames == 0u) {
      return 0u;
    }
    return static_cast<std::uint32_t>((static_cast<std::uint64_t>(file_frames) * kRateHz + rate - 1u) / rate);
  }
};

namespace detail {
inline std::uint32_t le16(const std::uint8_t* p) noexcept { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8); }
inline std::uint32_t le32(const std::uint8_t* p) noexcept { return le16(p) | (le16(p + 2) << 16); }
inline bool tag_is(const std::uint8_t* p, const char (&id)[5]) noexcept { return std::memcmp(p, id, 4) == 0; }

inline std::size_t put(char* dst, std::size_t cap, std::size_t n, const char* s) noexcept {  // appends `s` at `n`, always leaving room for the NUL
  while (*s != '\0' && n + 1u < cap) {
    dst[n++] = *s++;
  }
  return n;
}
inline std::size_t put_khz(char* dst, std::size_t cap, std::size_t n, std::uint32_t rate) noexcept {  // 44100 -> 44.1K, 22050 -> 22.05K, 48000 -> 48K
  char t[16];
  std::size_t k = 0;
  std::uint32_t whole = rate / 1000u;
  const std::uint32_t rem = rate % 1000u;
  char digits[6];
  std::size_t nd = 0;
  do {
    digits[nd++] = static_cast<char>('0' + whole % 10u);
    whole /= 10u;
  } while (whole != 0u && nd < sizeof digits);
  while (nd > 0u) {
    t[k++] = digits[--nd];
  }
  if (rem != 0u) {
    t[k++] = '.';
    const char f[3] = {static_cast<char>('0' + rem / 100u), static_cast<char>('0' + (rem / 10u) % 10u), static_cast<char>('0' + rem % 10u)};
    std::size_t nf = 3;
    while (nf > 1u && f[nf - 1u] == '0') {
      --nf;
    }
    for (std::size_t i = 0; i < nf; ++i) {
      t[k++] = f[i];
    }
  }
  t[k++] = 'K';
  t[k] = '\0';
  return put(dst, cap, n, t);
}
inline std::size_t put_bits(char* dst, std::size_t cap, std::size_t n, const Info& i) noexcept {
  if (i.is_float) {
    return put(dst, cap, n, "FLOAT");
  }
  char t[4] = {static_cast<char>(i.bits >= 10u ? '0' + i.bits / 10u : '0' + i.bits), i.bits >= 10u ? static_cast<char>('0' + i.bits % 10u) : 'B', i.bits >= 10u ? 'B' : '\0', '\0'};
  return put(dst, cap, n, t);
}
}  // namespace detail

// What was converted, for the strip: "48K 24B TO 44.1K 16B", "48K TO 44.1K", "24B TO 16B", "FLOAT TO 16B"; "" when only a stereo pair was mixed (or nothing was done).
// Writes at most `cap` - 1 characters and a NUL; returns the length.
inline std::size_t conversion_text(const Info& i, char* dst, std::size_t cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  std::size_t n = 0;
  dst[0] = '\0';
  const bool rate = i.rate != kRateHz;
  const bool fmt = i.bits != 16u || i.is_float;
  if (rate) {
    n = detail::put_khz(dst, cap, n, i.rate);
  }
  if (fmt) {
    n = detail::put(dst, cap, n, rate ? " " : "");
    n = detail::put_bits(dst, cap, n, i);
  }
  if (rate || fmt) {
    n = detail::put(dst, cap, n, " TO ");
    if (rate) {
      n = detail::put_khz(dst, cap, n, kRateHz);
    }
    if (fmt) {
      n = detail::put(dst, cap, n, rate ? " 16B" : "16B");
    }
  }
  dst[n] = '\0';
  return n;
}

// ---- the line after a load ---------------------------------------------------------------------------------------------------------------

// The screen's note line is kNoteCols characters of the Label face (8 px each, 748 px wide); a longer note goes on a second line. The first line is the load itself,
// worst case "FIRST 16.000 S LOADED, FILE HAS 97391.123 S, 22.05K 32B TO 44.1K 16B" (79); the second is the NO SIDE hint alone (a slot on no side has no loop to show it).
inline constexpr std::size_t kNoteCols = 93;
inline constexpr const char* kLoadedNoSideTail = "NO SIDE: F2 SPEAKS ON SAMPLE";

inline std::size_t loaded_note(const Info& i, char* dst, std::size_t cap) noexcept {
  if (cap == 0u) {
    return 0;
  }
  std::size_t n = 0;
  dst[0] = '\0';
  const auto secs = [&](std::uint32_t frames) {
    char tm[12];
    sample_edit::time_text(frames, tm);
    n = detail::put(dst, cap, n, tm);
    n = detail::put(dst, cap, n, " S");
  };
  const std::uint32_t total = i.total_frames();
  if (i.clipped && total != 0u) {
    n = detail::put(dst, cap, n, "FIRST ");
    secs(i.frames);
    n = detail::put(dst, cap, n, " LOADED, FILE HAS ");
    secs(total);
  } else if (i.clipped) {  // the size was not known: only that it went on
    n = detail::put(dst, cap, n, "FIRST ");
    secs(i.frames);
    n = detail::put(dst, cap, n, " LOADED");
  } else {
    n = detail::put(dst, cap, n, "LOADED ");
    secs(i.frames);
  }
  char conv[40];
  if (conversion_text(i, conv, sizeof conv) != 0u) {
    n = detail::put(dst, cap, n, ", ");
    n = detail::put(dst, cap, n, conv);
  }
  dst[n] = '\0';
  return n;
}

// ---- the slot's stage ------------------------------------------------------------------------------------------------------------------

// A linear fade in on the first and out on the last kFadeFrames of `n` samples (at most half of them each for a short file): the first and the last sample are 0.
inline void fade_ends(std::int16_t* dst, std::uint32_t n) noexcept {
  const std::uint32_t f = n / 2u < kFadeFrames ? n / 2u : kFadeFrames;
  for (std::uint32_t k = 0; k < f; ++k) {
    const auto g = static_cast<std::int32_t>(k);
    dst[k] = static_cast<std::int16_t>(dst[k] * g / static_cast<std::int32_t>(f));
    dst[n - 1u - k] = static_cast<std::int16_t>(dst[n - 1u - k] * g / static_cast<std::int32_t>(f));
  }
}

// The write from the stage into the slot, a kSlice of frames a pass (main loop): copy, then the fades with the last slice.
enum class Phase : std::uint8_t { Idle, Copy, Done };  // zeroed is Idle (the SDRAM's start state)
struct Write {
  Phase phase = Phase::Idle;
  std::uint32_t pos = 0, frames = 0;
};

inline void write_begin(Write& w, std::uint32_t frames) noexcept {
  w = Write{};
  w.frames = frames;
  w.phase = frames == 0u ? Phase::Done : Phase::Copy;
}

// One slice of the stage `src` (the converted samples) into `dst` (the slot's start); true once done.
inline bool write_step(Write& w, const std::int16_t* src, std::int16_t* dst) noexcept {
  if (w.phase == Phase::Copy) {
    std::uint32_t n = w.frames - w.pos;
    n = n < kSlice ? n : kSlice;
    std::memcpy(dst + w.pos, src + w.pos, static_cast<std::size_t>(n) * sizeof(std::int16_t));
    w.pos += n;
    if (w.pos >= w.frames) {
      fade_ends(dst, w.frames);
      w.phase = Phase::Done;
    }
  }
  return w.phase == Phase::Done;
}

constexpr unsigned write_percent(const Write& w) noexcept {
  return w.phase == Phase::Done ? 100u : (w.frames == 0u ? 0u : static_cast<unsigned>(static_cast<std::uint64_t>(w.pos) * 100u / w.frames));
}

// A load that leaves the slot at the length it had: the length alone does not tell the screens the samples changed.
constexpr bool same_length(std::uint32_t old_length, std::uint32_t new_length) noexcept { return old_length != 0u && old_length == new_length; }

// ---- the sink --------------------------------------------------------------------------------------------------------------------------

// The card job's sink: takes the file's bytes as they come, walks the RIFF chunks (the fmt chunk, any other chunk skipped, odd sizes padded to even, the data), converts the
// data frame by frame (decode, stereo mix, and for a rate other than 44.1 kHz the converter) into `dst` and stops at one loop. A refusal, the end of the data and a full stage
// make settled() true: the screen then cancels the job (the rest of the file is of no use) and calls finish().
// Work per call is bounded: once a pass has spent kPassTaps of converter work write() takes the bytes it has converted so far (a short count) or, at the top of a call, answers
// WouldBlock and takes nothing; the kit offers the rest again on the next pass and begin_pass() opens the next budget. Main loop only (the card job runs there).
class LoadSink final : public o3sdk::core::ByteSink {
 public:
  // `dst`: the stage (kMaxFrames samples); `file_size`: the file's size when the job knows it, else 0 (an external load: a cut data chunk is then found only if the stream
  // ends inside it).
  void bind(std::int16_t* dst, std::uint64_t file_size) noexcept {
    dst_ = dst;
    hint_ = file_size;
    st_ = St::Head;
    acc_len_ = 0;
    pos_ = 0;
    chunks_ = 0;
    skip_left_ = 0;
    chunk_size_ = 0;
    fmt_keep_ = 0;
    have_fmt_ = false;
    direct_ = true;
    bounded_ = false;
    data_left_ = 0;
    fb_ = 0;
    carry_len_ = 0;
    out_ = 0;
    in_frames_ = 0;
    file_frames_ = 0;
    cut_ = false;
    finished_ = false;
    pass_base_ = 0;
    info_ = Info{};
  }

  // A new pass of the main loop: the converter may spend kPassTaps again.
  void begin_pass() noexcept { pass_base_ = rs_.work(); }

  o3sdk::core::IoResult write(const void* src, std::size_t n) noexcept override {
    if (st_ == St::Refused || st_ == St::Done) {
      return {n, o3sdk::core::IoStatus::Ok};  // taken and dropped until the screen cancels
    }
    if (st_ == St::Data && over_budget()) {
      return {0, o3sdk::core::IoStatus::WouldBlock};
    }
    const auto* p = static_cast<const std::uint8_t*>(src);
    std::size_t left = n;
    while (left > 0u) {
      switch (st_) {
        case St::Head:
          if (gather(p, left, 12u)) {
            if (!detail::tag_is(acc_, "RIFF") || !detail::tag_is(acc_ + 8, "WAVE")) {
              refuse(Verdict::NotWav);
            } else {
              acc_len_ = 0;
              st_ = St::ChunkHdr;
            }
          }
          break;
        case St::ChunkHdr:
          if (gather(p, left, 8u)) {
            chunk_header();
          }
          break;
        case St::Fmt:
          if (gather(p, left, fmt_keep_)) {
            fmt_done();
          }
          break;
        case St::Skip: {
          const std::uint64_t take = left < skip_left_ ? left : skip_left_;
          p += take;
          left -= static_cast<std::size_t>(take);
          pos_ += take;
          skip_left_ -= take;
          if (skip_left_ == 0u) {
            st_ = St::ChunkHdr;
          }
          break;
        }
        case St::Data: {
          const std::size_t took = data(p, left);
          p += took;
          left -= took;
          if (left > 0u) {  // the pass's budget is spent: the rest of these bytes is offered again
            return {n - left, o3sdk::core::IoStatus::Ok};
          }
          break;
        }
        case St::Refused:
        case St::Done:
          left = 0;
          break;
      }
    }
    return {n, o3sdk::core::IoStatus::Ok};
  }

  // True once nothing more of the file is wanted: refused, the data complete, or the stage full.
  bool settled() const noexcept { return st_ == St::Refused || st_ == St::Done || full(); }

  // The stream is over (or settled): the verdict and the numbers. Idempotent.
  void finish() noexcept {
    if (finished_) {
      return;
    }
    finished_ = true;
    if (st_ == St::Refused) {
      return;
    }
    if (st_ != St::Data && st_ != St::Done) {
      info_.verdict = Verdict::Broken;  // the file ended before its data chunk began
      return;
    }
    if (st_ == St::Data && bounded_ && data_left_ > 0u && !full()) {
      info_.verdict = Verdict::Broken;  // the data chunk is cut short
      return;
    }
    if (!direct_ && !rs_.full()) {
      rs_.finish();
    }
    info_.frames = out_count();
    info_.file_frames = file_frames_;
    if (info_.frames == 0u) {
      info_.verdict = Verdict::Empty;
      return;
    }
    if (file_frames_ != 0u) {  // the converted length of the whole file, rounded up as the converter's is
      info_.clipped = info_.total_frames() > kMaxFrames;
    } else {
      info_.clipped = cut_;  // size not known: the cut is all we can tell
    }
    info_.verdict = Verdict::Ok;
  }

  const Info& info() const noexcept { return info_; }
  // The converter's multiply-adds so far (0 on the direct path): the cost.
  std::uint32_t work() const noexcept { return direct_ ? 0u : rs_.work(); }

 private:
  enum class St : std::uint8_t { Head, ChunkHdr, Fmt, Skip, Data, Done, Refused };

  bool full() const noexcept { return st_ == St::Data && out_count() >= kMaxFrames; }
  std::uint32_t out_count() const noexcept { return direct_ ? out_ : rs_.out(); }

  void refuse(Verdict v) noexcept {
    info_.verdict = v;
    st_ = St::Refused;
  }

  // Collects bytes into acc_ until it holds `want` (at most sizeof acc_); true when it does.
  bool gather(const std::uint8_t*& p, std::size_t& left, std::size_t want) noexcept {
    const std::size_t take = left < want - acc_len_ ? left : want - acc_len_;
    std::memcpy(acc_ + acc_len_, p, take);
    acc_len_ += take;
    p += take;
    left -= take;
    pos_ += take;
    return acc_len_ == want;
  }

  void chunk_header() noexcept {
    const std::uint32_t size = detail::le32(acc_ + 4);
    const bool is_fmt = detail::tag_is(acc_, "fmt ");
    const bool is_data = detail::tag_is(acc_, "data");
    acc_len_ = 0;
    if (++chunks_ > 64u) {
      refuse(Verdict::Broken);  // a file of nothing but tiny chunks ends here
      return;
    }
    if (is_fmt && !have_fmt_) {
      if (size < 16u) {
        refuse(Verdict::Broken);
        return;
      }
      chunk_size_ = size;
      fmt_keep_ = size < sizeof acc_ ? size : sizeof acc_;
      st_ = St::Fmt;
    } else if (is_data) {
      if (!have_fmt_) {
        refuse(Verdict::Broken);
        return;
      }
      start_data(size);
    } else {
      skip_left_ = static_cast<std::uint64_t>(size) + (size & 1u);
      st_ = skip_left_ == 0u ? St::ChunkHdr : St::Skip;
    }
  }

  void fmt_done() noexcept {
    std::uint32_t tag = detail::le16(acc_);
    if (tag == 0xFFFEu && chunk_size_ >= 40u) {  // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes are the real tag
      tag = detail::le16(acc_ + 24);
    }
    const std::uint32_t channels = detail::le16(acc_ + 2);
    const std::uint32_t rate = detail::le32(acc_ + 4);
    const std::uint32_t align = detail::le16(acc_ + 12);
    const std::uint32_t bits = detail::le16(acc_ + 14);
    info_.rate = rate;
    info_.channels = static_cast<std::uint8_t>(channels < 255u ? channels : 255u);
    info_.bits = static_cast<std::uint8_t>(bits < 255u ? bits : 255u);
    info_.is_float = tag == 3u;
    acc_len_ = 0;
    if (tag != 1u && tag != 3u) {
      refuse(Verdict::NotPcm);
    } else if (tag == 3u ? bits != 32u : (bits != 8u && bits != 16u && bits != 24u && bits != 32u)) {
      refuse(Verdict::Bits);
    } else if (rate < wav::kMinRate || rate > wav::kMaxRate) {
      refuse(Verdict::Rate);
    } else if (channels != 1u && channels != 2u) {
      refuse(Verdict::Channels);
    } else if (align != channels * wav::bytes_per_sample(bits)) {
      refuse(Verdict::Broken);
    } else {
      have_fmt_ = true;
      fb_ = align;
      coding_ = tag == 3u ? wav::Coding::Float : wav::Coding::Pcm;
      direct_ = rate == wav::kEngineRate;
      const std::uint64_t rest = static_cast<std::uint64_t>(chunk_size_ - fmt_keep_) + (chunk_size_ & 1u);
      skip_left_ = rest;
      st_ = rest == 0u ? St::ChunkHdr : St::Skip;
    }
  }

  void start_data(std::uint32_t size) noexcept {
    std::uint64_t declared = size;
    bounded_ = true;
    if (hint_ != 0u) {
      if (pos_ > hint_) {
        refuse(Verdict::Broken);
        return;
      }
      const std::uint64_t left = hint_ - pos_;
      if (size == 0xFFFFFFFFu) {  // a writer that never came back to the header: the rest of the file
        declared = left;
      } else if (declared > left) {
        refuse(Verdict::Broken);  // the file ends before the data does
        return;
      }
    } else if (size == 0xFFFFFFFFu) {
      bounded_ = false;
    }
    if (bounded_) {
      file_frames_ = static_cast<std::uint32_t>(declared / fb_);
      if (file_frames_ == 0u) {
        refuse(Verdict::Empty);
        return;
      }
      data_left_ = declared;
    }
    if (!direct_ && !rs_.begin(info_.rate, kRateHz, dst_, kMaxFrames)) {
      refuse(Verdict::Rate);
      return;
    }
    pass_base_ = 0;  // the converter's work counter starts again with it
    st_ = St::Data;
  }

  bool over_budget() const noexcept { return !direct_ && rs_.work() - pass_base_ >= kPassTaps; }

  // `n` bytes of the file inside the data chunk (and whatever follows it, which is dropped). Returns how many it took: all of them, unless the pass's converter budget ran
  // out first (checked before each frame), when it is the bytes up to the frame it did not start.
  std::size_t data(const std::uint8_t* p, std::size_t n) noexcept {
    std::size_t m = n;
    if (bounded_ && m > data_left_) {
      m = static_cast<std::size_t>(data_left_);
    }
    std::size_t took = 0;
    while (took < m && !full() && !over_budget()) {
      if (carry_len_ > 0u || m - took < fb_) {
        const std::size_t take = m - took < fb_ - carry_len_ ? m - took : fb_ - carry_len_;
        std::memcpy(carry_ + carry_len_, p + took, take);
        carry_len_ += take;
        took += take;
        if (carry_len_ < fb_) {
          break;
        }
        frame(carry_);
        carry_len_ = 0;
      } else {
        frame(p + took);
        took += fb_;
      }
    }
    if (bounded_) {
      data_left_ -= took;
    }
    if (full()) {
      cut_ = true;
      st_ = St::Done;
      return n;
    }
    if (bounded_ && data_left_ == 0u) {
      st_ = St::Done;
      return n;
    }
    return took;
  }

  void frame(const std::uint8_t* p) noexcept {
    if (direct_) {
      dst_[out_++] = wav::frame_to_16(p, info_.bits, coding_, info_.channels);
    } else {
      rs_.push(wav::frame_to_float(p, info_.bits, coding_, info_.channels));
    }
    ++in_frames_;
  }

  wav::Resampler rs_;
  alignas(4) std::uint8_t acc_[40] = {};   // the 12-byte head, an 8-byte chunk header, the first 40 bytes of the fmt chunk
  alignas(4) std::uint8_t carry_[8] = {};  // a frame split across two writes (at most 2 x 4 bytes)
  std::int16_t* dst_ = nullptr;
  std::uint64_t hint_ = 0, pos_ = 0, skip_left_ = 0, data_left_ = 0;
  std::size_t acc_len_ = 0, carry_len_ = 0, fmt_keep_ = 0, fb_ = 0;
  std::uint32_t chunk_size_ = 0, chunks_ = 0, out_ = 0, in_frames_ = 0, file_frames_ = 0, pass_base_ = 0;
  St st_ = St::Head;
  wav::Coding coding_ = wav::Coding::Pcm;
  bool have_fmt_ = false, direct_ = true, bounded_ = false, cut_ = false, finished_ = false;
  Info info_;
};

// ---- why a load is refused (UX3-5) ---------------------------------------------------------------------------------------------------------

// What keeps a slot from taking a load: its own take, its loop (playing or queued), or a slice of some slot borrowing its memory. The checks run in this
// order, so the line names the first thing the player has to stop.
enum class Busy : std::uint8_t { None, Recording, Loop, Slice };

constexpr Busy busy_of(bool recording, bool loop_on, bool slice_on) noexcept {
  return recording ? Busy::Recording : (loop_on ? Busy::Loop : (slice_on ? Busy::Slice : Busy::None));
}

constexpr const char* busy_text(Busy b) noexcept {
  switch (b) {
    case Busy::Recording: return "SLOT IS RECORDING: STOP THE TAKE FIRST";
    case Busy::Loop:      return "SLOT LOOP IS ON: STOP THE LOOP FIRST";
    case Busy::Slice:     return "A SLICE IS SOUNDING: WAIT FOR IT";
    case Busy::None:      break;
  }
  return "";
}

// The list shows at most `cap` files of a folder that holds more: the line that says so, empty when the list is whole.
constexpr const char* capped_text(bool truncated) noexcept { return truncated ? "LIST CUT AT 64 FILES: THE FIRST 64 SHOW" : ""; }


}  // namespace o3_voc03er::sample_load
