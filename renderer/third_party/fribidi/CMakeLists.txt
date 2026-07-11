cmake_minimum_required(VERSION 3.10)
project(fribidi C)

# FriBidi 1.0.16 core source files (matches lib/meson.build fribidi_sources).
# The .tab.i tables and fribidi-unicode-version.h are pre-generated and
# committed alongside the .c files (generated at vendor time from gen.tab/).
set(FRIBIDI_SOURCES
    lib/fribidi.c
    lib/fribidi-arabic.c
    lib/fribidi-bidi.c
    lib/fribidi-bidi-types.c
    lib/fribidi-char-sets.c
    lib/fribidi-char-sets-cap-rtl.c
    lib/fribidi-char-sets-cp1255.c
    lib/fribidi-char-sets-cp1256.c
    lib/fribidi-char-sets-iso8859-6.c
    lib/fribidi-char-sets-iso8859-8.c
    lib/fribidi-char-sets-utf8.c
    lib/fribidi-deprecated.c
    lib/fribidi-joining.c
    lib/fribidi-joining-types.c
    lib/fribidi-mirroring.c
    lib/fribidi-brackets.c
    lib/fribidi-run.c
    lib/fribidi-shape.c
)

add_library(fribidi STATIC ${FRIBIDI_SOURCES})

# CRITICAL: FRIBIDI_LIB_STATIC is mandatory for static linking on Windows/MinGW.
# Without it, fribidi-common.h decorates symbols with __declspec(dllimport) and
# MinGW emits undefined reference to __imp_fribidi_* when linking.
# PUBLIC so consumers (libass) also pick it up via INTERFACE.
target_compile_definitions(fribidi PUBLIC FRIBIDI_LIB_STATIC)

# HAVE_STRINGIZE is required by common.h (errors out if undefined).
# HAVE_STDLIB_H / HAVE_STRING_H gate the stdlib.h / string.h includes in common.h.
# FRIBIDI_ENTRY= forces empty entry-point decoration (belt-and-suspenders with
# the FRIBIDI_LIB_STATIC path in fribidi-common.h).
target_compile_definitions(fribidi PRIVATE
    HAVE_STDLIB_H=1
    HAVE_STRING_H=1
    HAVE_STRINGIZE=1
    FRIBIDI_ENTRY=
)

target_include_directories(fribidi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
