// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the vocoder unit (a header of its own only so that halfband.h, which Bands and Talk share, can use it). A product that is added or subtracted in the next statement is computed by VMUL and the add by VADD or
// VSUB (2 cycles on the Cortex-M4F), not by VMLA or VFMA (3 cycles, single issue, in order). -ffp-contract=off keeps the compiler from
// fusing (VFMA), but GCC still emits the non-fused VMLA/VMLS for a*b+c; the empty asm makes the product a value the add cannot absorb.
// No instruction is emitted. On the host it is a plain multiply.

namespace o3_voc03er {

inline float mulb(float a, float b) noexcept {
  float p = a * b;
#if defined(__arm__)
  __asm__("" : "+t"(p));
#endif
  return p;
}

}  // namespace o3_voc03er
