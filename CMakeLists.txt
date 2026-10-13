cmake_minimum_required(VERSION 3.21)
project(MediaAnalysisEngine VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
    add_compile_options(/utf-8 /W3 /EHsc)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BUILD_GUI   "Build the Qt desktop application" ON)
option(BUILD_TESTS "Build the unit tests (run with ctest)" ON)

# ---------------------------------------------------------------------------
# Dependencies
#
#   Windows (recommended): vcpkg
#       vcpkg install qtbase[core,gui,widgets] opencv4
#       cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
#
#   Qt Charts is NOT needed: the graphs are drawn by our own small ChartWidget.
# ---------------------------------------------------------------------------
find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# spectra_core : the processing engine. No Qt inside - only C++17 + OpenCV.
#   TaskQueue, ThreadPool, FrameAnalyzer, EventDetector, ResultManager,
#   PerformanceMonitor, VideoProcessor, BenchmarkManager, ReportWriter
# ---------------------------------------------------------------------------
add_library(spectra_core STATIC
    src/TaskQueue.cpp
    src/ThreadPool.cpp
    src/FrameAnalyzer.cpp
    src/EventDetector.cpp
    src/ResultManager.cpp
    src/PerformanceMonitor.cpp
    src/VideoProcessor.cpp
    src/BenchmarkManager.cpp
    src/ReportWriter.cpp
)
target_include_directories(spectra_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)
target_link_libraries(spectra_core PUBLIC ${OpenCV_LIBS} Threads::Threads)
if(WIN32)
    target_compile_definitions(spectra_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(spectra_core PUBLIC psapi)
endif()

# ---------------------------------------------------------------------------
# spectra_cli : run the engine from a console (also a handy fallback)
# ---------------------------------------------------------------------------
add_executable(spectra_cli src/cli_main.cpp)
target_link_libraries(spectra_cli PRIVATE spectra_core)

# ---------------------------------------------------------------------------
# MediaAnalysisEngine : the Qt desktop application
# ---------------------------------------------------------------------------
if(BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

    set(CMAKE_AUTOMOC ON)

    add_executable(MediaAnalysisEngine WIN32
        src/main.cpp
        src/MainWindow.cpp
        src/ApplicationController.cpp
        src/VideoPlayer.cpp
        src/VideoView.cpp
        src/ChartWidget.cpp
        include/MainWindow.hpp
        include/ApplicationController.hpp
        include/VideoPlayer.hpp
        include/VideoView.hpp
        include/ChartWidget.hpp
    )
    target_link_libraries(MediaAnalysisEngine PRIVATE spectra_core Qt6::Core Qt6::Gui Qt6::Widgets)

    # Windows + vcpkg: copy the Qt "platform plugin" next to the .exe.
    # Without qwindows.dll Qt shows "no Qt platform plugin could be initialized".
    if(WIN32)
        set(_qt_plugin_dirs "")
        if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            list(APPEND _qt_plugin_dirs "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        endif()
        if(DEFINED _VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
            list(APPEND _qt_plugin_dirs "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
        endif()
        find_file(QT_QWINDOWS_RELEASE qwindows.dll
                  PATHS ${_qt_plugin_dirs}
                  PATH_SUFFIXES Qt6/plugins/platforms plugins/platforms
                  NO_DEFAULT_PATH)
        find_file(QT_QWINDOWS_DEBUG qwindowsd.dll
                  PATHS ${_qt_plugin_dirs}
                  PATH_SUFFIXES debug/Qt6/plugins/platforms debug/plugins/platforms
                  NO_DEFAULT_PATH)
        if(QT_QWINDOWS_RELEASE)
            add_custom_command(TARGET MediaAnalysisEngine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:MediaAnalysisEngine>/platforms"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${QT_QWINDOWS_DEBUG}>>,${QT_QWINDOWS_DEBUG},${QT_QWINDOWS_RELEASE}>"
                        "$<TARGET_FILE_DIR:MediaAnalysisEngine>/platforms/"
                COMMENT "Copying Qt platform plugin (qwindows.dll) next to the executable")
        else()
            message(STATUS "qwindows.dll not found automatically. If the app says 'no Qt platform plugin', "
                           "run windeployqt on the .exe or copy qwindows.dll into a 'platforms' folder next to it.")
        endif()

        # Ship the sample video next to the exe so the demo works out of the box.
        add_custom_command(TARGET MediaAnalysisEngine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_CURRENT_SOURCE_DIR}/data/sample_videos"
                    "$<TARGET_FILE_DIR:MediaAnalysisEngine>/sample_videos")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (plain C++ programs, no extra framework). Run:  ctest -C Release --output-on-failure
# ---------------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    foreach(_t taskqueue threadpool frameanalyzer eventdetector resultmanager engine)
        add_executable(test_${_t} tests/test_${_t}.cpp)
        target_link_libraries(test_${_t} PRIVATE spectra_core)
        target_include_directories(test_${_t} PRIVATE tests)
        add_test(NAME ${_t} COMMAND test_${_t})
        set_tests_properties(${_t} PROPERTIES TIMEOUT 300)
    endforeach()
    # the engine test also uses the real sample video
    set_tests_properties(engine PROPERTIES
        ENVIRONMENT "SPECTRA_SAMPLE_VIDEO=${CMAKE_CURRENT_SOURCE_DIR}/data/sample_videos/sample_events.mp4")

    # GUI smoke test: runs the real window off-screen. Needs Qt's "offscreen" platform plugin,
    # which Linux Qt packages have but the Windows vcpkg build does not - so it is Linux-only.
    if(BUILD_GUI AND NOT WIN32)
        add_executable(test_gui_smoke tests/test_gui_smoke.cpp
            src/MainWindow.cpp src/ApplicationController.cpp src/VideoPlayer.cpp
            src/VideoView.cpp src/ChartWidget.cpp
            include/MainWindow.hpp include/ApplicationController.hpp include/VideoPlayer.hpp
            include/VideoView.hpp include/ChartWidget.hpp)
        target_link_libraries(test_gui_smoke PRIVATE spectra_core Qt6::Core Qt6::Gui Qt6::Widgets)
        target_include_directories(test_gui_smoke PRIVATE tests)
        add_test(NAME gui_smoke COMMAND test_gui_smoke)
        set_tests_properties(gui_smoke PROPERTIES
            TIMEOUT 300
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;SPECTRA_SAMPLE_VIDEO=${CMAKE_CURRENT_SOURCE_DIR}/data/sample_videos/sample_events.mp4")
    endif()
endif()
