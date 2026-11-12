# SPDX-License-Identifier: GPL-3.0-or-later
# VOCODER: the carrier (8 paraphonic voices + noise), the 16-band channel vocoder and the Instrument unit that joins them. The library is
# `unit_vocoder`; headers are <o3_voc03er/units/vocoder/*.h>. DSP only: it depends on o3-sdk's dsp layer (o3sdk_unit links it).
o3sdk_unit(vocoder SOURCES src/carrier.cpp src/bands.cpp src/lpc.cpp src/mouth_live.cpp src/talk.cpp src/loop.cpp src/tune.cpp src/autotune.cpp src/robot.cpp src/vocoder.cpp DEPENDS unit_mouth)
# The sample-loop files are built without fused multiply-adds: on the Cortex-M4F VFMA and VMLA take 3 cycles, VMUL plus VADD 2 (single
# issue, in order), so contraction makes the biquad and oscillator loops slower. GCC still emits the non-fused VMLA for a*b+c, which nofma.h (mulb) prevents. Only these files; the rest keeps the default.
# They are also built at -O2, against the image's -Os: the unit's sample loops run every block, and at -Os the rare BLEP branches stay inline, which
# made the carrier's two-oscillator loop 55 instructions where -O2 gives 24 on the common path.
set_source_files_properties(src/bands.cpp src/carrier.cpp src/lpc.cpp src/mouth_live.cpp src/talk.cpp src/tune.cpp src/autotune.cpp src/robot.cpp PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-O2")
