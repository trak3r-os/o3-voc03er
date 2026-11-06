# SPDX-License-Identifier: GPL-3.0-or-later
# O3-VOC03ER: the app. o3sdk_app() (o3-sdk/cmake/O3sdkTargets.cmake) builds the library `app_o3_voc03er`, the simulator `o3_voc03er_sim` on the
# host and, with -DO3SDK_PLATFORM=mk66, the image `o3_voc03er.elf` / `.hex` / `.bin` / `.ptf` with its ctests (`o3_voc03er_image`,
# `o3_voc03er_flashconfig`, `o3_voc03er_ramfunc`, `o3_voc03er_usb`).
o3sdk_app(o3_voc03er SOURCES src/app.cpp src/play_screen.cpp src/skin/ornoir.cpp src/perform_screen.cpp src/sample_rec_screen.cpp src/params_screen.cpp src/master_screen.cpp src/sample_screen.cpp src/sample_edit_screen.cpp src/sample_load_screen.cpp src/overview.cpp src/pattern_screen.cpp src/stub_screen.cpp DEPENDS unit_vocoder
  # The boot logo of this image: the kit's O3 logo unless you name a PNG (a path from this directory), which the build converts and links
  # instead (docs/NEW-APP.md, "The boot logo"). A grey picture is drawn white, or in LOGO_COLOR; a picture with colour in its own colours.
  # LOGO ../logo/logo.png
  # LOGO_COLOR 0xFF8000
)
# <o3_voc03er/pad_map.h>: the pure parts of the app, which the tests include.
target_include_directories(app_o3_voc03er PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
