=== BUILDING HARFBUZZ ===
This is a modified version of the HarfBuzz source tree. The changes listed below do not necessarily need to be migrated to every new version of HarfBuzz. Evaluate each of these patches.


The CMakeLists.txt in this directory builds HarfBuzz against ICU 78 and FreeType2-2.14.1.
Use BuildForWindows.bat, BuildForAndroid.bat, BuildForLinux.bat etc to produce libs for
each platform. Output goes to lib-icu78/ to coexist with the existing lib/ (ICU 64) build.

=== APPLIED PATCHES ===

Patch 1: -Wcast-function-type error promotion suppressed
  src/hb.hh

  hb.hh promotes -Wcast-function-type to a compiler error for all HarfBuzz translation
  units. Clang 16+ introduced -Wcast-function-type-strict, which fires on the intentional
  function pointer casts in hb-ft.cc used to register FreeType generic finalizers
  (FT_Face vs void*). The promotion is commented out so these casts remain warnings
  rather than errors on newer toolchains.

Patch 2: Unused supp_size variable suppressed
  src/hb-subset-cff1.cc

  supp_size is declared and written to but its value is never read, triggering
  -Wunused-but-set-variable. Since hb.hh promotes -Wunused to an error, this caused
  build failures on newer toolchains. The declaration and its two assignment sites are
  commented out as the variable is dead code.

Patch 3: hb_user_data_item_t::operator== takes const reference (C++20 compatibility)
  src/hb-object.hh

  hb_vector_t<Type>::find() in hb-vector.hh evaluates `array[i] == v`. Under C++20's
  rewritten/reversed candidate rules ([over.match.oper]/9), the compiler also considers
  the synthesized reverse `v == array[i]`. The original member operator on
  hb_user_data_item_t took a non-const lvalue reference, which made the direct and
  reversed candidates equally viable and produced an MSVC C2666 ambiguity error. The
  parameter is now declared `const hb_user_data_item_t&`, which engages the standard
  tie-breaker that prefers the non-rewritten candidate and resolves the call
  unambiguously. Required to compile against C++20 (CMakeLists.txt sets
  CMAKE_CXX_STANDARD 20).
