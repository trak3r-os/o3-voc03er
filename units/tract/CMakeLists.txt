# SPDX-License-Identifier: GPL-3.0-or-later
# TRACT: the Effect that wraps o3-sdk's extras/tract Processor (a vocal tract: vowels, closures, consonant hiss) for the vocoder's TRACT mode. The
# library is `unit_tract`; its header is <o3_voc03er/units/tract/tract.h>. DSP only: it depends on o3-sdk's dsp layer (o3sdk_unit links it) and on the
# processor's library, which units/CMakeLists.txt adds from ${O3SDK_DIR}/extras/tract (opt-in on the sdk's side).
o3sdk_unit(tract SOURCES src/tract.cpp DEPENDS unit_tract_dsp)
