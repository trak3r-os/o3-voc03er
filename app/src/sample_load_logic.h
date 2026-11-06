// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// SAMPLE LOAD's pure logic (docs/UI-DESIGN.md, "SAMPLE LOAD"): what a WAV on the card may be (16-bit PCM, 44.1 kHz, mono or stereo), the refusal texts,
// the clip to one loop (16 s), the stereo mix, the fades at both ends, the slice cursor of the write into the slot, and the sink the card job reads into.
// No kit state, no engine: host tests in tests/sample_load_test.cpp.

#include <o3_voc03er/units/vocoder/loop.h>

#include <o3sdk/core/byte_io.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace o3_voc03er::sample_load {

inline constexpr std::uint32_t kRateHz = 44100;
inline constexpr std::uint32_t kMaxFrames = Loop::kMaxSamples;  // one loop: 16 s
inline constexpr std::uint32_t kFadeFrames = Loop::kFadeSamples;
inline constexpr std::uint32_t kSlice = 8192;  // frames one write pass moves: at most 32 KiB read and 16 KiB written in the SDRAM, a frame's share of the main loop
inline constexpr std::size_t kHeadSlack = 8192;  // room before the data for the header and its other chunks
// The stage holds the file from its first byte: the header, then 16 s of stereo at most. A longer file is cut there.
inline constexpr std::size_t kStageBytes = static_cast<std::size_t>(kMaxFrames) * 2u * sizeof(std::int16_t) + kHeadSlack;
static_assert(kStageBytes % 4u == 0u, "the stage is whole words");

// ---- the header ------------------------------------------------------------------------------------------------------------------------

enum class Verdict : std::uint8_t {
  Broken,    // too short, a chunk past the end, no data chunk, the data cut short (0: a zeroed Info, as the SDRAM starts, is a refusal)
  Ok,
  NotWav,    // no RIFF / WAVE
  NotPcm,    // a float, a compressed or another format tag
  Bits,      // not 16 bit
  Rate,      // not 44.1 kHz
  Channels,  // not mono or stereo
  Empty,     // a data chunk without one whole frame
};

// The strip's line for a refusal (static, upper case); "" for Ok.
constexpr const char* refusal_text(Verdict v) noexcept {
  switch (v) {
    case Verdict::Ok: return "";
    case Verdict::NotWav: return "NOT A WAV FILE";
    case Verdict::Broken: return "BROKEN WAV FILE";
    case Verdict::NotPcm: return "NEEDS PCM, NOT FLOAT";
    case Verdict::Bits: return "NEEDS 16 BIT";
    case Verdict::Rate: return "NEEDS 44.1 KHZ";
    case Verdict::Channels: return "NEEDS MONO OR STEREO";
    case Verdict::Empty: return "EMPTY WAV FILE";
  }
  return "BROKEN WAV FILE";
}

struct Info {
  Verdict verdict = Verdict::Broken;
  std::uint32_t data_offset = 0;   // the first byte of the data in the file
  std::uint32_t frames = 0;        // what goes into the slot: the file's frames, at most kMaxFrames (and what the stage held)
  std::uint32_t file_frames = 0;   // what the file holds
  std::uint8_t channels = 0;
  bool clipped = false;            // frames < file_frames: the slot gets the first 16 s
};

namespace detail {
inline std::uint32_t le16(const std::uint8_t* p) noexcept { return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8); }
inline std::uint32_t le32(const std::uint8_t* p) noexcept { return le16(p) | (le16(p + 2) << 16); }
inline bool tag_is(const std::uint8_t* p, const char (&id)[5]) noexcept { return std::memcmp(p, id, 4) == 0; }
}  // namespace detail

// `b`: the first `have` bytes of the file; `file_size`: the whole file. Reads only inside `have`; never writes.
inline Info parse(const std::uint8_t* b, std::size_t have, std::uint64_t file_size) noexcept {
  Info info;
  if (b == nullptr || have < 12u) {
    return info;  // Broken
  }
  if (!detail::tag_is(b, "RIFF") || !detail::tag_is(b + 8, "WAVE")) {
    info.verdict = Verdict::NotWav;
    return info;
  }
  bool have_fmt = false;
  std::uint32_t channels = 0;
  std::uint64_t pos = 12;
  for (unsigned chunks = 0; chunks < 64u; ++chunks) {  // bounded: a file of nothing but tiny chunks ends here, as Broken
    if (pos + 8u > have) {
      return info;
    }
    const std::uint8_t* c = b + pos;
    const std::uint64_t size = detail::le32(c + 4);
    const std::uint64_t body = pos + 8u;
    if (detail::tag_is(c, "fmt ")) {
      if (size < 16u || body + size > have) {
        return info;
      }
      std::uint32_t tag = detail::le16(c + 8);
      if (tag == 0xFFFEu && size >= 40u) {  // WAVE_FORMAT_EXTENSIBLE: the sub-format's first two bytes are the real tag
        tag = detail::le16(c + 8 + 24);
      }
      channels = detail::le16(c + 10);
      const std::uint32_t rate = detail::le32(c + 12);
      const std::uint32_t bits = detail::le16(c + 22);
      if (tag != 1u) {
        info.verdict = Verdict::NotPcm;
      } else if (bits != 16u) {
        info.verdict = Verdict::Bits;
      } else if (rate != kRateHz) {
        info.verdict = Verdict::Rate;
      } else if (channels != 1u && channels != 2u) {
        info.verdict = Verdict::Channels;
      } else {
        have_fmt = true;
      }
      if (!have_fmt) {
        return info;
      }
    } else if (detail::tag_is(c, "data")) {
      if (!have_fmt || body > file_size) {
        return info;
      }
      const std::uint64_t left = file_size - body;
      std::uint64_t declared = size;
      if (declared == 0xFFFFFFFFu) {  // a writer that never came back to the header: the rest of the file
        declared = left;
      }
      if (declared > left) {
        return info;  // the file ends before the data does
      }
      const std::uint32_t fb = channels * 2u;
      const std::uint64_t file_frames = declared / fb;
      if (file_frames == 0u) {
        info.verdict = Verdict::Empty;
        return info;
      }
      const std::uint64_t staged = have > body ? have - body : 0u;
      std::uint64_t frames = file_frames < staged / fb ? file_frames : staged / fb;
      frames = frames < kMaxFrames ? frames : kMaxFrames;
      if (frames == 0u) {
        return info;  // the data starts past what the stage holds
      }
      info.verdict = Verdict::Ok;
      info.data_offset = static_cast<std::uint32_t>(body);
      info.frames = static_cast<std::uint32_t>(frames);
      info.file_frames = static_cast<std::uint32_t>(file_frames);
      info.channels = static_cast<std::uint8_t>(channels);
      info.clipped = frames < file_frames;
      return info;
    }
    pos = body + size + (size & 1u);
  }
  return info;
}

