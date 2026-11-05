# SPDX-License-Identifier: GPL-3.0-or-later
# VOCODER: the carrier (8 paraphonic voices + noise), the 16-band channel vocoder and the Instrument unit that joins them. The library is
# `unit_vocoder`; headers are <o3_voc03er/units/vocoder/*.h>. DSP only: it depends on o3-sdk's dsp layer (o3sdk_unit links it).
o3sdk_unit(vocoder SOURCES src/carrier.cpp src/bands.cpp src/vocoder.cpp)
