# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests (doctest): every *_test.cpp in this directory, one executable (o3_voc03er_tests), one ctest per source file
# (o3sdk_app_test_suite(), o3-sdk/cmake/O3sdkTargets.cmake). Rerun the cmake configure after adding a file.
file(GLOB test_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
# The tract's own tests (o3-sdk/extras/tract/tests) run here too: the sdk never builds extras/.
file(GLOB tract_test_sources CONFIGURE_DEPENDS ${O3SDK_DIR}/extras/tract/tests/*_test.cpp)
list(APPEND test_sources ${tract_test_sources})
# The helper links what the tests need: the kit, the simulator, the host platform and the app library WHOLE (it defines o3sdk_app(),
# which app_test.cpp names), and hands app/scenarios to the sources as the macro APP_SCENARIO_DIR (app_test.cpp runs every scenario
# as it is). DEPENDS: the units the tests exercise.
o3sdk_app_test_suite(o3_voc03er SOURCES ${test_sources} DEPENDS unit_voice unit_vocoder unit_tract_dsp)
