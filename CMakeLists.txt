cmake_minimum_required(VERSION 3.21)
project(BanshouNative VERSION 1.0.0 LANGUAGES C CXX RC)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Use MSVC on Windows")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The target and native toolbox must be x86")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
    "MSVC runtime; release publishing always uses the static CRT" FORCE)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/ui")
option(BANSHOU_QT_STATIC "Link Qt statically for the single-file release" OFF)
option(BANSHOU_SINGLE_EXE "Enforce the self-contained release contract" OFF)
if(BANSHOU_SINGLE_EXE)
  set(BANSHOU_QT_STATIC ON CACHE BOOL "" FORCE)
  if(NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "^MultiThreaded")
    message(FATAL_ERROR "BANSHOU_SINGLE_EXE requires the static /MT MSVC runtime")
  endif()
endif()
add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
add_compile_options(/utf-8 /W4 /permissive-)
# 发布版启用整程序优化与函数级 COMDAT，配合链接期 /LTCG 才能真正删掉未引用代码；
# 这是单文件体积从 ~14MB 降到 ~10MB 的主要来源。
add_compile_options(
  "$<$<CONFIG:Release>:/GL>"
  "$<$<CONFIG:Release>:/Gy>"
  "$<$<CONFIG:Release>:/Gw>"
  "$<$<CONFIG:Release>:/Oi>"
  "$<$<CONFIG:Release>:/Ob2>")

# The native build consumes the Python oracle.  Refuse a stale schema instead
# of silently falling back to old hardcoded addresses.
file(READ "${CMAKE_SOURCE_DIR}/resources/catalog.json" BANSHOU_CATALOG_JSON)
file(READ "${CMAKE_SOURCE_DIR}/resources/baseline.json" BANSHOU_BASELINE_JSON)
string(JSON BANSHOU_CATALOG_SCHEMA GET "${BANSHOU_CATALOG_JSON}" schema)
string(JSON BANSHOU_BASELINE_SCHEMA GET "${BANSHOU_BASELINE_JSON}" schema)
if(NOT BANSHOU_CATALOG_SCHEMA STREQUAL "2" OR
   NOT BANSHOU_BASELINE_SCHEMA STREQUAL "2")
  message(FATAL_ERROR
    "catalog.json/baseline.json schema mismatch; run tools/export_baseline.py")
endif()

# 单文件发布优先使用 Qt 5.15.2；本机只提供 5.14.2 源码，可静态自行构建，
# 因此允许通过 BANSHOU_QT_MIN_VERSION 指定最小可用版本（默认 5.14）。
set(BANSHOU_QT_MIN_VERSION "5.14" CACHE STRING
    "Minimum acceptable Qt version for this build")
