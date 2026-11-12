# SPDX-License-Identifier: GPL-3.0-or-later
# MOUTH: the one header (<o3_voc03er/units/mouth/link.h>) the vocoder and the TRACT unit share, so that neither depends on the other. No sources:
# o3sdk_unit makes an INTERFACE library, `unit_mouth`, that carries the include directory.
o3sdk_unit(mouth)
