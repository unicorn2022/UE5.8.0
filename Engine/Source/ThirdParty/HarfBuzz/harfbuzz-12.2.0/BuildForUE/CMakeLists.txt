# Copyright Epic Games, Inc. All Rights Reserved.
cmake_minimum_required(VERSION 3.6)

# CMake script to build HarfBuzz 12.2.0 against ICU 78 and FreeType2-2.14.1.
# Output goes to lib/ (no lib-icu78 suffix; HB 12 only links against ICU 78).

set(FREETYPE2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../FreeType2/FreeType2-2.14.1")
set(ICU_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../ICU/icu4c-78_1")

project(harfbuzz)

# C++20 to match ICU 78 headers and the rest of the third-party stack.
set(CMAKE_CXX_STANDARD 20)

# Some platforms need to define these in order to build
option(USE_INTEL_ATOMIC_PRIMITIVES "Platform needs to define HAVE_INTEL_ATOMIC_PRIMITIVES" FALSE)
option(USE_STUB_GETENV "Platform needs to define stub getenv as it doesn't have its own version" FALSE)

# Put our build output one level up so we can easily delete the temporary files and only check-in the final libs
if (NOT DEFINED CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
	set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../")
endif()

# Explicit source list. Files we don't want never enter the build; commented
# entries below document why each excluded file is left out.
set(TPS_HARFBUZZ_SOURCES
	# Core primitives and shaping entry points
	hb-blob.cc
	hb-buffer.cc
	hb-buffer-serialize.cc
	hb-buffer-verify.cc
	hb-common.cc
	hb-face.cc
	hb-face-builder.cc
	hb-fallback-shape.cc
	hb-font.cc
	hb-map.cc
	hb-number.cc
	hb-set.cc
	hb-shape.cc
	hb-shape-plan.cc
	hb-shaper.cc
	hb-static.cc
	hb-style.cc
	hb-unicode.cc

	# Apple Advanced Typography (used by OT layout)
	hb-aat-layout.cc
	hb-aat-map.cc

	# OpenType tables, layout, and per-script shapers
	hb-ot-cff1-table.cc
	hb-ot-cff2-table.cc
	hb-ot-color.cc
	hb-ot-face.cc
	hb-ot-font.cc
	hb-ot-layout.cc
	hb-ot-map.cc
	hb-ot-math.cc
	hb-ot-meta.cc
	hb-ot-metrics.cc
	hb-ot-name.cc
	hb-ot-shape.cc
	hb-ot-shape-fallback.cc
	hb-ot-shape-normalize.cc
	hb-ot-shaper-arabic.cc
	hb-ot-shaper-default.cc
	hb-ot-shaper-hangul.cc
	hb-ot-shaper-hebrew.cc
	hb-ot-shaper-indic.cc
	hb-ot-shaper-indic-table.cc
	hb-ot-shaper-khmer.cc
	hb-ot-shaper-myanmar.cc
	hb-ot-shaper-syllabic.cc
	hb-ot-shaper-thai.cc
	hb-ot-shaper-use.cc
	hb-ot-shaper-vowel-constraints.cc
	hb-ot-tag.cc
	hb-ot-var.cc

	# Variable composites table (new in HB 12; only .cc under src/OT/)
	OT/Var/VARC/VARC.cc

	# Unicode property backends (UE uses ICU at runtime; UCD is the fallback)
	hb-icu.cc
	hb-ucd.cc

	# FreeType bridge (UE creates hb_face_t via hb_ft_face_create)
	hb-ft.cc

	# Excluded files (kept out of the build deliberately):
	# - harfbuzz.cc, harfbuzz-subset.cc: upstream amalgamations; ODR clash with per-file list
	# - hb-coretext{,-font,-shape}.cc:    Apple CoreText shaper; UE uses HB OT shaping
	# - hb-directwrite{,-font,-shape}.cc: Windows DirectWrite shaper; UE uses HB OT shaping
	# - hb-uniscribe.cc, hb-gdi.cc:       Legacy Windows; UE uses HB OT shaping
	# - hb-graphite2.cc, hb-kbts.cc, hb-harfrust.cc: Alternative shapers; UE uses HB OT shaping
	# - hb-glib.cc:                       GLib unicode funcs; UE uses ICU instead
	# - hb-gobject-structs.cc:            GObject bindings; not used by UE
	# - hb-cairo{,-utils}.cc:             Cairo render target; UE renders via Slate
	# - hb-wasm-api.cc, hb-wasm-shape.cc: WASM shaper; not used by UE
	# - hb-paint{,-bounded,-extents}.cc, hb-draw.cc, hb-outline.cc: Paint/Draw API; HB_NO_PAINT/DRAW
	# - hb-subset*.cc:                    Subset library; HB_NO_SUBSET (UE doesn't subset fonts)
	# - test*.cc, main.cc, failing-alloc.c: Internal HB test mains
)
list(TRANSFORM TPS_HARFBUZZ_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/../src/")

# Trust the C library version of round
add_definitions(-DHAVE_ROUND)

# Suppress upstream pragma block that promotes ~37 warnings to errors (hb.hh:63-99).
# Newer Clang fires -Wcast-function-type-strict on intentional FT generic finalizer casts.
add_definitions(-DHB_NO_PRAGMA_GCC_DIAGNOSTIC_ERROR)

# Disable HB subsystems UE doesn't use (matches our source list exclusions).
# HB_NO_PAINT cascades from HB_NO_DRAW at hb-config.hh:143-146 so we don't redefine it.
add_definitions(-DHB_NO_DRAW -DHB_NO_SUBSET -DHB_NO_GOBJECT)

# Some platforms need to define these in order to build
if(USE_INTEL_ATOMIC_PRIMITIVES)
	add_definitions(-DHAVE_INTEL_ATOMIC_PRIMITIVES)
endif()
if(USE_STUB_GETENV)
	add_definitions(-DHAVE_STUB_GETENV)
endif()

# Add the include directory for our own build of FreeType2
add_definitions(-DHAVE_OT -DHAVE_FALLBACK -DHAVE_FREETYPE)
include_directories("${FREETYPE2_ROOT}/devel" "${FREETYPE2_ROOT}/include")

# Add the include directory for our own build of ICU
add_definitions(-DHAVE_ICU -DHAVE_ICU_BUILTIN)
include_directories("${ICU_ROOT}/include")

# Specify that we are using our own allocator
add_definitions(-Dhb_malloc_impl=HarfBuzzMalloc -Dhb_calloc_impl=HarfBuzzCalloc -Dhb_realloc_impl=HarfBuzzRealloc -Dhb_free_impl=HarfBuzzFree)

# Tell CMake to generate a static library
add_library(harfbuzz STATIC ${TPS_HARFBUZZ_SOURCES})
