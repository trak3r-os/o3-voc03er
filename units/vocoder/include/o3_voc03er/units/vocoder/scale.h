// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The SCALE parameter's notes, in one place: Autotune snaps to them and the chord engine (app/include/o3_voc03er/chords.h) builds on them, so
// the pitch correction and the chords agree by construction. Index = the SCALE value (autotune.h Autotune::Scale); bit n of a mask = the
// note n semitones above the KEY.

#include <cstdint>

namespace o3_voc03er {

inline constexpr unsigned kScaleTableCount = 7;  // CHROM, MAJOR, MINOR, MAJ-P, MIN-P, BLUES, DORIAN (static_assert'ed against Autotune::kScaleCount in autotune.cpp)

namespace detail {
constexpr unsigned scale_bits(unsigned a, unsigned b, unsigned c, unsigned d, unsigned e, unsigned f, unsigned g) noexcept {
  return (1u << a) | (1u << b) | (1u << c) | (1u << d) | (1u << e) | (1u << f) | (1u << g);
}
}  // namespace detail

inline constexpr unsigned kScaleMask[kScaleTableCount] = {
    0xFFFu,                                        // CHROM
    detail::scale_bits(0, 2, 4, 5, 7, 9, 11),      // MAJOR
    detail::scale_bits(0, 2, 3, 5, 7, 8, 10),      // MINOR
    (1u << 0) | (1u << 2) | (1u << 4) | (1u << 7) | (1u << 9),             // MAJ-P
    (1u << 0) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 10),            // MIN-P
    (1u << 0) | (1u << 3) | (1u << 5) | (1u << 6) | (1u << 7) | (1u << 10),  // BLUES
    detail::scale_bits(0, 2, 3, 5, 7, 9, 10)};    // DORIAN

}  // namespace o3_voc03er
