cmake_minimum_required(VERSION 3.20)
project(reaper_lyrics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Windows: link the C/C++ runtime statically, so the DLL loads on any PC without a Visual C++
# Redistributable (a VS 2022/2026 build would otherwise need a recent one).
# (Linux: the .so uses the system's libstdc++/glibc; build.sh prints the versions it needs.)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Where REAPER keeps its settings and UserPlugins folder (Options > Show REAPER resource path).
# Defaults to the standard install; set it for a portable REAPER or a custom location:
#   cmake -S . -B build -DREAPER_RESOURCE_PATH="D:/REAPER"
if(WIN32)
  set(_reaper_default "$ENV{APPDATA}/REAPER")
else()
  set(_reaper_default "$ENV{HOME}/.config/REAPER")
endif()
set(REAPER_RESOURCE_PATH "${_reaper_default}" CACHE PATH "REAPER resource path (contains UserPlugins)")

set(SOURCES
  src/main.cpp
  src/model.cpp
  src/trackstyle.cpp
  src/editor.cpp
  src/panel.cpp
  src/textedit.cpp
  src/platform.cpp
  src/util.cpp
)

if(WIN32)
  list(APPEND SOURCES src/resource.rc WDL/WDL/win32_utf8.c)
else()
  list(APPEND SOURCES src/resource_swell.cpp WDL/WDL/swell/swell-modstub-generic.cpp)
endif()

add_library(reaper_lyrics SHARED ${SOURCES})

# "WDL/WDL" on the include path lets reaper_plugin.h find "../WDL/swell/swell.h"
target_include_directories(reaper_lyrics PRIVATE reaper-sdk/sdk src WDL/WDL)
set_target_properties(reaper_lyrics PROPERTIES PREFIX "" OUTPUT_NAME "reaper_lyrics")

if(WIN32)
  # no WIN32_LEAN_AND_MEAN: WDL's win32_utf8 needs the shell and common-dialog declarations
  target_compile_definitions(reaper_lyrics PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
  target_compile_options(reaper_lyrics PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /utf-8 /EHsc> $<$<COMPILE_LANGUAGE:C>:/w>)
  target_link_libraries(reaper_lyrics PRIVATE gdi32 user32 comdlg32)
  set(PLUGIN_FILE "reaper_lyrics.dll")
else()
  # REAPER provides SWELL at runtime; the modstub fetches the function table
  target_compile_definitions(reaper_lyrics PRIVATE SWELL_PROVIDED_BY_APP NOMINMAX)
  target_compile_options(reaper_lyrics PRIVATE -fPIC -fvisibility=hidden -Wall -Wno-unused-parameter)
  target_link_libraries(reaper_lyrics PRIVATE dl pthread)
  # export only what REAPER calls, not the C++ library code the headers instantiate
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exports.map" "{ global: ReaperPluginEntry; SWELL_dllMain; local: *; };\n")
  target_link_options(reaper_lyrics PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exports.map")
  set(PLUGIN_FILE "reaper_lyrics.so")
endif()
set(INSTALL_DIR "${REAPER_RESOURCE_PATH}/UserPlugins")

# Model tests against a fake REAPER API:
#   cmake -S . -B build -DLYRICS_BUILD_TESTS=ON && cmake --build build --config Release --target model_test
option(LYRICS_BUILD_TESTS "Build the model tests" OFF)
if(LYRICS_BUILD_TESTS)
  set(TEST_SOURCES tests/model_test.cpp src/model.cpp src/trackstyle.cpp src/util.cpp src/textedit.cpp src/platform.cpp src/panel.cpp)
  if(WIN32)
    list(APPEND TEST_SOURCES WDL/WDL/win32_utf8.c)
  else()
    list(APPEND TEST_SOURCES WDL/WDL/swell/swell-modstub-generic.cpp)
  endif()
  add_executable(model_test ${TEST_SOURCES})
  target_include_directories(model_test PRIVATE reaper-sdk/sdk src WDL/WDL)
  if(WIN32)
    target_compile_definitions(model_test PRIVATE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    target_compile_options(model_test PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /utf-8 /EHsc> $<$<COMPILE_LANGUAGE:C>:/w>)
    target_link_libraries(model_test PRIVATE gdi32 user32 comdlg32)
  else()
    target_compile_definitions(model_test PRIVATE SWELL_PROVIDED_BY_APP NOMINMAX)
    target_link_libraries(model_test PRIVATE dl pthread)
  endif()
endif()

# `cmake --build build --target install_to_reaper` copies the plugin into UserPlugins
add_custom_target(install_to_reaper
  COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALL_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:reaper_lyrics> "${INSTALL_DIR}/${PLUGIN_FILE}"
  DEPENDS reaper_lyrics
  COMMENT "Copying ${PLUGIN_FILE} to ${INSTALL_DIR}")
