cmake_minimum_required(VERSION 3.20)
project(n5_multiplexers C)

# N5 - terminal multiplexers (dtach, abduco, dvtm) ported to native Windows.
# See README.md and N1-coreutils/Docs/porter-guide (modules 18-21).

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT MSVC)
    message(FATAL_ERROR "This kata targets MSVC (cl.exe).")
endif()

set(COMMON_FLAGS /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE)

# --- posix_compat: OBJECT library (see compat/src/init.c for why) ----------
file(GLOB COMPAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/compat/src/*.c)
add_library(posix_compat OBJECT ${COMPAT_SOURCES})
target_compile_options(posix_compat PRIVATE ${COMMON_FLAGS})
target_include_directories(posix_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
target_link_libraries(posix_compat PUBLIC ws2_32 advapi32)

# Upstream sources: shadow headers first in the path + forced include.
# Applied per target, never globally: PDCurses must not see them.
function(posix_port target)
    target_link_libraries(${target} PRIVATE posix_compat)
    target_include_directories(${target} BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat/include)
    target_compile_options(${target} PRIVATE ${COMMON_FLAGS}
        /FI${CMAKE_CURRENT_SOURCE_DIR}/compat/include/compat.h)
    # UTF-8 as the process ANSI code page: argv, getenv() and the -A
    # Win32 APIs all speak UTF-8 (Windows 10 1903+).
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/compat/utf8.manifest)
endfunction()

# --- dtach ---------------------------------------------------------------
add_executable(dtach ports/dtach/main.c ports/dtach/master.c ports/dtach/attach.c)
target_include_directories(dtach PRIVATE ports/dtach)
posix_port(dtach)

# --- abduco --------------------------------------------------------------
add_executable(abduco ports/abduco/abduco.c)
target_include_directories(abduco PRIVATE ports/abduco)
target_compile_definitions(abduco PRIVATE VERSION="0.6-win32" NDEBUG)
posix_port(abduco)

# --- PDCursesMod (wincon backend) for dvtm ----------------------------------
set(PDC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/upstream/PDCursesMod)
if(EXISTS ${PDC_DIR}/curses.h)
    file(GLOB PDC_CORE ${PDC_DIR}/pdcurses/*.c)
    file(GLOB PDC_PORT ${PDC_DIR}/wincon/*.c)
    add_library(pdcurses STATIC ${PDC_CORE} ${PDC_PORT})
    target_include_directories(pdcurses PUBLIC ${PDC_DIR})
    target_compile_definitions(pdcurses PUBLIC PDC_WIDE PDC_FORCE_UTF8)
    target_compile_options(pdcurses PRIVATE /W1 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
    target_link_libraries(pdcurses PUBLIC user32 advapi32 winmm)

    add_executable(dvtm ports/dvtm/dvtm.c ports/dvtm/vt.c)
    target_include_directories(dvtm PRIVATE ports/dvtm)
    # PDC_NCMOUSE selects the ncurses mouse API in curses.h: application-only,
    # the library itself must be built without it (it defines both APIs).
    target_compile_definitions(dvtm PRIVATE VERSION="0.15-win32" NDEBUG PDC_NCMOUSE)
    target_link_libraries(dvtm PRIVATE pdcurses)
    posix_port(dvtm)
else()
    message(WARNING "upstream/PDCursesMod missing: run fetch-upstream first. dvtm skipped.")
endif()

# --- test driver: a scripted ConPTY "terminal" ---------------------------------
add_executable(ptyrun tests/ptyrun.c)
target_compile_options(ptyrun PRIVATE /W3 /utf-8 /D_CRT_SECURE_NO_WARNINGS)