find_package(Qt5 ${BANSHOU_QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
set(BANSHOU_QT_PLATFORM_PLUGIN_TARGET "")
set(BANSHOU_QT_OFFSCREEN_PLUGIN_TARGET "")
if(BANSHOU_QT_STATIC)
  foreach(_qt_target Qt5::Core Qt5::Gui Qt5::Widgets)
    if(NOT TARGET ${_qt_target})
      message(FATAL_ERROR "Static Qt target is missing: ${_qt_target}")
    endif()
    get_target_property(_qt_type ${_qt_target} TYPE)
    if(NOT _qt_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR "BANSHOU_QT_STATIC requires static Qt libraries; ${_qt_target} is ${_qt_type}")
    endif()
  endforeach()
  if(NOT TARGET Qt5::QWindowsIntegrationPlugin)
    message(FATAL_ERROR "Static Qt Windows platform plugin is missing")
  endif()
  if(NOT TARGET Qt5::QOffscreenIntegrationPlugin)
    message(FATAL_ERROR "Static Qt offscreen plugin is missing for UI contract tests")
  endif()
  set(BANSHOU_QT_PLATFORM_PLUGIN_TARGET Qt5::QWindowsIntegrationPlugin)
  set(BANSHOU_QT_OFFSCREEN_PLUGIN_TARGET Qt5::QOffscreenIntegrationPlugin)
  add_compile_definitions(QT_STATIC)
endif()
set(CAPSTONE_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
set(CAPSTONE_X86_SUPPORT ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/capstone EXCLUDE_FROM_ALL)
add_library(banshou_core STATIC
  src/platform.cpp src/memory.cpp src/image.cpp src/profile.cpp
  src/game.cpp src/diy.cpp src/remote.cpp src/session.cpp src/evidence.cpp
  src/variables.cpp)
target_include_directories(banshou_core PUBLIC include third_party/json third_party/csv third_party/capstone/include)
target_link_libraries(banshou_core PUBLIC capstone-static bcrypt version psapi shlwapi shell32 advapi32 dbghelp ole32)
add_executable(banshou WIN32
  src/main.cpp src/qt_mainwindow.cpp include/qt_mainwindow.hpp
  ui/mainwindow.ui resources/qt_resources.qrc resources/app.rc)
target_include_directories(banshou PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/banshou_autogen/include)
target_link_libraries(banshou PRIVATE banshou_core Qt5::Core Qt5::Gui Qt5::Widgets
  ${BANSHOU_QT_PLATFORM_PLUGIN_TARGET} comctl32)
target_link_options(banshou PRIVATE /SUBSYSTEM:WINDOWS,6.01 /MANIFEST:NO /DEBUG
  /LTCG /OPT:REF /OPT:ICF)
if(BANSHOU_SINGLE_EXE)
  target_link_options(banshou PRIVATE /Brepro)
endif()
target_compile_options(banshou PRIVATE /wd4125)
set_target_properties(banshou PROPERTIES
  OUTPUT_NAME "扳手"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist"
  PDB_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/dist/symbols")
enable_testing()
add_executable(native_tests tests/native_tests.cpp resources/app.rc)
target_link_libraries(native_tests PRIVATE banshou_core)
target_link_options(native_tests PRIVATE /MANIFEST:NO)
target_compile_options(native_tests PRIVATE /UNDEBUG)
add_test(NAME native_contracts COMMAND native_tests)
add_executable(lifecycle_tests tests/lifecycle_tests.cpp resources/app.rc)
target_link_libraries(lifecycle_tests PRIVATE banshou_core)
target_link_options(lifecycle_tests PRIVATE /MANIFEST:NO)
target_compile_options(lifecycle_tests PRIVATE /UNDEBUG)
add_test(NAME lifecycle COMMAND lifecycle_tests)
add_executable(mock_target WIN32 tests/mock_target.cpp)
target_link_libraries(mock_target PRIVATE banshou_core)
target_link_options(mock_target PRIVATE /MANIFEST:NO)

add_executable(qt_ui_tests tests/qt_ui_tests.cpp src/qt_mainwindow.cpp
  include/qt_mainwindow.hpp ui/mainwindow.ui resources/qt_resources.qrc resources/app.rc)
target_include_directories(qt_ui_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/qt_ui_tests_autogen/include)
target_link_libraries(qt_ui_tests PRIVATE banshou_core Qt5::Core Qt5::Gui Qt5::Widgets
  ${BANSHOU_QT_OFFSCREEN_PLUGIN_TARGET})
target_link_options(qt_ui_tests PRIVATE /MANIFEST:NO)
target_compile_options(qt_ui_tests PRIVATE /UNDEBUG /wd4125)
if(BANSHOU_QT_STATIC)
  target_compile_definitions(qt_ui_tests PRIVATE BANSHOU_QT_STATIC_TESTS)
endif()
get_target_property(QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
get_filename_component(QT_RUNTIME_DIRECTORY "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
get_filename_component(QT_PREFIX_DIRECTORY "${QT_RUNTIME_DIRECTORY}" DIRECTORY)
# 用真实 windows 平台跑 UI 契约：offscreen 平台加载不到「微软雅黑」，会退化成
# 更宽的替代字体，导致“文字装不下”的误判。测试不调用 show()，不会弹窗。
add_test(NAME qt_ui_contracts COMMAND ${CMAKE_COMMAND} -E env
  "PATH=${QT_RUNTIME_DIRECTORY};$ENV{PATH}"
  "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_PREFIX_DIRECTORY}/plugins/platforms"
  $<TARGET_FILE:qt_ui_tests>)

add_executable(parity_tests tests/parity_tests.cpp resources/app.rc)
target_link_libraries(parity_tests PRIVATE banshou_core)
target_link_options(parity_tests PRIVATE /MANIFEST:NO)
target_compile_options(parity_tests PRIVATE /wd4125)
target_compile_definitions(parity_tests PRIVATE
  BANSHOU_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
foreach(mode memory profile item variable transaction remote evidence cli
             coverage verify_release platform)
  add_test(NAME ${mode}_contracts COMMAND $<TARGET_FILE:parity_tests> ${mode})
endforeach()
add_test(NAME startup_contracts COMMAND ${CMAKE_COMMAND} -E env
  "PATH=${QT_RUNTIME_DIRECTORY};$ENV{PATH}"
  "QT_QPA_PLATFORM_PLUGIN_PATH=${QT_PREFIX_DIRECTORY}/plugins/platforms"
  $<TARGET_FILE:parity_tests> startup)
