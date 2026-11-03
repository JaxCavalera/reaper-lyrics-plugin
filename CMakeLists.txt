cmake_minimum_required(VERSION 3.20)
project(reaper_lyrics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SOURCES
  src/main.cpp
  src/model.cpp
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
  set(INSTALL_DIR "$ENV{APPDATA}/REAPER/UserPlugins")
  set(PLUGIN_FILE "reaper_lyrics.dll")
else()
  # REAPER provides SWELL at runtime; the modstub fetches the function table
  target_compile_definitions(reaper_lyrics PRIVATE SWELL_PROVIDED_BY_APP NOMINMAX)
  target_compile_options(reaper_lyrics PRIVATE -fPIC -fvisibility=hidden -Wall -Wno-unused-parameter)
  target_link_libraries(reaper_lyrics PRIVATE dl pthread)
  set(INSTALL_DIR "$ENV{HOME}/.config/REAPER/UserPlugins")
  set(PLUGIN_FILE "reaper_lyrics.so")
endif()

# `cmake --build build --target install_to_reaper` copies the plugin into UserPlugins
add_custom_target(install_to_reaper
  COMMAND ${CMAKE_COMMAND} -E make_directory "${INSTALL_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:reaper_lyrics> "${INSTALL_DIR}/${PLUGIN_FILE}"
  DEPENDS reaper_lyrics
  COMMENT "Copying ${PLUGIN_FILE} to ${INSTALL_DIR}")