// ---- the samples -----------------------------------------------------------------------------------------------------------------------

// `frames` frames of 16-bit little-endian PCM (`channels` 1 or 2, no alignment needed) into `dst` as mono; a stereo pair is its mean.
inline void mix_frames(const std::uint8_t* src, std::uint32_t frames, std::uint8_t channels, std::int16_t* dst) noexcept {
  const auto s16 = [](const std::uint8_t* p) noexcept { return static_cast<std::int32_t>(static_cast<std::int16_t>(detail::le16(p))); };
  if (channels == 2u) {
    for (std::uint32_t i = 0; i < frames; ++i) {
      dst[i] = static_cast<std::int16_t>((s16(src + 4u * static_cast<std::size_t>(i)) + s16(src + 4u * static_cast<std::size_t>(i) + 2u)) / 2);
    }
  } else {
    for (std::uint32_t i = 0; i < frames; ++i) {
      dst[i] = static_cast<std::int16_t>(s16(src + 2u * static_cast<std::size_t>(i)));
    }
  }
}

// A linear fade in on the first and out on the last kFadeFrames of `n` samples (at most half of them each for a short file): the first and the last sample are 0.
inline void fade_ends(std::int16_t* dst, std::uint32_t n) noexcept {
  const std::uint32_t f = n / 2u < kFadeFrames ? n / 2u : kFadeFrames;
  for (std::uint32_t k = 0; k < f; ++k) {
    const auto g = static_cast<std::int32_t>(k);
    dst[k] = static_cast<std::int16_t>(dst[k] * g / static_cast<std::int32_t>(f));
    dst[n - 1u - k] = static_cast<std::int16_t>(dst[n - 1u - k] * g / static_cast<std::int32_t>(f));
  }
}

// The write into the slot, a kSlice of frames a pass (main loop): mix, then the fades with the last slice.
enum class Phase : std::uint8_t { Idle, Copy, Done };  // zeroed is Idle (the SDRAM's start state)
struct Write {
  Phase phase = Phase::Idle;
  std::uint32_t pos = 0, frames = 0;
  std::uint8_t channels = 0;  // 0 with the rest: a zeroed Write (the SDRAM's start state) is Idle
};

inline void write_begin(Write& w, std::uint32_t frames, std::uint8_t channels) noexcept {
  w = Write{};
  w.frames = frames;
  w.channels = channels;
  w.phase = frames == 0u ? Phase::Done : Phase::Copy;
}

// One slice of the data at `pcm` (the file's bytes from data_offset) into `dst` (the slot's start); true once done.
inline bool write_step(Write& w, const std::uint8_t* pcm, std::int16_t* dst) noexcept {
  if (w.phase == Phase::Copy) {
    std::uint32_t n = w.frames - w.pos;
    n = n < kSlice ? n : kSlice;
    mix_frames(pcm + static_cast<std::size_t>(w.pos) * w.channels * 2u, n, w.channels, dst + w.pos);
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

// Takes the file's bytes into the stage and drops what does not fit (it still counts them): the read can then end by itself or be cancelled
// once overflowed() says the stage is full. Main loop only (the card job runs there).
class StageSink final : public o3sdk::core::ByteSink {
 public:
  void bind(std::uint8_t* buf, std::size_t cap) noexcept {
    buf_ = buf;
    cap_ = cap;
    held_ = 0;
    seen_ = 0;
  }
  void reset() noexcept {
    held_ = 0;
    seen_ = 0;
  }
  o3sdk::core::IoResult write(const void* src, std::size_t n) noexcept override {
    if (buf_ != nullptr && held_ < cap_) {
      const std::size_t take = n < cap_ - held_ ? n : cap_ - held_;
      std::memcpy(buf_ + held_, src, take);
      held_ += take;
    }
    seen_ += n;
    return {n, o3sdk::core::IoStatus::Ok};
  }
  const std::uint8_t* data() const noexcept { return buf_; }
  std::size_t held() const noexcept { return held_; }
  std::uint64_t seen() const noexcept { return seen_; }
  bool overflowed() const noexcept { return seen_ > cap_; }

 private:
  std::uint8_t* buf_ = nullptr;
  std::size_t cap_ = 0;
  std::size_t held_ = 0;
  std::uint64_t seen_ = 0;
};

}  // namespace o3_voc03er::sample_load
