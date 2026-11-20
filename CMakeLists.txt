cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
#  KlipIt — game clipping app for Windows (Qt 6 + libobs capture engine)
# ---------------------------------------------------------------------------
set(APP_NAME    "KlipIt")
set(APP_EXE     "KlipIt")
set(APP_ORG     "KlipIt")
set(APP_VERSION "1.0.3")

project(KlipIt VERSION ${APP_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(KLIPIT_BUILD_TESTS "Build tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Concurrent Test)

# --- libobs (capture, GPU encoders, replay buffer) -------------------------
find_package(libobs CONFIG QUIET)
if(TARGET OBS::libobs)
    set(LIBOBS_TARGET OBS::libobs)
elseif(TARGET libobs)
    set(LIBOBS_TARGET libobs)
else()
    find_path(LIBOBS_INCLUDE_DIR obs.h PATH_SUFFIXES obs libobs REQUIRED)
    find_library(LIBOBS_LIBRARY NAMES obs libobs REQUIRED)
    add_library(libobs_imported UNKNOWN IMPORTED)
    set_target_properties(libobs_imported PROPERTIES
        IMPORTED_LOCATION "${LIBOBS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}")
    set(LIBOBS_TARGET libobs_imported)
endif()
message(STATUS "Using libobs target: ${LIBOBS_TARGET}")

# --- FFmpeg libraries (clip trimming, thumbnails, durations) ----------------
# On Windows these are the same FFmpeg DLLs the engine ships with (from OBS's deps).
set(FFMPEG_ROOT "" CACHE PATH "Folder with FFmpeg include/ and lib/ (Windows)")
add_library(klipit_ffmpeg INTERFACE)
if(FFMPEG_ROOT)
    target_include_directories(klipit_ffmpeg INTERFACE "${FFMPEG_ROOT}/include")
    foreach(lib avformat avcodec avutil swscale)
        find_library(FF_${lib} NAMES ${lib} PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
        target_link_libraries(klipit_ffmpeg INTERFACE "${FF_${lib}}")
    endforeach()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswscale)
    target_link_libraries(klipit_ffmpeg INTERFACE PkgConfig::FFMPEG)
endif()

configure_file(src/BuildInfo.h.in ${CMAKE_BINARY_DIR}/generated/BuildInfo.h @ONLY)

# Everything except main.cpp goes into a static library so the tests link the same code.
file(GLOB KL_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/engine/*.cpp src/ui/*.cpp)
file(GLOB KL_HEADERS CONFIGURE_DEPENDS src/core/*.h src/engine/*.h src/ui/*.h)
add_library(klipit_core STATIC ${KL_SOURCES} ${KL_HEADERS} resources/resources.qrc)
target_include_directories(klipit_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(klipit_core PUBLIC
    Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets Qt6::Concurrent ${LIBOBS_TARGET} klipit_ffmpeg)

if(WIN32)
    target_compile_definitions(klipit_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_link_libraries(klipit_core PUBLIC dbghelp user32 dwmapi psapi version)
endif()
if(MSVC)
    target_compile_options(klipit_core PUBLIC /W4 /permissive- /utf-8 /wd4100 /wd4996 /Zi)
    target_link_options(klipit_core PUBLIC /DEBUG /OPT:REF /OPT:ICF)
else()
    target_compile_options(klipit_core PUBLIC -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)
    # Ubuntu's libobs-dev routes SSE through SIMDe; GCC's AVX-512 headers must be seen first.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_compile_options(klipit_core PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-include immintrin.h>)
    endif()
endif()

add_executable(${APP_EXE} WIN32 src/main.cpp resources/windows/app.rc)
if(NOT WIN32)
    set_source_files_properties(resources/windows/app.rc PROPERTIES HEADER_FILE_ONLY ON)
endif()
target_link_libraries(${APP_EXE} PRIVATE klipit_core)
if(WIN32)
    set_target_properties(${APP_EXE} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    if(MSVC)
        target_link_options(${APP_EXE} PRIVATE /MANIFEST:NO)   # app.rc embeds our manifest
    endif()
endif()

if(KLIPIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
