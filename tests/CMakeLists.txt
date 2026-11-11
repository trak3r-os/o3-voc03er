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
o3sdk_app_test_suite(o3_voc03er SOURCES ${test_sources} DEPENDS unit_voice unit_vocoder unit_tract unit_tract_dsp)

# The two longest ctests are cut into shares, so that a parallel run's floor is the longest share and not the longest file.
#  * app_test.cpp: its scenarios case takes O3_VOC03ER_SCENARIO_SHARD=i/n (tests/app_test.cpp, by the sorted file names), three shares; a direct run, with no
#    variable, runs all. Share 0 is `app_test` itself (it also runs the file's other cases); the others run that one case only.
#  * sample_edit_test.cpp: O3SDK_TEST_SHARD=i/n (o3-sdk tests/main.cpp), the file's test cases by line mod n, two shares. Share 0 keeps the file's own ctest name,
#    the other is <name>_<i>. Three of its cases take 30 to 40 s each (F4 and F5 / SHIFT + DELETE / a hand cut survives), so two shares is the most that balances.
# Every share has the labels of the file. o3_voc03er_shard_coverage proves, by the lists the shares print, that the shares cover the file's test cases and
# app/scenarios, each once.
set(o3_voc03er_scenario_shards 3)
set(o3_voc03er_sharded sample_edit_test:2)
set_tests_properties(app_test PROPERTIES ENVIRONMENT "O3_VOC03ER_SCENARIO_SHARD=0/${o3_voc03er_scenario_shards}")
get_test_property(app_test_labels app_test LABELS)
math(EXPR last_scenario_shard "${o3_voc03er_scenario_shards} - 1")
foreach(shard RANGE 1 ${last_scenario_shard})
  add_test(NAME app_test_${shard} COMMAND o3_voc03er_tests --source-file=*app_test.cpp "--test-case=app: the scenarios run as they are*" WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  set_tests_properties(app_test_${shard} PROPERTIES ENVIRONMENT "O3_VOC03ER_SCENARIO_SHARD=${shard}/${o3_voc03er_scenario_shards}" LABELS "${app_test_labels}")
endforeach()
foreach(spec IN LISTS o3_voc03er_sharded)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 test)
  list(GET parts 1 n)
  get_test_property(test_labels ${test} LABELS)
  set_tests_properties(${test} PROPERTIES ENVIRONMENT "O3SDK_TEST_SHARD=0/${n}")
  math(EXPR last "${n} - 1")
  foreach(shard RANGE 1 ${last})
    add_test(NAME ${test}_${shard} COMMAND o3_voc03er_tests --source-file=*${test}.cpp WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(${test}_${shard} PROPERTIES ENVIRONMENT "O3SDK_TEST_SHARD=${shard}/${n}" LABELS "${test_labels}")
  endforeach()
endforeach()
# COST (ctest starts the highest first, also in a fresh build directory that has no timing history), seconds: the remote run of 2026-11-11 at 16 jobs, per share.
# app_test was 159 s for the 37 scenarios and its other cases (the shares are about 55 each); sample_edit_test 82 s (shares of about 31 and 51).
set(o3_voc03er_cost app_test:55 sample_edit_test:31 sample_edit_test_1:51 sample_test:37 perform_screen_test:26 song_test:15)
foreach(shard RANGE 1 ${last_scenario_shard})
  list(APPEND o3_voc03er_cost app_test_${shard}:55)
endforeach()
foreach(spec IN LISTS o3_voc03er_cost)
  string(REPLACE ":" ";" parts "${spec}")
  list(GET parts 0 test)
  list(GET parts 1 cost)
  if(TEST ${test})
    set_tests_properties(${test} PROPERTIES COST ${cost})
  endif()
endforeach()
add_test(NAME o3_voc03er_shard_coverage
  COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:o3_voc03er_tests> "-DSHARDS=${o3_voc03er_sharded}" -DSCENARIO_SHARDS=${o3_voc03er_scenario_shards}
          -DSCENARIO_DIR=${PROJECT_SOURCE_DIR}/app/scenarios -P ${CMAKE_CURRENT_SOURCE_DIR}/shard_coverage.cmake
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(o3_voc03er_shard_coverage PROPERTIES TIMEOUT 300)
o3sdk_test_labels(o3_voc03er_shard_coverage AREA o3_voc03er_tests)
