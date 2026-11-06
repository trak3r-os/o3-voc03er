# SPDX-License-Identifier: GPL-3.0-or-later
# VOCODER: the carrier (8 paraphonic voices + noise), the 16-band channel vocoder and the Instrument unit that joins them. The library is
# `unit_vocoder`; headers are <o3_voc03er/units/vocoder/*.h>. DSP only: it depends on o3-sdk's dsp layer (o3sdk_unit links it).
o3sdk_unit(vocoder SOURCES src/carrier.cpp src/bands.cpp src/talk.cpp src/loop.cpp src/tune.cpp src/autotune.cpp src/vocoder.cpp)
# The two sample-loop files are built without fused multiply-adds: on the Cortex-M4F VFMA and VMLA take 3 cycles, VMUL plus VADD 2 (single
# issue, in order), so contraction makes the biquad and oscillator loops slower. GCC still emits the non-fused VMLA for a*b+c, which nofma.h (mulb) prevents. Only these files; the rest keeps the default.
set_source_files_properties(src/bands.cpp src/carrier.cpp src/tune.cpp src/autotune.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
