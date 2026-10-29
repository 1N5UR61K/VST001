  cmake_minimum_required(VERSION 3.22)
project(BeadsVST VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- Beads DSP core (MIT, Emilie Gillet / Mutable Instruments) -------------
add_library(beads_core STATIC
  core/beads_engine.cc
  core/beads/dsp/processor.cc
  core/beads/dsp/mu_law.cc
  core/beads/dsp/delay_clock.cc
  core/beads/dsp/delay_controller.cc
  core/beads/dsp/trigger_generator.cc
  core/beads/resources.cc
  core/stmlib/utils/random.cc
  core/stmlib/dsp/units.cc)
target_include_directories(beads_core PUBLIC core)
# TEST selects the portable C++ code paths in stmlib instead of ARM assembly.
target_compile_definitions(beads_core PRIVATE TEST _USE_MATH_DEFINES)
set_target_properties(beads_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(beads_core PRIVATE -Wno-unused-local-typedefs -Wno-unused-variable)
endif()

add_executable(render_test tools/render_test.cc)
target_link_libraries(render_test PRIVATE beads_core)

option(BEADS_BUILD_PLUGIN "Build the JUCE plugin" ON)
if(BEADS_BUILD_PLUGIN AND EXISTS "${CMAKE_SOURCE_DIR}/plugin/CMakeLists.txt")
  add_subdirectory(plugin)
endif()
