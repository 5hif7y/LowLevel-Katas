cmake_minimum_required(VERSION 3.20)
project(openbsd_coreutils C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MSVC specific compiler configuration
if(MSVC)
    add_compile_options(
        /W3
        /utf-8
        /D_CRT_SECURE_NO_WARNINGS
        /D_CRT_NONSTDC_NO_DEPRECATE
        /FI "${CMAKE_CURRENT_SOURCE_DIR}/compat/include/compat.h"
    )
endif()

# Global include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/include
    "C:/Users/Shifty/DEV/vcpkg/installed/x64-windows/include"
)

# bsd_compat: OBJECT library, deliberadamente NO una biblioteca estática.
#
# Una biblioteca estática sólo aporta los .obj que resuelven algún símbolo
# pendiente. compat_init.c no exporta nada que el resto del programa llame: su
# única razón de ser es colgar un inicializador de la sección .CRT$XCU para que
# el CRT lo ejecute antes de main(). Linkeado como .lib, ese objeto se descarta
# y el inicializador nunca corre — ni el modo binario ni el soporte VT100 se
# activan, en silencio y sin ningún error de compilación.
#
# Como OBJECT library los objetos se enlazan siempre, sin lógica de descarte.
file(GLOB COMPAT_SOURCES "compat/src/*.c")
add_library(bsd_compat OBJECT ${COMPAT_SOURCES})
target_link_libraries(bsd_compat PUBLIC advapi32)

# Helper macro for standard single-file utilities
function(add_bsd_tool tool_name source_path)
    add_executable(${tool_name} ${source_path})
    target_link_libraries(${tool_name} PRIVATE bsd_compat)
endfunction()

# 1. Basic Flow and Shell Utilities
add_bsd_tool(cat openbsd-src/bin/cat/cat.c)
add_bsd_tool(echo openbsd-src/bin/echo/echo.c)
add_bsd_tool(pwd openbsd-src/bin/pwd/pwd.c)
add_bsd_tool(sleep openbsd-src/bin/sleep/sleep.c)
add_bsd_tool(yes openbsd-src/usr.bin/yes/yes.c)
add_bsd_tool(true openbsd-src/usr.bin/true/true.c)
add_bsd_tool(false openbsd-src/usr.bin/false/false.c)

# 2. Text Processing & Filters
add_bsd_tool(head openbsd-src/usr.bin/head/head.c)
add_bsd_tool(wc openbsd-src/usr.bin/wc/wc.c)
add_bsd_tool(tee openbsd-src/usr.bin/tee/tee.c)
add_bsd_tool(uniq openbsd-src/usr.bin/uniq/uniq.c)
add_bsd_tool(cut openbsd-src/usr.bin/cut/cut.c)
add_bsd_tool(printf openbsd-src/usr.bin/printf/printf.c)

# 3. Path and File Manipulation
add_bsd_tool(basename openbsd-src/usr.bin/basename/basename.c)
add_bsd_tool(dirname openbsd-src/usr.bin/dirname/dirname.c)
add_bsd_tool(touch openbsd-src/usr.bin/touch/touch.c)
add_bsd_tool(mkdir openbsd-src/bin/mkdir/mkdir.c)
add_bsd_tool(rmdir openbsd-src/bin/rmdir/rmdir.c)

# 4. Multi-File Core Utilities
add_executable(tail
    openbsd-src/usr.bin/tail/forward.c
    openbsd-src/usr.bin/tail/misc.c
    openbsd-src/usr.bin/tail/read.c
    openbsd-src/usr.bin/tail/reverse.c
    openbsd-src/usr.bin/tail/tail.c
)
target_include_directories(tail PRIVATE openbsd-src/usr.bin/tail)
target_link_libraries(tail PRIVATE bsd_compat)

add_executable(tr
    openbsd-src/usr.bin/tr/str.c
    openbsd-src/usr.bin/tr/tr.c
)
target_include_directories(tr PRIVATE openbsd-src/usr.bin/tr)
target_link_libraries(tr PRIVATE bsd_compat)

add_executable(ls
    openbsd-src/bin/ls/cmp.c
    openbsd-src/bin/ls/ls.c
    openbsd-src/bin/ls/main.c
    openbsd-src/bin/ls/print.c
    openbsd-src/bin/ls/utf8.c
    openbsd-src/bin/ls/util.c
)
target_include_directories(ls PRIVATE openbsd-src/bin/ls)
target_link_libraries(ls PRIVATE bsd_compat advapi32)
